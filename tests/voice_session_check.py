"""An old voice session must not send speech or actions to a rebooted device."""
import sys,queue,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'host'))
sys.path.insert(0,str(ROOT/'tools'))
from validation_context import evidence_directory
from voice_transport import VoiceTransport
from device import DeviceError
class Board:
    generation=1;failure=None
    def __init__(self):self.calls=[];self.subscribers=[]
    def subscribe(self,*a,**kw):q=queue.Queue();self.subscribers.append(q);return q
    def unsubscribe(self,q):self.subscribers.remove(q)
    def call(self,name,*args,**kwargs):
        self.calls.append(name)
        if name=='audio.capture.start' and len(self.calls)==1:self.generation+=1
        return {}
d=Board();v=VoiceTransport(d);v.start();d.generation+=1;before=len(d.calls)
checks=[lambda:v.read_audio(),lambda:v.write_audio(bytes(640)),lambda:v.skill('timer_start',{'seconds':60}),lambda:v.set_state('speaking'),lambda:v.interrupt(),lambda:v.begin_speech()]
for check in checks:
    try:check();raise AssertionError('Old voice session accepted after reboot')
    except DeviceError:pass
v.close();assert len(d.calls)==before and not d.subscribers
v.start();v.write_audio(bytes(640));v.close()
assert d.calls.count('audio.play')==1 and not d.subscribers
report={'old_session_audio_actions_and_state_rejected':len(checks),'close_does_not_modify_new_generation':True,'explicit_restart_works':True}
(evidence_directory(ROOT)/'voice-session-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
