"""Selected-player audio only: PulseAudio/PipeWire sink-input monitor + short-time FFT.
Never opens a microphone or a mixed/default output monitor.
"""
from __future__ import annotations
import hashlib
import json
import math
import os
from pathlib import Path
import selectors
import shutil
import struct
import subprocess
import tempfile
import threading
import time

MAGIC = 0x31414236  # 6BA1
PACKET = struct.Struct('<IIQQd262f')
READY, NO_STREAM, AMBIGUOUS, NO_BACKEND, CAPTURE_FAILED, PAUSED = 1, 2, 4, 8, 16, 32
RATE, WINDOW, HOP, BANDS, WAVE = 48000, 2048, 512, 64, 96


def identity(track):
    value = track.get('identity', '')
    return int(value[:16], 16) if value else 0


def normalized(value):
    return ' '.join(str(value).casefold().split())


def process_belongs(child, owner):
    """MPRIS owner or its audio subprocess; never another instance of the app."""
    try:
        child, owner = int(child), int(owner)
        if child <= 0 or owner <= 0: return False
        for _ in range(24):
            if child == owner: return True
            if child <= 1: return False
            status = Path('/proc') / str(child) / 'status'
            parent = next(line for line in status.read_text().splitlines() if line.startswith('PPid:'))
            child = int(parent.split()[1])
    except (OSError, TypeError, ValueError, StopIteration):
        pass
    return False


def enrich_streams(streams, clients):
    """Native PipeWire nodes omit process metadata; resolve their exact client."""
    lookup = {str(client.get('index')): client.get('properties', {}) for client in clients}
    return [{**stream, 'properties': {**lookup.get(str(stream.get('client')), {}), **stream.get('properties', {})}}
            for stream in streams]


def flatpak_owner(props, owner, read=None):
    """DBus proxy and audio PID can be siblings, but share the exact app scope.
    Use PipeWire's portal instance ID, never just a friendly application name.
    """
    read = read or (lambda pid: (Path('/proc') / str(int(pid)) / 'cgroup').read_text())
    app, instance = props.get('pipewire.access.portal.app_id', ''), props.get('pipewire.access.portal.instance_id', '')
    if not app or not str(instance).isdigit() or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-' for c in app):
        return False
    if str(props.get('pipewire.sec.uid', os.getuid())) != str(os.getuid()):
        return False
    scope = '/app-flatpak-' + app + '-' + str(instance) + '.scope'
    try:
        if not any(line.rsplit(':',1)[-1].endswith(scope) for line in read(owner).splitlines()):return False
        if props.get('pipewire.protocol') == 'protocol-native':
            return any(line.rsplit(':',1)[-1].endswith(scope) for line in read(props['pipewire.sec.pid']).splitlines())
        return True
    except (OSError, TypeError, ValueError, KeyError):
        return False


def select_stream(track, streams, related=None):
    """Fail closed when a browser doesn't label the musical tab's own stream.
    A shared browser process/PID or a single generic browser stream is not proof
    of tab ownership. Never substitute all audio from that browser or sink.
    """
    if not track or not track.get('playing'):
        return None, PAUSED
    related = related or process_belongs
    pid = str(track.get('pid', ''))
    bus = normalized(track.get('bus', ''))
    title = normalized(track.get('title', ''))
    desktop_spotify = 'spotify' in bus and 'browser' not in bus
    music_apps = ('youtube-music', 'youtube music', 'ytmusic')
    dedicated_youtube = any(name in bus + ' ' + normalized(track.get('application', '')) for name in music_apps)
    candidates = []
    for stream in streams:
        props = stream.get('properties', {})
        binary = normalized(props.get('application.process.binary', '')).rsplit('/', 1)[-1]
        app = normalized(props.get('application.name', ''))
        # For PulseAudio clients sec.pid can name pipewire-pulse itself.
        # Keep the proven application PID path as well as native/portal ownership.
        candidate = props.get('pipewire.sec.pid') if props.get('pipewire.protocol') == 'protocol-native' else None
        owns_pid = bool(pid and (flatpak_owner(props, pid) or related(props.get('application.process.id', ''), pid) or
                                (candidate and related(candidate, pid))))
        if not owns_pid:
            continue
        is_spotify = binary == 'spotify' or app == 'spotify'
        # Desktop Spotify has a dedicated audio process; Chromium/Firefox do not.
        if desktop_spotify:
            if not is_spotify:
                continue
        elif dedicated_youtube:
            if not any(name in binary + ' ' + app for name in music_apps):
                continue
        else:
            # Browser subprocesses may have another PID; exact per-stream title
            # plus a matching browser application is required in that case.
            browser = next((name for name in ('firefox','chromium','chrome','brave','vivaldi','edge','opera','youtube','ytmusic') if name in bus + ' ' + normalized(track.get('application', ''))), '')
            if browser and browser not in binary + ' ' + app:
                continue
            if not owns_pid and not browser:
                continue
        label = normalized(props.get('media.name', ''))
        exact_title = bool(len(title) >= 3 and label in (title, normalized(track.get('artist', '')) + ' - ' + title,
                                                      title + ' - ' + normalized(track.get('artist', ''))))
        if not (desktop_spotify or dedicated_youtube) and not exact_title:
            continue
        candidates.append(stream)
    if len(candidates) == 1:
        return candidates[0], READY
    return None, AMBIGUOUS if candidates or not desktop_spotify else NO_STREAM


class Analyzer:
    """42.7ms Hann window, 10.7ms hop; fast drum transients, no BPM grid lock."""
    def __init__(self):
        import numpy as np
        self.np = np
        self.window = np.hanning(WINDOW).astype(np.float32)
        self.samples = np.zeros((WINDOW, 2), np.float32)
        frequencies = np.fft.rfftfreq(WINDOW, 1 / RATE)
        edges = np.geomspace(45, 16000, BANDS + 1)
        self.slices = [(max(1, int(np.searchsorted(frequencies, edges[i]))),
                        max(2, int(np.searchsorted(frequencies, edges[i+1])))) for i in range(BANDS)]
        self.low = (frequencies >= 45) & (frequencies < 220)
        self.mid = (frequencies >= 350) & (frequencies < 4500)
        self.high = (frequencies >= 6000) & (frequencies < 16000)
        self.previous = np.zeros(WINDOW // 2 + 1)
        self.flux_floor = .001
        self.envelope = .03
        self.transient = 0.
        self.wave_indices = np.linspace(WINDOW - 1024, WINDOW - 1, WAVE).astype(int)

    def process(self, raw):
        np = self.np
        block = np.frombuffer(raw, dtype='<f4').reshape(-1, 2)
        count = min(len(block), WINDOW)
        self.samples[:-count] = self.samples[count:]
        self.samples[-count:] = np.nan_to_num(block[-count:], nan=0, posinf=0, neginf=0)
        np.clip(self.samples, -1, 1, out=self.samples)
        # Energy-average stereo spectra: anti-phase bass must not cancel out.
        spectra = abs(np.fft.rfft(self.samples * self.window[:, None], axis=0)) * (2 / self.window.sum())
        spectrum = np.sqrt(np.mean(spectra * spectra, axis=1))
        rms = float(np.sqrt(np.mean(self.samples * self.samples)))
        peak = float(np.max(abs(self.samples)))
        self.envelope = max(.03, peak, self.envelope * .995)
        bands = []
        for lo, hi in self.slices:
            amplitude = float(np.max(spectrum[lo:max(lo + 1, hi)]))
            # Fixed noise floor; normalization never pumps silence into a beat.
            db = 20 * math.log10(max(1e-7, amplitude / max(.12, self.envelope)))
            bands.append(max(0., min(1., (db + 62) / 56)))
        flux = float(np.maximum(0, spectrum - self.previous).sum())
        self.previous[:] = spectrum
        floor = self.flux_floor
        self.flux_floor += (flux - self.flux_floor) * (.025 if flux > self.flux_floor else .07)
        onset = max(0., min(1., (flux / max(.003, floor) - 1.3) / 4)) if rms > .001 else 0.
        self.transient = max(onset, self.transient * .63)
        def energy(mask):
            return min(1., float(np.sqrt(np.sum(spectrum[mask] ** 2))) / max(.10, self.envelope))
        wave = np.clip(self.samples[self.wave_indices] / max(.18, self.envelope), -1, 1)
        return [rms, peak, energy(self.low), energy(self.mid), energy(self.high), self.transient,
                *bands, *wave[:, 0].tolist(), *wave[:, 1].tolist()]


class AudioCapture:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.condition = threading.Condition()
        self.track = {}
        self.enabled = False
        self.stopping = False
        self.sequence = 0
        self.thread = threading.Thread(target=self.run, name='music-spectrum', daemon=True)
        self.thread.start()

    def configure(self, track, enabled):
        with self.condition:
            self.track, self.enabled = dict(track or {}), bool(enabled)
            self.condition.notify()

    def publish(self, flags, track, values=None):
        self.sequence += 1
        data = PACKET.pack(MAGIC, flags, self.sequence, identity(track), time.time(), *(values or [0.] * 262))
        fd, temporary = tempfile.mkstemp(prefix='.audio-', dir=self.directory)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(data)
            os.replace(temporary, self.directory / 'audio.bin')
        finally:
            if os.path.exists(temporary): os.unlink(temporary)

    @staticmethod
    def query(kind):
        env = dict(os.environ, LC_ALL='C')
        data = subprocess.run(['pactl','-f','json','list',kind], capture_output=True, timeout=.7, check=True, env=env)
        return json.loads(data.stdout)

    def run(self):
        process = None
        selected = None
        selector = selectors.DefaultSelector()
        buffer = bytearray()
        analyzer = None
        last_query = last_publish = 0.
        active_identity = None
        idle = False
        retry = 0.
        def stop_capture():
            nonlocal process, selected, analyzer
            if process:
                try: selector.unregister(process.stdout)
                except (KeyError, ValueError): pass
                process.terminate()
                try: process.wait(timeout=.4)
                except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=.4)
                process.stdout.close()
            process = selected = analyzer = None
            buffer.clear()
        try:
            while True:
                try:
                    with self.condition:
                        if self.stopping: break
                        track, enabled = dict(self.track), self.enabled
                        if not enabled:
                            stop_capture()
                            if not idle: self.publish(PAUSED, track)
                            idle = True
                            self.condition.wait(timeout=1)
                            continue
                    idle = False
                    now = time.monotonic()
                    token = track.get('identity')
                    if token != active_identity:
                        stop_capture(); active_identity = token; last_query = 0.; retry = 0.
                    if not track.get('playing'):
                        stop_capture(); self.publish(PAUSED, track)
                        with self.condition: self.condition.wait(timeout=.1)
                        continue
                    if now - last_query >= 1 and now >= retry:
                        last_query = now
                        try:
                            if not shutil.which('parec') or not shutil.which('pactl'):
                                raise ImportError('PulseAudio tools unavailable')
                            streams = self.query('sink-inputs')
                            # Join by the server's client index; don't infer identity from a node label.
                            streams = enrich_streams(streams, self.query('clients'))
                            stream, flags = select_stream(track, streams)
                            target = (int(stream['index']), int(stream['sink'])) if stream else None
                            if target != selected or (process and process.poll() is not None): stop_capture()
                            if target and not process:
                                import numpy  # optional; do not break lyrics if absent
                                sinks = self.query('sinks')
                                monitor = next((sink.get('monitor_source') for sink in sinks if int(sink['index']) == target[1]), None)
                                if monitor is None: raise RuntimeError('No source for selected stream')
                                # Both monitor AND input filter are mandatory. No mixed-audio fallback.
                                process = subprocess.Popen(['parec','--raw','--format=float32le','--rate=48000','--channels=2',
                                    '--latency-msec=20','--process-time-msec=10','--device='+str(monitor),
                                    '--monitor-stream='+str(target[0]),'--client-name=SixBox Karaoke','--stream-name=Selected music only'],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
                                os.set_blocking(process.stdout.fileno(), False)
                                selector.register(process.stdout, selectors.EVENT_READ)
                                selected = target; analyzer = Analyzer()
                                self.publish(NO_STREAM, track)
                            elif not target:
                                self.publish(flags, track)
                        except (ImportError, OSError, ValueError, subprocess.SubprocessError, RuntimeError):
                            stop_capture(); self.publish(NO_BACKEND, track); retry = now + 2
                    if process:
                        if process.poll() is not None:
                            stop_capture(); self.publish(CAPTURE_FAILED, track); retry = now + 2
                            continue
                        for key, _ in selector.select(timeout=.02):
                            try: chunk = os.read(key.fd, 65536)
                            except BlockingIOError: continue
                            if not chunk: continue
                            buffer.extend(chunk)
                        # Drop an obsolete backlog instead of animating drums late.
                        frame_bytes = HOP * 2 * 4
                        if len(buffer) > frame_bytes * 8:
                            del buffer[:(len(buffer) // frame_bytes - 4) * frame_bytes]
                        while len(buffer) >= frame_bytes:
                            values = analyzer.process(buffer[:frame_bytes])
                            del buffer[:frame_bytes]
                            now = time.monotonic()
                            if now - last_publish >= 1 / 60:
                                self.publish(READY, track, values); last_publish = now
                    else:
                        with self.condition: self.condition.wait(timeout=.1)
                except Exception:
                    stop_capture()
                    try: self.publish(CAPTURE_FAILED, track)
                    except OSError: pass
                    retry=time.monotonic()+2
                    with self.condition: self.condition.wait(timeout=1)
        except Exception:
            # An audio backend failure must not terminate the MPRIS/lyrics service.
            try: self.publish(CAPTURE_FAILED, self.track)
            except OSError: pass
        finally:
            stop_capture(); selector.close()

    def close(self):
        with self.condition:
            self.stopping = True; self.condition.notify()
        self.thread.join(timeout=3)
