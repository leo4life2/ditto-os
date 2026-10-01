"""Exercise the cancellation ordering on the real USB playback boundary."""
import json,sys,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from voice_transport import VoiceTransport
from validation_context import evidence_directory

class PausedFirstPacket:
    def __init__(self,device):
        self.device=device;self.calls=[];self.started=threading.Event();self.release=threading.Event();self.block=True
    def __getattr__(self,name):return getattr(self.device,name)
    def call(self,name,*a,**kw):
        self.calls.append(name)
        value=self.device.call(name,*a,**kw)
        if name=='audio.play' and self.block:
            self.block=False;self.started.set();assert self.release.wait(4)
        return value

with Device() as d:
    time.sleep(2);baseline=d.call('system.status');p=PausedFirstPacket(d);v=VoiceTransport(p);v.start()
    token=v.begin_speech();results=[];errors=[]
    def run(fn):
        try:results.append(fn())
        except Exception as e:errors.append(str(e))
    writer=threading.Thread(target=lambda:run(lambda:v.write_audio(bytes(640*12),token)))
    cancel=threading.Thread(target=lambda:run(v.interrupt))
    try:
        writer.start();assert p.started.wait(3),'Device did not accept first PCM packet'
        cancel.start();deadline=time.monotonic()+2
        while v._speech_id==token and time.monotonic()<deadline:time.sleep(.001)
        assert v._speech_id!=token
        p.release.set();writer.join(4);cancel.join(4)
        assert not writer.is_alive() and not cancel.is_alive() and not errors,errors
        assert False in results and p.calls.count('audio.play')==1,p.calls
        cut=max(i for i,n in enumerate(p.calls) if n=='audio.clear')
        assert 'audio.play' not in p.calls[cut+1:],p.calls
        before=len(p.calls);assert v.write_audio(bytes(640),token) is False
        assert len(p.calls)==before
        current=v.begin_speech();assert v.write_audio(bytes(640*5),current) is True
        v.interrupt();time.sleep(.15)
        s=d.call('system.status')
        for k in ('audio_rx_errors','audio_tx_errors','audio_overflows','display_errors'):assert s[k]==baseline[k],(k,s[k])
        report={'version':s['version'],'old_packets_after_clear':0,'late_response_packet_rejected':True,'new_response_accepted':True,'error_counter_increases':0,'test_audio':'Silent PCM; verifies transport ordering, not acoustic interruption quality'}
    finally:
        p.release.set()
        if writer.ident is not None:writer.join(4)
        if cancel.ident is not None:cancel.join(4)
        v.close();d.call('voice.state.set',{'state':'idle'});d.call('app.home')
(evidence_directory(ROOT)/'audio-interrupt-hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
