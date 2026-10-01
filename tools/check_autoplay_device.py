"""Exercise real firmware acknowledgements and renderer, without Mac keystrokes."""
import json,time,urllib.request,uuid,subprocess,sys
from pathlib import Path
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'test-results/status-spacing-0.11.27';out.mkdir(exist_ok=True)
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=4))
def act(tool,**args):
 r=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(r,timeout=4))
def capture(name):
 urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/capture',data=b'{}'),timeout=15).read()
 (out/name).write_bytes(urllib.request.urlopen('http://127.0.0.1:8781/frame.png',timeout=4).read())
client=str(uuid.uuid4());seq=0
allowed={'Yes','Celebrate','Giggle','Happy','Surprise','Wink','Thinking'}
def feedback(kind,number=None):
 global seq
 if number is None:seq+=1;number=seq
 r=act('input.remote',dictating=False,feedback={'client':client,'seq':number,'kind':kind})
 assert r['feedback_ack']==number
 return number
report={'before':status(),'reactions':[]}
assert not report['before']['usb_recording'] and not report['before']['touch_down']
try:
 act('display.wake');act('audio.microphone.set',muted=False);act('app.home')
 with scratch_directory('autoplay-mic',reserve_mib=12) as tmp:
  exe=tmp/'mic';subprocess.run(['c++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
  with (out/'autoplay-mic.log').open('w') as log:
   mic=subprocess.Popen([str(exe),'/dev/null','12'],stdout=log,stderr=log)
   try:
    time.sleep(1)
    for _ in range(4):act('input.remote',dictating=True);time.sleep(.1)
    assert status()['clip']=='Listening'
    event=feedback('end');time.sleep(.25);s=status()
    assert s['clip'] in allowed and s['usb_recording'] and not s['remote_dictating'],s['clip']
    report['release_during_usb_tail']=s
    count=s['auto_reaction_count'];feedback('end',event);time.sleep(.15)
    assert status()['auto_reaction_count']==count
    capture('device-reaction.png')
    act('input.remote',dictating=True);time.sleep(.16)
    assert status()['clip']=='Listening';report['new_recording_interrupts']=True
    act('input.remote',dictating=False)
   finally:mic.wait(timeout=15)
 time.sleep(5)
 prev=None
 for i in range(28):
  kind=('end','enter','undo')[i%3];feedback(kind);time.sleep(.17);s=status()
  assert s['clip'] in allowed and s['clip']!=prev,(s['clip'],prev)
  prev=s['clip'];report['reactions'].append({'kind':kind,'clip':s['clip']})
 seen={r['clip'] for r in report['reactions']};assert seen==allowed,seen
 # Touch cancels current gesture; feedback arriving during it must not replay later.
 act('debug.touch',down=True,x=245,y=320);feedback('enter');time.sleep(.12)
 assert status()['clip']=='Idle'
 act('debug.touch',down=False,x=245,y=320);time.sleep(.25)
 assert status()['clip']=='Idle'
 act('display.sleep');feedback('undo');time.sleep(2.5)
 assert status()['sleeping']
 act('display.wake');act('app.home');time.sleep(.3);assert status()['clip']=='Idle'
 # A real quiet interval: wait for a random wave, then let auto-sleep occur.
 t=time.monotonic();wave=None;last=None
 while time.monotonic()-t<66:
  s=status();last=s
  if s['clip']=='Wave' and wave is None:
   wave=s;capture('device-idle-wave.png');print('PASS: spontaneous idle wave',flush=True)
  if s['sleeping'] and s['render_parked']:
   break
  time.sleep(.3)
 assert wave is not None,'No idle wave before sleep'
 assert last['sleeping'] and last['render_parked'],'Idle wave postponed sleep'
 report['idle_wave']=wave;report['auto_sleep']=last
 capture('device-auto-sleep.png')
finally:
 act('debug.touch',down=False,x=245,y=320);act('input.remote',dictating=False)
 act('display.wake');act('audio.microphone.set',muted=report['before']['muted']);act('app.home')
 report['after']=status();(out/'autoplay-device.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: release during active USB tail; dedup; 7 reaction choices; recording/touch preemption; no deferred sleep feedback; idle wave; automatic sleep',flush=True)
