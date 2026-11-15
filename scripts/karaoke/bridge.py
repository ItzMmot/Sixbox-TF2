#!/usr/bin/env python3
"""SixBox's user-local MPRIS/lyrics bridge. No listening port, no bundled API secrets."""
from __future__ import annotations
import argparse
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor
import datetime
import difflib
import email.utils
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

VERSION = 1
MAX_BYTES = 2 * 1024 * 1024
USER_AGENT = 'SixBox-Karaoke/1.0 (https://github.com/ItzMmot/Sixbox-TF2)'


def atomic_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temporary = tempfile.mkstemp(prefix='.karaoke-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def read_json(path: Path):
    if path.stat().st_size > MAX_BYTES:
        raise ValueError('Bridge document exceeds its size limit')
    with path.open(encoding='utf-8') as stream:
        return json.load(stream)


def number(value, default=0.0):
    try:
        value = float(value)
        return value if math.isfinite(value) else default
    except (TypeError, ValueError):
        return default


def stamp(minutes, seconds):
    return int(minutes) * 60 + float(seconds)


def finalize_lines(lines, duration):
    lines = sorted(lines[:2000], key=lambda line: line['start'])
    for index, line in enumerate(lines):
        following = lines[index + 1]['start'] if index + 1 < len(lines) else (duration if duration > line['start'] else line['start'] + 5)
        if following <= line['start']:
            following = duration if duration > line['start'] else line['start'] + 5
        end = number(line.get('end'), following)
        line['end'] = max(line['start'] + .05, end if end > line['start'] else following)
    return lines


def parse_lrc(text, duration=0):
    """LRC including repeated tags, offset metadata, and enhanced word tags."""
    lines = []
    offset_match = re.search(r'\[offset:([+-]?\d+)\]', text, re.I)
    offset = number(offset_match[1]) / 1000 if offset_match else 0
    tag = re.compile(r'\[(\d+):(\d{1,2}(?:\.\d+)?)\]')
    word = re.compile(r'<(\d+):(\d{1,2}(?:\.\d+)?)>')
    for raw in text.splitlines():
        tags = list(tag.finditer(raw))
        if not tags:
            continue
        body = raw[tags[-1].end():].strip()
        words = list(word.finditer(body))
        segments = []
        for index, match in enumerate(words):
            end = words[index + 1].start() if index + 1 < len(words) else len(body)
            value = body[match.end():end]
            if value:
                segments.append({'text': value, 'start': max(0, stamp(*match.groups()) + offset),
                                 'end': stamp(*words[index + 1].groups()) + offset if index + 1 < len(words) else -1})
        value = word.sub('', body)
        if not value:
            continue
        for match in tags:
            start = max(0, stamp(*match.groups()) + offset)
            delta = start - max(0, stamp(*tags[0].groups()) + offset)
            shifted = [{**s, 'start': s['start'] + delta, 'end': s['end'] + delta if s['end'] >= 0 else -1} for s in segments]
            lines.append({'start': start, 'text': value[:8192], 'segments': shifted})
    lines = finalize_lines(lines, duration)
    for line in lines:
        for segment in line['segments']:
            if segment['end'] < segment['start']:
                segment['end'] = line['end']
    return lines


def parse_spicy(response, duration=0):
    body = response.get('Body', response)
    kind = body.get('Type', '')
    lines = []
    for item in body.get('Lines' if kind == 'Static' else 'Content', [])[:2000]:
        if not isinstance(item, dict):
            continue
        lead = item.get('Lead', item)
        syllables = lead.get('Syllables', [])
        segments = []
        for syllable in syllables[:1000]:
            value = str(syllable.get('Text', ''))
            if not syllable.get('IsPartOfWord', False):
                value += ' '
            segments.append({'text': value[:8192], 'start': max(0, number(syllable.get('StartTime'))),
                             'end': max(0, number(syllable.get('EndTime')))})
        text = ''.join(s['text'] for s in segments).rstrip() if segments else str(item.get('Text', lead.get('Text', '')))
        if text:
            lines.append({'text': text[:8192], 'start': max(0, number(item.get('StartTime', lead.get('StartTime', segments[0]['start'] if segments else 0)))),
                          'end': max(1, duration) if kind == 'Static' else number(item.get('EndTime', lead.get('EndTime', segments[-1]['end'] if segments else -1)), -1), 'segments': segments})
    attribution = [{'name': {'spicy_lyrics': 'Spicy Lyrics', 'apple_music': 'Apple Music',
                             'musixmatch': 'Musixmatch', 'spotify': 'Spotify', 'unknown': 'Unknown source (via Spicy Lyrics)'}.get(body.get('source'), 'Spicy Lyrics'),
                    'url': 'https://spicylyrics.org'}]
    upload = body.get('UploadAttribution') or {}
    if body.get('source') == 'spicy_lyrics':
        for field in ('Uploader', 'Maker'):
            person = upload.get(field) or body.get(field.lower()) or {}
            if person.get('username'):
                attribution.append({'name': field + ': ' + str(person['username']),
                                    'url': person.get('url', 'https://spicylyrics.org/uid/' + str(person.get('id', '')))})
    return {'provider': 'Spicy Lyrics', 'synced': kind != 'Static', 'lines': finalize_lines(lines, duration),
            'attribution': attribution}


class ProviderError(Exception):
    pass


class Providers:
    def __init__(self, fetch=None, clock=time.monotonic, sleep=time.sleep):
        self.fetcher = fetch
        self.clock = clock
        self.sleep = sleep
        self.cooldowns = {}
        self.last = {}
        self.cancel = None

    def get(self, provider, url, headers=None):
        if self.cancel is not None and self.cancel.is_set():
            raise ProviderError('Lookup superseded')
        now = self.clock()
        if self.cooldowns.get(provider, 0) > now:
            raise ProviderError(provider + ': rate limited; waiting for Retry-After')
        wait = .3 - (now - self.last.get(provider, -100))
        if wait > 0:
            self.sleep(wait)
        if self.cancel is not None and self.cancel.is_set():
            raise ProviderError('Lookup superseded')
        self.last[provider] = self.clock()
        if self.fetcher:
            return self.fetcher(provider, url, headers or {})
        request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT, 'Accept': 'application/json', **(headers or {})})
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                raw = response.read(MAX_BYTES + 1)
                if len(raw) > MAX_BYTES:
                    raise ProviderError(provider + ': response too large')
                return json.loads(raw)
        except urllib.error.HTTPError as error:
            if error.code in (429, 503):
                retry = error.headers.get('Retry-After', '60')
                try:
                    seconds = float(retry)
                except ValueError:
                    try:
                        seconds = email.utils.parsedate_to_datetime(retry).timestamp() - time.time()
                    except (TypeError, ValueError):
                        seconds = 60
                self.cooldowns[provider] = self.clock() + max(1, seconds)
            # Deliberately never print the URL/query: Musixmatch puts its key there.
            raise ProviderError(f'{provider}: HTTP {error.code}') from None
        except (OSError, ValueError) as error:
            raise ProviderError(provider + ': network or response error') from None

    @staticmethod
    def usable(result):
        return bool(result.get('lines')) or result.get('instrumental', False)

    def lrclib(self, track, _settings):
        args = {'track_name': track['title'], 'artist_name': track['artist']}
        if track.get('album'):
            args['album_name'] = track['album']
        if 1 <= track.get('duration', 0) <= 3600:
            args['duration'] = round(track['duration'])
        try:
            data = self.get('LRCLIB', 'https://lrclib.net/api/get?' + urllib.parse.urlencode(args))
        except ProviderError as error:
            if not any(code in str(error) for code in ('400', '404')):
                raise
            choices = self.get('LRCLIB', 'https://lrclib.net/api/search?' + urllib.parse.urlencode(
                {'track_name': track['title'], 'artist_name': track['artist']}))
            choices = [c for c in choices if not track['duration'] or abs(number(c.get('duration')) - track['duration']) <= 6]
            def score(item):
                return difflib.SequenceMatcher(None, track['title'].casefold(), str(item.get('trackName', '')).casefold()).ratio()
            data = max(choices, key=score) if choices else {}
            if data and score(data) < .65:
                raise ProviderError('LRCLIB: no matching recording')
        synced = data.get('syncedLyrics') or ''
        plain = data.get('plainLyrics') or ''
        lines = parse_lrc(synced, track['duration']) if synced else [
            {'start': 0, 'end': max(1, track['duration']), 'text': value, 'segments': []}
            for value in plain.splitlines()[:2000] if value.strip()]
        return {'provider': 'LRCLIB', 'synced': bool(synced), 'lines': lines,
                'instrumental': bool(data.get('instrumental')), 'attribution': [{'name': 'LRCLIB', 'url': 'https://lrclib.net'}]}

    def spicy(self, track, settings):
        key = settings.get('spicy_key', '').strip()
        if not key:
            raise ProviderError('Spicy Lyrics: add your personal API key in Karaoke settings')
        headers = {'Authorization': 'Bearer ' + key}
        spotify = re.search(r'(?:spotify:track:|open\.spotify\.com/track/)([A-Za-z0-9]{22})', track.get('url', ''))
        if spotify:
            identifier = spotify[1]
        else:
            args = {k: track[k] for k in ('title', 'artist', 'album', 'duration') if track.get(k)}
            found = self.get('Spicy Lyrics', 'https://api.spicylyrics.org/v1/tracks/search?' + urllib.parse.urlencode(args), headers)
            results = found.get('Body', {}).get('results', [])
            exact = [r for r in results if r.get('tier') in ('exact', 'likely') and
                     (not track['duration'] or abs(number(r.get('duration')) - track['duration']) <= 6)]
            if not exact:
                raise ProviderError('Spicy Lyrics: no matching recording')
            identifier = exact[0]['id']
        data = self.get('Spicy Lyrics', 'https://api.spicylyrics.org/v1/lyrics/' + urllib.parse.quote(identifier, safe=''), headers)
        return parse_spicy(data, track['duration'])

    def musixmatch(self, track, settings):
        key = settings.get('musixmatch_key', '').strip()
        if not key:
            raise ProviderError('Musixmatch: fallback needs its own developer API key')
        args = {'q_track': track['title'], 'q_artist': track['artist'], 'apikey': key, 'format': 'json'}
        def request(method, parameters):
            data = self.get('Musixmatch', 'https://api.musixmatch.com/ws/1.1/' + method + '?' + urllib.parse.urlencode(parameters))
            message = data.get('message', {})
            status = message.get('header', {}).get('status_code', 0)
            if status != 200:
                raise ProviderError(f'Musixmatch: API status {status}')
            return message.get('body', {})
        found = request('matcher.track.get', args).get('track', {})
        identifier = found.get('track_id')
        if not identifier:
            raise ProviderError('Musixmatch: recording not found')
        query = {'track_id': identifier, 'apikey': key, 'format': 'json'}
        attribution = [{'name': 'Musixmatch', 'url': found.get('track_share_url', 'https://www.musixmatch.com')}]
        if found.get('has_subtitles'):
            try:
                subtitle = request('track.subtitle.get', {**query, 'subtitle_format': 'lrc'}).get('subtitle', {})
                lines = parse_lrc(subtitle.get('subtitle_body', ''), track['duration'])
                if lines:
                    return {'provider': 'Musixmatch', 'synced': True, 'lines': lines, 'attribution': attribution,
                            'copyright': subtitle.get('subtitle_copyright', '')}
            except ProviderError:
                pass  # Public keys may have only plain-lyrics entitlement.
        lyrics = request('track.lyrics.get', query).get('lyrics', {})
        text = lyrics.get('lyrics_body', '')
        return {'provider': 'Musixmatch', 'synced': False, 'lines': [
            {'start': 0, 'end': max(1, track['duration']), 'text': line, 'segments': []}
            for line in text.splitlines()[:2000] if line.strip()], 'attribution': attribution,
            'copyright': lyrics.get('lyrics_copyright', '')}

    def resolve(self, track, settings, cancel=None):
        self.cancel = cancel
        errors = []
        names = ['spicy', 'lrclib', 'musixmatch'] if int(settings.get('provider', 0)) == 1 else ['lrclib', 'musixmatch']
        for name in names:
            if cancel is not None and cancel.is_set():
                break
            try:
                result = getattr(self, name)(track, settings)
                if self.usable(result):
                    result['error'] = ''
                    result['fallback_errors'] = errors
                    return result
                errors.append(name + ': lyrics not found')
            except Exception as error:
                errors.append(str(error) if isinstance(error, ProviderError) else name + ': invalid provider response')
        return {'provider': '', 'synced': False, 'lines': [], 'attribution': [], 'error': ' | '.join(errors)}


class Mpris:
    def __init__(self):
        import dbus
        self.dbus = dbus
        self.bus = dbus.SessionBus(private=True)
        self.selected = ''
        self.owner = ''
        self.player = None
        self.properties = None
        self.can_repeat = False
        self.pid = 0
        self.application = ''

    def players(self):
        return [str(name) for name in self.bus.list_names() if str(name).startswith('org.mpris.MediaPlayer2.')]

    def poll(self, settings):
        candidates = []
        for name in self.players():
            try:
                obj = self.bus.get_object(name, '/org/mpris/MediaPlayer2')
                props = self.dbus.Interface(obj, 'org.freedesktop.DBus.Properties')
                values = props.GetAll('org.mpris.MediaPlayer2.Player', timeout=.4)
                meta = values.get('Metadata', {})
                title = str(meta.get('xesam:title', ''))[:1024]
                if not title:
                    continue
                url = str(meta.get('xesam:url', ''))[:2048]
                is_spotify = 'spotify' in name.lower() or 'spotify' in url.lower()
                is_youtube = any(k in name.lower() for k in ('youtube', 'ytmusic', 'firefox', 'chromium', 'chrome', 'plasma-browser-integration', 'brave', 'vivaldi', 'edge', 'opera')) or 'youtube' in url
                source = int(settings.get('source', 0))
                if source == 1 and not is_spotify or source == 2 and not is_youtube:
                    continue
                if source != 3 and not (is_spotify or is_youtube):
                    continue
                playing = str(values.get('PlaybackStatus')) == 'Playing'
                score = (4 if playing else 0) + (1 if name == self.selected else 0)
                candidates.append((score, name, obj, props, values, meta, title, url, is_spotify))
            except Exception:
                continue
        if not candidates:
            self.player = self.properties = None
            self.selected = ''
            return None
        _, name, obj, props, values, meta, title, url, spotify = max(candidates, key=lambda item: (item[0], item[1]))
        owner = str(self.bus.get_name_owner(name))
        if name != self.selected or owner != self.owner:
            self.selected = name
            self.owner = owner
            try:
                self.pid = int(self.dbus.Interface(self.bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus'), 'org.freedesktop.DBus').GetConnectionUnixProcessID(name, timeout=.4))
            except Exception:
                self.pid = 0
            try:
                self.application = str(props.Get('org.mpris.MediaPlayer2', 'DesktopEntry', timeout=.4))
            except Exception:
                self.application = ''
            self.player = self.dbus.Interface(obj, 'org.mpris.MediaPlayer2.Player')
            self.properties = props
            try:
                import xml.etree.ElementTree as ET
                xml = self.dbus.Interface(obj, 'org.freedesktop.DBus.Introspectable').Introspect(timeout=.4)
                tree = ET.fromstring(str(xml))
                self.can_repeat = any(p.get('name') == 'LoopStatus' and p.get('access') == 'readwrite'
                                      for interface in tree.findall('interface')
                                      if interface.get('name') == 'org.mpris.MediaPlayer2.Player' for p in interface.findall('property'))
            except Exception:
                self.can_repeat = False
        artists = meta.get('xesam:artist', [])
        artist = ', '.join(str(a) for a in artists) if not isinstance(artists, str) else artists
        try:
            position = max(0, number(props.Get('org.mpris.MediaPlayer2.Player', 'Position', timeout=.4)) / 1e6)
            position_known = True
        except Exception:
            position = 0
            position_known = False
        return {'bus': name, 'pid': self.pid, 'application': self.application, 'id': str(meta.get('mpris:trackid', '')),
                'source': 'Spotify' if spotify else 'Browser media / YouTube Music', 'title': title,
                'artist': artist[:1024], 'album': str(meta.get('xesam:album', ''))[:1024], 'url': url,
                'duration': max(0, number(meta.get('mpris:length')) / 1e6), 'position': position, 'position_known': position_known,
                'sampled': time.time(), 'rate': max(.01, min(4, number(values.get('Rate'), 1))),
                'playing': str(values.get('PlaybackStatus')) == 'Playing', 'repeat': str(values.get('LoopStatus', 'None')),
                'can_play': bool(values.get('CanControl', False) and (values.get('CanPause' if str(values.get('PlaybackStatus')) == 'Playing' else 'CanPlay'))),
                'can_next': bool(values.get('CanGoNext') and values.get('CanControl')), 'can_previous': bool(values.get('CanGoPrevious') and values.get('CanControl')),
                'can_seek': bool(values.get('CanSeek') and values.get('CanControl') and meta.get('mpris:trackid')),
                'can_repeat': self.can_repeat and bool(values.get('CanControl'))}

    def control(self, command, track):
        if (not self.player or command.get('bus') != track.get('bus') or command.get('track') != track.get('id')
                or command.get('identity') != track.get('identity')):
            return 'Track changed; command discarded'
        action = command.get('action')
        try:
            if action in ('play', 'pause') and track['can_play']:
                getattr(self.player, 'Play' if action == 'play' else 'Pause')(timeout=.6)
            elif action in ('next', 'previous') and track['can_next' if action == 'next' else 'can_previous']:
                getattr(self.player, 'Next' if action == 'next' else 'Previous')(timeout=.6)
            elif action == 'seek' and track['can_seek']:
                seconds = max(0, min(track['duration'], number(command.get('value'))))
                self.player.SetPosition(self.dbus.ObjectPath(track['id']), self.dbus.Int64(seconds * 1e6), timeout=.6)
            elif action == 'repeat' and track['can_repeat']:
                value = command.get('value')
                if value not in ('None', 'Track', 'Playlist'):
                    return 'Invalid repeat mode'
                self.properties.Set('org.mpris.MediaPlayer2.Player', 'LoopStatus', self.dbus.String(value), timeout=.6)
            else:
                return 'The selected player does not support this control'
        except Exception:
            return 'The media player rejected the command'
        return ''


class Bridge:
    def __init__(self, directory, media=None, providers=None):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(self.directory, 0o700)
        self.media = media or Mpris()
        self.providers = providers or Providers()
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='lyrics')
        self.future = None
        self.future_key = None
        self.cancel = None
        self.enabled = False
        self.idle = False
        self.cache = OrderedDict()
        self.key = None
        self.epoch = ''
        self.ack = 0
        self.control_error = ''
        self.last_lyrics = {'lines': [], 'attribution': [], 'error': '', 'provider': '', 'synced': False}
        self.last_request = ''
        self.request = {}
        self.sequence = 0
        self.audio = None
        self.published_lyrics = None
        self.lyrics_revision = ''
        try:
            old = read_json(self.directory / 'status.json')
            self.epoch, self.ack = old.get('epoch', ''), int(old.get('ack', 0))
        except (OSError, ValueError):
            pass

    def publish(self, result):
        lyrics = result.get('lyrics', {})
        if lyrics is not self.published_lyrics and lyrics != self.published_lyrics:
            encoded = json.dumps(lyrics, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
            self.lyrics_revision = hashlib.sha256(encoded.encode()).hexdigest()
            atomic_json(self.directory / 'lyrics.json', {'revision': self.lyrics_revision, 'payload': lyrics})
            self.published_lyrics = lyrics
        small = {key: value for key, value in result.items() if key != 'lyrics'}
        small['lyrics_revision'] = self.lyrics_revision
        atomic_json(self.directory / 'status.json', small)

    @staticmethod
    def track_key(track, settings):
        # Credentials distinguish caches without persisting or logging secret values.
        return hashlib.sha256(json.dumps([track.get(k) for k in ('title', 'artist', 'album', 'duration', 'url')] +
                                        [settings.get(k) for k in ('provider', 'spicy_key', 'musixmatch_key')],
                                        ensure_ascii=False).encode()).hexdigest()

    def configure_audio(self, track, enabled):
        if enabled and self.audio is None:
            import importlib.util
            spec = importlib.util.spec_from_file_location('sixbox_music_audio', Path(__file__).with_name('audio.py'))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.audio = module.AudioCapture(self.directory)
        if self.audio is not None:
            self.audio.configure(track, enabled)

    def step(self):
        now = time.time()
        try:
            request = read_json(self.directory / 'request.json')
            fresh = abs(now - number(request.get('updated'))) < 5 and str(request.get('version')) == str(VERSION)
        except (OSError, ValueError):
            request, fresh = {}, False
        enabled = fresh and str(request.get('enabled')).lower() in ('true', '1')
        self.enabled = enabled
        result = {'version': VERSION, 'updated': now, 'epoch': self.epoch, 'ack': self.ack,
                  'track': {}, 'lyrics': self.last_lyrics, 'error': '', 'control_error': self.control_error}
        if not enabled:
            self.configure_audio({}, False)
            if self.cancel:
                self.cancel.set()
            if self.future:
                self.future.cancel()
            self.key = None
            self.last_lyrics = {'lines': [], 'attribution': [], 'error': '', 'provider': '', 'synced': False}
            result['lyrics'] = self.last_lyrics
            result['error'] = 'Karaoke disabled or client disconnected'
            if not self.idle:
                self.publish(result)
            self.idle = True
            return
        self.idle = False
        settings = request.get('settings', {})
        track = self.media.poll(settings)
        if not track:
            self.configure_audio({}, False)
            if self.cancel:
                self.cancel.set()
            self.key = None
            result['error'] = 'No Spotify / YouTube Music media session. Enable browser media controls or use a desktop player.'
            result['lyrics'] = {'lines': [], 'attribution': [], 'error': '', 'provider': '', 'synced': False}
            self.publish(result)
            return
        track.setdefault('sampled', time.time())
        result['updated'] = time.time()
        track['identity'] = hashlib.sha256(json.dumps([track.get(k) for k in ('bus', 'id', 'title', 'artist', 'duration')]).encode()).hexdigest()
        result['track'] = track
        self.configure_audio(track, str(settings.get('visualizer', False)).lower() in ('true', '1'))
        if not track.get('position_known', True):
            result['error'] = 'This media session does not report playback position; synchronized lyrics cannot follow it.'
        command = request.get('command') or {}
        epoch, sequence = str(request.get('epoch', '')), int(command.get('sequence', 0))
        if epoch != self.epoch:
            self.epoch, self.ack = epoch, 0
        if sequence > self.ack:
            # Persist before executing non-idempotent Next/Previous: no replay after restart.
            self.ack = sequence
            result.update(epoch=self.epoch, ack=self.ack)
            self.publish(result)
            self.control_error = (self.media.control(command, track) if 0 <= now - number(command.get('created')) < 4 else 'Playback command expired')
        result.update(epoch=self.epoch, ack=self.ack, control_error=self.control_error)
        key = self.track_key(track, settings)
        if self.cancel and self.future_key != key:
            self.cancel.set()
        if self.future and self.future.done():
            if not self.future.cancelled() and not (self.cancel and self.cancel.is_set()):
                lyrics = self.future.result()
                self.cache[self.future_key] = (now + (60 if lyrics.get('error') else 1800), lyrics)
                while len(self.cache) > 32:
                    self.cache.popitem(last=False)
            self.future = None
        cached = self.cache.get(key)
        if cached and cached[0] > now:
            self.last_lyrics = cached[1]
            self.cache.move_to_end(key)
        else:
            self.last_lyrics = {'lines': [], 'attribution': [], 'error': 'Looking up lyrics...', 'provider': '', 'synced': False}
            if self.future is None:
                self.future_key = key
                self.cancel = threading.Event()
                self.future = self.executor.submit(self.providers.resolve, dict(track), dict(settings), self.cancel)
        self.key = key
        result['lyrics'] = self.last_lyrics
        result['revision'] = key + ':' + str(bool(cached))
        self.publish(result)

    def close(self):
        if self.audio is not None:
            self.audio.close()
        if self.cancel:
            self.cancel.set()
        self.executor.shutdown(wait=True, cancel_futures=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--once', action='store_true')
    args = parser.parse_args()
    bridge = Bridge(args.directory)
    stopping = False
    def stop(*_):
        nonlocal stopping
        stopping = True
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        while not stopping:
            start = time.monotonic()
            try:
                bridge.step()
            except Exception:
                # Never dump provider URLs, credentials or a user's song history in logs.
                atomic_json(args.directory / 'status.json', {'version': VERSION, 'updated': time.time(),
                            'error': 'Media bridge failed. Check the user service and D-Bus session.', 'track': {}, 'lyrics': {}})
            if args.once:
                break
            time.sleep(max(.02, (.25 if bridge.enabled else 1.) - (time.monotonic() - start)))
    finally:
        bridge.close()


if __name__ == '__main__':
    main()
