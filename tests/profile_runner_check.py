"""Offline orchestration checks. Never creates a serial connection."""
import contextlib,io,json,runpy,sys,tempfile,time
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
import device,voice_transport,build_provenance
from validation_context import evidence_directory
real_sleep=time.sleep
instances=[]

class Board:
    mode='ok'
    def __init__(self):self.failure=None;self.boot_count=1;self.logs=[];self.calls=[];self.tick=0;self.closed=False;instances.append(self)
    def __enter__(self):return self
    def __exit__(self,*a):self.closed=True
    def sync_clock(self):pass
    def call(self,name,args=None):
        self.calls.append((name,args));self.tick+=1
        if name=='debug.touch' and args['down'] and self.mode=='touch-fault':raise RuntimeError('Injected touch transport fault')
        if name=='debug.power':return {'uptime_us':self.tick*100000,'tasks':[{'id':1,'name':'mock','runtime_us':self.tick*50000}]}
        if name!='system.status':return {}
        s={k:0 for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','scan_deadline_misses','te_timeouts']}
        s.update({k:1 for k in ['frame_ms','frame_p95_ms','physics_ms','anim_ms','bin_ms','draw_ms','raster0_ms','raster1_ms','compose_ms','upload_ms','copy_ms','diff_ms','dma_wait_ms','present_ms','internal_free']})
        s.update(version='mock',uptime_s=self.tick/10,frames=self.tick*3,presented_frames=self.tick*3,
                 frames_within25=self.tick*2,missed_frames=self.tick,audio_output_blocks=self.tick*5)
        return s

class Voice:
    def __init__(self,d):self.device=d;self.closed=False;self.writes=0
    def start(self):pass
    def read_audio(self):real_sleep(.01);return b'' if self.device.mode=='missing-mic' else bytes(640)
    def write_audio(self,pcm):
        self.writes+=1
        if self.device.mode=='audio-pause' and self.writes==10:real_sleep(.120)
        return True
    def close(self):self.closed=True

checks={}
with tempfile.TemporaryDirectory(prefix='scout-profile-runner-') as temp:
    for mode in ['ok','touch-fault','missing-mic','audio-pause']:
        Board.mode=mode;out=Path(temp)/(mode+'.json');error=None
        args=['frame_profile.py',str(out),'--idle-seconds','.03','--drag-seconds','.03','--mixed-seconds','1']
        with patch.object(device,'Device',Board),patch.object(voice_transport,'VoiceTransport',Voice), \
             patch.object(build_provenance,'verify',lambda:{'version':'mock','artifacts':{'build/scout_os.bin':'mock'}}), \
             patch.object(time,'sleep',lambda s:None if s>=1 else real_sleep(s)),patch.object(sys,'argv',args), \
             contextlib.redirect_stdout(io.StringIO()):
            try:runpy.run_path(str(ROOT/'tests/frame_profile.py'),run_name='__main__')
            except Exception as e:error=e
        report=json.loads(out.read_text());board=instances[-1]
        assert board.closed
        assert ('debug.touch',{'down':False,'x':205,'y':290}) in board.calls
        assert board.calls[-1][0]=='app.home'
        if mode=='ok':
            assert error is None and not report['errors'] and report['run_complete'],(error,report.get('audio_transport'))
            assert set(report['phases'])=={'idle','drag','mixed'}
            assert not report['phases']['mixed']['frame_budget']['headroom_thresholds_met']
        elif mode=='touch-fault':
            assert error and not report['run_complete'] and 'mixed' not in report['phases']
            assert any('Injected touch' in str(e) for e in report['errors'])
        elif mode=='audio-pause':
            assert error and report['run_complete']
            assert report['audio_transport']['speaker_packets_per_second']>=49
            assert not report['audio_transport']['delivery_timing_met']
            assert any('PCM delivery timing' in str(e) for e in report['errors'])
        else:
            assert error and any('Microphone transport' in str(e) for e in report['errors'])
        checks[mode]=True
    # Existing evidence is refused before constructing even the fake board.
    before=len(instances)
    with patch.object(device,'Device',Board),patch.object(sys,'argv',['frame_profile.py',str(out)]),contextlib.redirect_stderr(io.StringIO()):
        try:runpy.run_path(str(ROOT/'tests/frame_profile.py'),run_name='__main__')
        except SystemExit as e:assert e.code==2
        else:raise AssertionError('Existing evidence was overwritten')
    assert len(instances)==before
    checks['overwrite_refused_before_device_open']=True
report={'passed':True,'checks':checks,'scope':'Profile orchestration with fake device/voice; no hardware, timing or audio qualification'}
(evidence_directory(ROOT)/'profile-runner-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
