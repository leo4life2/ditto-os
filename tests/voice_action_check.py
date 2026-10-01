"""Response cancellation at the real Device.call serialization boundary.

No USB device or provider is opened. A fake wire supplies acknowledgments, but
VoiceTransport, ManagedDevice, skill dispatch and Device.call are production.
"""
import json,queue,sys,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device,DeviceError
from connection import ManagedDevice
from voice_transport import VoiceTransport,ResponseCancelled
from validation_context import evidence_directory

class ObservedLock:
    def __init__(self):self.lock=threading.Lock();self.waiting=threading.Event()
    def __enter__(self):self.waiting.set();self.lock.acquire();return self
    def __exit__(self,*a):self.lock.release()

class Wire:
    def __init__(self,device):
        self.device=device;self.sent=[];self.hold=None;self.held=None;self.arrived=threading.Event()
    def write(self,packet):
        request=json.loads(packet);self.sent.append(request)
        if request['tool']==self.hold:
            self.held=request;self.arrived.set()
        else:self.reply(request)
        return len(packet)
    def reply(self,request):
        self.device.pending[request['id']].put({'ok':True,'result':{'observed':request['tool']}})

def device():
    d=Device.__new__(Device)
    d.closed=False;d.failure=None;d.boot_count=1;d.pending={};d.logs=[];d.subscribers=[]
    d.lock=ObservedLock();d.serial=Wire(d)
    return d

def setup(managed=False):
    d=device();target=d
    if managed:
        target=ManagedDevice.__new__(ManagedDevice)
        target._device=d;target._boot_count=d.boot_count;target._lock=threading.Lock()
        target.generation=1;target.subscribers=[]
    v=VoiceTransport(target);v.start();token=v.begin_speech()
    return d,target,v,token

def spawn(fn):
    result=[];errors=[]
    def run():
        try:result.append(fn())
        except Exception as e:errors.append(e)
    thread=threading.Thread(target=run,daemon=True);thread.start()
    return thread,result,errors

def join(worker):
    worker[0].join(3);assert not worker[0].is_alive(),'Deadlock or unexpected device-reply wait'

def changed(v,token):
    deadline=time.monotonic()+2
    while v._speech_id==token and time.monotonic()<deadline:time.sleep(.001)
    assert v._speech_id!=token,'Cancellation waited for a queued command'

def rejected(fn,exception=DeviceError):
    try:fn()
    except exception:return
    raise AssertionError('Stale response was accepted')

def request(v,token,kind):
    if kind=='action':return lambda:v.skill('timer_start',{'seconds':60},speech_id=token)
    if kind=='state':return lambda:v.set_state('speaking','Old caption',speech_id=token)
    return lambda:v.write_audio(bytes(640),speech_id=token)

def queued(kind,operation):
    d,target,v,token=setup(operation=='reconnect')
    d.lock.lock.acquire();d.lock.waiting.clear();before=len(d.serial.sent)
    worker=spawn(request(v,token,kind));assert d.lock.waiting.wait(2)
    cancel=None
    try:
        if operation=='reconnect':
            replacement=device()
            with target._lock:target._device=replacement;target.generation+=1
        else:
            cancel=spawn(v.close if operation=='close' else v.interrupt)
            changed(v,token)
    finally:d.lock.lock.release()
    join(worker)
    if cancel:join(cancel);assert not cancel[2],cancel[2]
    attempted='timer.start' if kind=='action' else 'voice.state.set' if kind=='state' else 'audio.play'
    sends=[r for r in d.serial.sent[before:] if r['tool']==attempted]
    if kind=='state':sends=[r for r in sends if r['arguments'].get('state')=='speaking']
    assert not sends,'Canceled command reached the wire'
    if kind=='audio' and operation!='reconnect':assert worker[1]==[False] and not worker[2]
    else:assert len(worker[2])==1 and isinstance(worker[2][0],DeviceError),worker
    assert not d.pending,'Canceled request leaked into pending replies'
    if operation=='reconnect':assert not replacement.serial.sent
    v.close()

def late_and_fresh():
    d,_,v,token=setup();fresh=v.interrupt();before=len(d.serial.sent)
    rejected(request(v,token,'action'),ResponseCancelled)
    rejected(request(v,token,'state'),ResponseCancelled)
    assert v.write_audio(bytes(640),token) is False
    assert len(d.serial.sent)==before
    assert v.skill('timer_start',{'seconds':2},speech_id=fresh)=={'observed':'timer.start'}
    assert v.set_state('speaking',speech_id=fresh)=={'observed':'voice.state.set'}
    assert v.write_audio(bytes(640),fresh) is True
    rejected(lambda:v.skill('unknown',{},speech_id=fresh),ValueError)
    v.close();before=len(d.serial.sent)
    rejected(request(v,fresh,'action'));rejected(request(v,fresh,'state'))
    assert len(d.serial.sent)==before
    v.start();new=v.begin_speech();assert new!=fresh
    rejected(request(v,fresh,'action'),ResponseCancelled)
    assert v.skill('timer_cancel',{},speech_id=new)=={'observed':'timer.cancel'}
    # Local synchronous actions still work, but cannot bypass closed transport.
    assert v.skill('system_status',{})=={'observed':'system.status'}
    v.close();rejected(lambda:v.skill('timer_start',{'seconds':2}))

def accepted_action():
    d,_,v,token=setup();d.serial.hold='timer.start'
    worker=spawn(request(v,token,'action'));assert d.serial.arrived.wait(2)
    cancel=spawn(v.interrupt)
    try:
        join(cancel);assert not cancel[2],cancel[2]
        assert worker[0].is_alive(),'Fixture did not withhold the action acknowledgment'
        assert d.serial.sent[-1]['tool']=='audio.clear'
    finally:d.serial.reply(d.serial.held)
    join(worker);assert not worker[2] and worker[1]==[{'observed':'timer.start'}]
    assert sum(r['tool']=='timer.start' for r in d.serial.sent)==1
    v.close();assert not d.pending

cases=[]
for repeat in range(10):
    for kind in ('action','state','audio'):
        for operation in ('interrupt','close','reconnect'):
            queued(kind,operation)
            cases.append(f'{kind}/{operation}')
late_and_fresh();accepted_action()
report={'passed':True,'queued_dispatch_races':len(cases),'race_kinds':sorted(set(cases)),
        'late_callbacks_rejected':True,'explicit_new_response_and_restart_work':True,
        'already_dispatched_action_keeps_observed_result':True,
        'slow_action_reply_does_not_block_interrupt':True,'pending_request_leaks':0,
        'scope':'Production host dispatch with fake wire acknowledgments; no hardware or cloud conversation claim'}
(evidence_directory(ROOT)/'voice-action-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
