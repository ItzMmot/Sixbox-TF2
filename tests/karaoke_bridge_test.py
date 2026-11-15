import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import types
import unittest
from unittest.mock import patch
import urllib.error
import email.message

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('karaoke_bridge',ROOT/'scripts/karaoke/bridge.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)


def track(title='Example'):
    return dict(title=title,artist='Artist',album='Album',duration=60,url='',bus='org.mpris.MediaPlayer2.spotify',
                id='/track/1',position=2,playing=True,can_play=True,can_seek=True,can_repeat=True,
                can_next=True,can_previous=True,repeat='None',rate=1)

class Media:
    def __init__(self):self.track=track();self.calls=[];self.polls=0
    def poll(self,_):self.polls+=1;return dict(self.track)
    def control(self,command,_):self.calls.append(command);return ''

class ProviderFixture:
    def resolve(self,track,settings,cancel=None):
        return dict(lines=[dict(text=track['title'],start=0,end=60,segments=[])],provider='fixture',synced=True,attribution=[],error='')

def status(directory):
    value=b.read_json(Path(directory)/'status.json')
    if value.get('lyrics_revision'):
        lyrics=b.read_json(Path(directory)/'lyrics.json')
        assert lyrics['revision']==value['lyrics_revision']
        value['lyrics']=lyrics['payload']
    return value

class Tests(unittest.TestCase):
    def test_lrc_offsets_duplicate_tags_unicode_words(self):
        lines=b.parse_lrc('[offset:+100]\n[00:01.50][00:04.00]Пример\n[00:06.0]<00:06.0>one <00:07.0>two',10)
        self.assertEqual([v['start'] for v in lines],[1.6,4.1,6.1])
        self.assertEqual(lines[0]['end'],4.1)
        self.assertEqual(lines[2]['text'],'one two')
        self.assertEqual(lines[2]['segments'][0]['end'],7.1)
        self.assertEqual(lines[2]['segments'][-1]['end'],10)

    def test_spicy_syllables_and_conditional_attribution(self):
        body={'Type':'Syllable','source':'spicy_lyrics','UploadAttribution':{'Uploader':{'username':'Ada','url':'https://spicylyrics.org/uid/1'}},
              'Content':[{'Type':'Vocal','StartTime':1,'EndTime':3,'Lead':{'Syllables':[
                  {'Text':'exam','StartTime':1,'EndTime':2,'IsPartOfWord':True},
                  {'Text':'ple','StartTime':2,'EndTime':3,'IsPartOfWord':False}]}}]}
        result=b.parse_spicy({'Body':body},4)
        self.assertEqual(result['lines'][0]['text'],'example')
        self.assertEqual(len(result['attribution']),2)
        body['source']='apple_music';body.pop('UploadAttribution')
        result=b.parse_spicy({'Body':body},4)
        self.assertEqual(result['attribution'],[{'name':'Apple Music','url':'https://spicylyrics.org'}])

    def test_spicy_infers_group_times_from_real_syllables(self):
        result=b.parse_spicy({'Body':{'Type':'Syllable','Content':[{'Lead':{'Syllables':[
            {'Text':'hello','StartTime':5,'EndTime':7,'IsPartOfWord':False}]}}]}},60)
        self.assertEqual(result['lines'][0]['start'],5)
        self.assertEqual(result['lines'][0]['end'],7)

    def test_cancel_during_throttle_prevents_network(self):
        cancel=threading.Event();calls=[]
        provider=b.Providers(fetch=lambda *args:calls.append(args),clock=lambda:5,sleep=lambda _:cancel.set())
        provider.last['LRCLIB']=5;provider.cancel=cancel
        with self.assertRaises(b.ProviderError):provider.get('LRCLIB','https://example.invalid')
        self.assertFalse(calls)

    def test_spicy_static_has_no_fabricated_timing(self):
        result=b.parse_spicy({'Body':{'Type':'Static','Lines':[{'Text':'one'},{'Text':'two'}]}},60)
        self.assertFalse(result['synced']);self.assertEqual(len(result['lines']),2)
        self.assertEqual(result['lines'][0]['end'],60)

    def test_fallback_order_and_no_lyrics_error(self):
        provider=b.Providers(sleep=lambda _:None)
        calls=[]
        def missing(name):
            def call(*_):calls.append(name);raise b.ProviderError(name+': denied')
            return call
        provider.spicy=missing('spicy');provider.lrclib=missing('lrclib');provider.musixmatch=missing('musixmatch')
        result=provider.resolve(track(),{'provider':'1'})
        self.assertEqual(calls,['spicy','lrclib','musixmatch']);self.assertIn('denied',result['error'])
        calls.clear();provider.resolve(track(),{'provider':0});self.assertEqual(calls,['lrclib','musixmatch'])

    def test_lrclib_400_uses_search_and_avoids_wrong_duration(self):
        def fetch(provider,url,headers):
            if '/get?' in url:raise b.ProviderError('LRCLIB: HTTP 400')
            return [dict(trackName='Example',duration=99,syncedLyrics='[00:01]wrong'),
                    dict(trackName='Example',duration=60,syncedLyrics='[00:01]right')]
        result=b.Providers(fetch,sleep=lambda _:None).lrclib(track(),{})
        self.assertEqual(result['lines'][0]['text'],'right')

    def test_throttle_and_retry_after_without_key_leak(self):
        headers=email.message.Message();headers['Retry-After']='120'
        error=urllib.error.HTTPError('https://example/?apikey=private-test-key',429,'rate',headers,None)
        provider=b.Providers(clock=lambda:5,sleep=lambda _:None)
        with patch.object(b.urllib.request,'urlopen',side_effect=error) as network:
            with self.assertRaises(b.ProviderError) as first:provider.get('Musixmatch','https://example/?apikey=private-test-key')
            with self.assertRaises(b.ProviderError):provider.get('Musixmatch','https://example/?apikey=private-test-key')
            self.assertEqual(network.call_count,1)
            self.assertNotIn('private-test-key',str(first.exception))
        self.assertEqual(provider.cooldowns['Musixmatch'],125)

    def test_idle_bridge_has_no_poll_or_network(self):
        with tempfile.TemporaryDirectory() as directory:
            media=Media();bridge=b.Bridge(directory,media,ProviderFixture())
            try:
                bridge.step();path=Path(directory)/'status.json';stamp=path.stat().st_mtime_ns
                bridge.step();self.assertEqual(media.polls,0);self.assertEqual(path.stat().st_mtime_ns,stamp)
            finally:bridge.close()

    def write_request(self,directory,**extra):
        data=dict(version='1',updated=str(time.time()),enabled='true',epoch='test',settings={'provider':'0'})
        data.update(extra);b.atomic_json(Path(directory)/'request.json',data)

    def test_boost_json_strings_and_no_replayed_next_after_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            media=Media();bridge=b.Bridge(directory,media,ProviderFixture())
            self.write_request(directory,command=dict(sequence='1',created=time.time(),action='next'))
            try:
                bridge.step();bridge.future.result(timeout=1);bridge.step()
                state=status(directory)
                self.assertEqual(state['ack'],1);self.assertEqual(len(media.calls),1)
                self.assertEqual(state['lyrics']['lines'][0]['text'],'Example')
            finally:bridge.close()
            second=b.Bridge(directory,media,ProviderFixture())
            try:second.step();self.assertEqual(len(media.calls),1)
            finally:second.close()

    def test_expired_command_is_acknowledged_not_executed(self):
        with tempfile.TemporaryDirectory() as directory:
            media=Media();bridge=b.Bridge(directory,media,ProviderFixture())
            self.write_request(directory,command=dict(sequence='9',created=time.time()-20,action='next'))
            try:
                bridge.step();state=status(directory)
                self.assertEqual(state['ack'],9);self.assertFalse(media.calls);self.assertIn('expired',state['control_error'])
            finally:bridge.close()

    def test_stale_provider_result_does_not_replace_new_song(self):
        started=threading.Event();finish=threading.Event()
        class Slow(ProviderFixture):
            def resolve(self,track,settings,cancel=None):
                if track['title']=='Example':started.set();finish.wait(1)
                return super().resolve(track,settings,cancel)
        with tempfile.TemporaryDirectory() as directory:
            media=Media();bridge=b.Bridge(directory,media,Slow());self.write_request(directory)
            try:
                bridge.step();self.assertTrue(started.wait(1));media.track['title']='New';bridge.step()
                finish.set();bridge.future.result(timeout=1);bridge.step();bridge.future.result(timeout=1);bridge.step()
                state=status(directory)
                self.assertEqual(state['lyrics']['lines'][0]['text'],'New')
            finally:finish.set();bridge.close()

    def test_mpris_seek_repeat_capabilities_and_stale_identity(self):
        calls=[]
        class Player:
            def SetPosition(self,*args,**kwargs):calls.append(('seek',args))
            def Next(self,**kwargs):calls.append(('next',))
        class Props:
            def Set(self,*args,**kwargs):calls.append(('repeat',args))
        media=b.Mpris.__new__(b.Mpris);media.player=Player();media.properties=Props()
        media.dbus=types.SimpleNamespace(ObjectPath=str,Int64=int,String=str)
        state=track();state['identity']='current'
        command=dict(bus=state['bus'],track=state['id'],identity='old',action='next')
        self.assertIn('changed',media.control(command,state));self.assertFalse(calls)
        command.update(identity='current',action='seek',value=500)
        self.assertEqual(media.control(command,state),'');self.assertEqual(calls[0][1][1],60_000_000)
        command.update(action='repeat',value='Track');self.assertEqual(media.control(command,state),'')
        state['can_repeat']=False;self.assertIn('does not support',media.control(command,state))

if __name__=='__main__':unittest.main()
