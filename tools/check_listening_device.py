"""Actual native USB recording drives the visual cue; synthetic touch stays local."""
from pathlib import Path
import sys,time,json,subprocess,urllib.request
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'test-results/status-compact-0.11.16'
out.mkdir(parents=True,exist_ok=True)
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=3))
def act(tool,**args):
 r=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(r,timeout=5))
def capture(name):
 r=urllib.request.Request('http://127.0.0.1:8781/capture',data=b'{}',headers={'Content-Type':'application/json'})
 urllib.request.urlopen(r,timeout=15).read();(out/name).write_bytes(urllib.request.urlopen('http://127.0.0.1:8781/frame.png').read())
initial=status()
# A preceding AudioQueue test may have disposed while macOS still holds the
# device's streaming alternate setting. Wait for that bounded transport tail.
until=time.monotonic()+6
while initial['usb_recording'] and time.monotonic()<until:
 time.sleep(.2);initial=status()
assert not initial['usb_recording'] and not initial['muted']
report={'before':initial};act('display.wake');act('app.home');capture('device-home.png')
with scratch_directory('listening-device',reserve_mib=24) as tmp:
 exe=Path(tmp)/'mic'
 subprocess.run(['c++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
 with (out/'device-audio.log').open('w') as log:
  recording=subprocess.Popen([str(exe),'/dev/null','20'],stdout=log,stderr=log)
  try:
   time.sleep(1);report['recording']=status();assert report['recording']['clip']=='Listening' and report['recording']['usb_recording']
   for i in range(45):
    travel=min(1,i/22);act('debug.touch',down=True,x=242+int(53*travel),y=320-int(53*travel));time.sleep(.08)
   report['held']=status();assert report['held']['touch_down'] and report['held']['clip']=='Listening' and report['held']['slime_stretch']>.05
   capture('device-listening-held.png')
   act('debug.touch',down=False,x=295,y=267)
   time.sleep(.5);report['released_while_capture']=status()
   act('audio.microphone.set',muted=True);time.sleep(.85);report['muted']=status();assert report['muted']['muted'] and report['muted']['clip']!='Listening'
   act('audio.microphone.set',muted=False);act('app.home');time.sleep(.5);report['unmuted']=status();assert report['unmuted']['clip']=='Listening'
   recording.wait(timeout=24);assert recording.returncode==0
   # Core Audio can retain the USB alternate setting briefly after Dispose.
   # Check the actual transport transition, then the visual transition.
   stopped=time.monotonic();closing=[]
   while time.monotonic()-stopped<6:
    snap=status();closing.append({'elapsed':time.monotonic()-stopped,'usb_recording':snap['usb_recording'],'clip':snap['clip']})
    if not snap['usb_recording']:break
    time.sleep(.15)
   report['close_poll']=closing
   time.sleep(.35);report['capture_closed']=status();assert not report['capture_closed']['usb_recording'] and report['capture_closed']['clip']=='Idle' 
   act('display.sleep');time.sleep(5.3);report['sleep']=status()
   assert report['sleep']['sleeping'] and report['sleep']['render_parked'] and report['sleep']['audio_sleeping']
   capture('device-sleep.png')
  finally:
   act('debug.touch',down=False,x=295,y=267);act('audio.microphone.set',muted=initial['muted']);act('display.wake');act('app.home')
   if recording.poll() is None:recording.wait(timeout=24)
   (out/'device-check.json').write_text(json.dumps(report,indent=2)+'\n')
 time.sleep(.8);report['after']=status();capture('device-home-final.png')
(out/'device-check.json').write_text(json.dumps(report,indent=2)+'\n')
errors={k:report['after'][k]-initial[k] for k in ('usb_audio_drops','audio_rx_errors','audio_tx_errors','audio_overflows','display_errors','scan_deadline_misses','te_timeouts')}
print(json.dumps({'version':report['after']['version'],'held_clip':report['held']['clip'],'held_stretch':report['held']['slime_stretch'],'sleep_parked':report['sleep']['render_parked'],'audio_parked':report['sleep']['audio_sleeping'],'errors':errors},indent=2));assert all(v==0 for v in errors.values())
