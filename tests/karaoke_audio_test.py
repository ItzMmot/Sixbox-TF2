"""Selected stream ownership, PCM analysis and the fixed binary wire format."""
import importlib.util
from pathlib import Path
import unittest
import tempfile
import time
from unittest.mock import patch
import numpy as np

spec=importlib.util.spec_from_file_location('sixbox_audio',Path(__file__).resolve().parents[1]/'scripts/karaoke/audio.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)

def stream(index, app, binary, pid, title='(null)'):
    return {'index':index,'sink':7,'properties':{'application.name':app,'application.process.binary':binary,
        'application.process.id':str(pid),'media.name':title}}

class Tests(unittest.TestCase):
    def test_spotify_never_selects_game_other_player_or_browser(self):
        track={'bus':'org.mpris.MediaPlayer2.spotify','pid':20,'playing':True,'title':'Song'}
        inputs=[stream(1,'TF2','tf_win64.exe',1),stream(2,'Firefox','firefox',2,'Song'),
                stream(3,'VLC','vlc',3,'Song'),stream(4,'Spotify','spotify',20)]
        selected,flags=a.select_stream(track,inputs)
        self.assertEqual((selected['index'],flags),(4,a.READY))
        self.assertEqual(a.select_stream(track,inputs[:3]),(None,a.NO_STREAM))
        self.assertEqual(a.select_stream(track,inputs+[stream(5,'Spotify','spotify',20)]),(None,a.AMBIGUOUS))

    def test_another_instance_of_same_app_is_not_the_selected_player(self):
        track={'bus':'org.mpris.MediaPlayer2.spotify','pid':10,'playing':True,'title':'Song'}
        inputs=[stream(1,'Spotify','spotify',20),stream(2,'Spotify','spotify',30)]
        selected,flags=a.select_stream(track,inputs,related=lambda child,parent:int(child)==20 and int(parent)==10)
        self.assertEqual((selected['index'],flags),(1,a.READY))
        self.assertEqual(a.select_stream(track,inputs,related=lambda *_:False),(None,a.NO_STREAM))

    def test_native_pipewire_flatpak_metadata_joins_exact_client(self):
        track={'bus':'org.mpris.MediaPlayer2.spotify','pid':400,'playing':True,'title':'Song'}
        client={'index':19,'properties':{'application.name':'Spotify','application.process.binary':'spotify',
            'application.process.id':'4','pipewire.protocol':'protocol-native','pipewire.sec.pid':'500',
            'pipewire.access.portal.app_id':'com.spotify.Client','pipewire.access.portal.instance_id':'71'}}
        inputs=[{'index':9,'client':'19','sink':7,'properties':{'application.name':'Spotify'}}]
        merged=a.enrich_streams(inputs,[client])
        self.assertEqual(merged[0]['properties']['pipewire.sec.pid'],'500')
        groups={'400':'0::/user.slice/app-flatpak-com.spotify.Client-71.scope',
                '500':'0::/user.slice/app-flatpak-com.spotify.Client-71.scope'}
        self.assertTrue(a.flatpak_owner(merged[0]['properties'],'400',read=lambda pid:groups[str(pid)]))
        resolver=a.flatpak_owner
        with patch.object(a,'flatpak_owner',side_effect=lambda props,pid:resolver(props,pid,read=lambda q:groups[str(q)])):
            selected,flags=a.select_stream(track,merged,related=lambda *_:False)
            self.assertEqual((selected['index'],flags),(9,a.READY))
        groups['500']='0::/user.slice/app-flatpak-com.spotify.Client-72.scope'
        self.assertFalse(a.flatpak_owner(merged[0]['properties'],'400',read=lambda pid:groups[str(pid)]))
        self.assertFalse(a.flatpak_owner(client['properties'],'400',read=lambda pid:'0::/user.slice/app-flatpak-com.discordapp.Discord-71.scope'))

    def test_pulse_server_security_pid_does_not_replace_proven_application_pid(self):
        track={'bus':'org.mpris.MediaPlayer2.spotify','pid':20,'playing':True,'title':'Song'}
        item=stream(7,'Spotify','spotify',20)
        item['properties'].update({'pipewire.protocol':'protocol-native','pipewire.sec.pid':'99'})
        selected,flags=a.select_stream(track,[item],related=lambda child,owner:str(child)==str(owner))
        self.assertEqual((selected['index'],flags),(7,a.READY))

    def test_browser_requires_its_own_label_even_with_matching_pid(self):
        track={'bus':'org.mpris.MediaPlayer2.firefox.instance1','pid':10,'playing':True,'title':'My Song','artist':'Artist'}
        self.assertEqual(a.select_stream(track,[stream(1,'Firefox','firefox',10)]),(None,a.AMBIGUOUS))
        inputs=[stream(1,'Firefox','firefox',10,'Game Sound'),stream(2,'Firefox','firefox',10,'Artist - My Song'),stream(3,'VLC','vlc',10,'My Song')]
        selected,flags=a.select_stream(track,inputs)
        self.assertEqual((selected['index'],flags),(2,a.READY))
        inputs.append(stream(4,'Firefox','firefox',10,'My Song'))
        self.assertEqual(a.select_stream(track,inputs),(None,a.AMBIGUOUS))
        track['playing']=False;self.assertEqual(a.select_stream(track,inputs),(None,a.PAUSED))

    def test_dedicated_youtube_app_is_not_a_shared_browser(self):
        track={'bus':'org.mpris.MediaPlayer2.youtube-music','pid':2,'playing':True,'title':'Song'}
        inputs=[stream(1,'Firefox','firefox',1),stream(2,'YouTube Music','electron',2)]
        selected,flags=a.select_stream(track,inputs)
        self.assertEqual((selected['index'],flags),(2,a.READY))

    def test_ambiguous_browser_never_launches_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(a.AudioCapture,'query',return_value=[stream(1,'Firefox','firefox',10)]), \
                 patch.object(a.shutil,'which',return_value='/mock/tool'),patch.object(a.subprocess,'Popen') as spawn:
                capture=a.AudioCapture(directory)
                try:
                    capture.configure({'identity':'1234567812345678','bus':'org.mpris.MediaPlayer2.firefox','pid':10,'title':'Song','playing':True},True)
                    end=time.monotonic()+1
                    while time.monotonic()<end:
                        path=Path(directory)/'audio.bin'
                        if path.exists() and a.PACKET.unpack(path.read_bytes())[1]==a.AMBIGUOUS:break
                        time.sleep(.01)
                    else:self.fail('Audio worker did not report ambiguous ownership')
                    spawn.assert_not_called()
                finally:capture.close()
                self.assertFalse(capture.thread.is_alive())

    def test_silence_stays_silent_and_packets_match_native_layout(self):
        analyzer=a.Analyzer();values=analyzer.process(np.zeros((a.HOP,2),dtype='<f4').tobytes())
        self.assertEqual(len(values),262);self.assertFalse(any(values))
        packet=a.PACKET.pack(a.MAGIC,a.READY,7,0,1,*values)
        self.assertEqual(len(packet),1080)
        self.assertEqual(a.PACKET.unpack(packet)[:3],(a.MAGIC,a.READY,7))

    def analyze(self,frequency,antiphase=False):
        analyzer=a.Analyzer()
        for index in range(20):
            x=np.arange(a.HOP)+index*a.HOP
            tone=(.4*np.sin(2*np.pi*frequency*x/a.RATE)).astype('<f4')
            values=analyzer.process(np.column_stack((tone,-tone if antiphase else tone)).astype('<f4').tobytes())
        return analyzer,values

    def test_fft_separates_bass_and_hats_and_preserves_antiphase_stereo(self):
        _,bass=self.analyze(94,True);_,hats=self.analyze(9000)
        self.assertGreater(bass[2],.2);self.assertLess(bass[4],.01)
        self.assertGreater(hats[4],.2);self.assertLess(hats[2],.01)
        self.assertGreater(max(bass[6:70]),.5)
        np.testing.assert_allclose(bass[70:166],-np.array(bass[166:262]),atol=1e-5)

    def test_drum_transients_repeat_without_bpm_lock_and_decay(self):
        analyzer=a.Analyzer();hits=[]
        rng=np.random.default_rng(1)
        for index in range(120):
            block=np.zeros((a.HOP,2),dtype='<f4')
            if index in (8,15,21,28,35,43,50,57):block[:128]=rng.uniform(-.7,.7,(128,2))
            values=analyzer.process(block.tobytes())
            if values[5]>.3:hits.append(index)
        for beat in (8,15,21,28,35,43,50,57):self.assertTrue(any(beat<=hit<=beat+3 for hit in hits))
        self.assertLess(values[5],.001);self.assertFalse(any(values[6:70]))

if __name__=='__main__':unittest.main()
