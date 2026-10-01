"""Concurrent playback cancellation, response ownership, and close behavior."""
import json,queue,sys,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from voice_transport import VoiceTransport
from validation_context import evidence_directory

class Board:
    generation=1;failure=None
    def __init__(self):
        self.calls=[];self.started=threading.Event();self.release=threading.Event();self.block=True
    def subscribe(self,*a,**kw):return queue.Queue()
    def unsubscribe(self,q):pass
    def call(self,name,*a,**kw):
        self.calls.append(name)
        if name=='audio.play' and self.block:
            self.block=False;self.started.set();assert self.release.wait(3),'Test writer was not released'
        return {}

def race(closing=False):
    b=Board();v=VoiceTransport(b);v.start();token=v.begin_speech();result=[];failures=[]
    def run(fn):
        try:result.append(fn())
        except Exception as e:failures.append(str(e))
    writer=threading.Thread(target=lambda:run(lambda:v.write_audio(bytes(640*5),token)))
    writer.start();assert b.started.wait(2)
    stop=threading.Thread(target=lambda:run(v.close if closing else v.interrupt));stop.start()
    deadline=time.monotonic()+2
    while v._speech_id==token and time.monotonic()<deadline:time.sleep(.001)
    assert v._speech_id!=token,'Cancellation did not invalidate the active response'
    b.release.set();writer.join(3);stop.join(3)
    assert not writer.is_alive() and not stop.is_alive() and not failures,failures
    assert False in result,'Canceled multi-chunk buffer was not stopped'
    assert b.calls.count('audio.play')==1,b.calls
    clear=max(i for i,x in enumerate(b.calls) if x=='audio.clear')
    assert 'audio.play' not in b.calls[clear+1:],b.calls
    if not closing:
        before=len(b.calls);assert v.write_audio(bytes(640),token) is False
        assert len(b.calls)==before,'Late callback from canceled response reached USB'
        fresh=v.begin_speech();assert fresh!=token
        assert v.write_audio(bytes(1280),fresh) is True
        assert b.calls.count('audio.play')==3
        v.close()
    return {'old_chunks_after_clear':0,'in_flight_buffer_canceled':True,'deadlock':False}

report={'interrupt':race(),'close':race(True),'late_provider_callback_rejected':True,'new_response_accepted':True}
(evidence_directory(ROOT)/'voice-interrupt-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
