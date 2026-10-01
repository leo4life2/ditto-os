"""Exercise full-screen listening, touch, release, reaction and standby locally."""
from pathlib import Path
import sys,json,time,urllib.request,threading,subprocess,uuid,math
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=4))
def act(tool,**arguments):
 r=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':arguments}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(r,timeout=6))
def capture(name):
 urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/capture',data=b'{}'),timeout=15).read()
 (out/(name+'.png')).write_bytes(urllib.request.urlopen('http://127.0.0.1:8781/frame.png',timeout=4).read())
initial=status();assert not initial['touch_down'] and not initial['usb_recording'] and not initial['remote_dictating']
errors=('usb_audio_drops','audio_rx_errors','audio_tx_errors','audio_overflows','display_errors','scan_deadline_misses','te_timeouts')
report={'before':initial};target=[False,242,320];stop=threading.Event();fail=[];recording=None
specific=initial['motion_specific_g'];length=math.sqrt(sum(v*v for v in specific));specific=[v/length for v in specific]
def keep_input():
 while not stop.is_set():
  try:
   down,x,y=target;act('debug.motion',acceleration=specific,gyro=[0,0,0]);act('debug.touch',down=down,x=x,y=y)
  except Exception as e:fail.append(str(e));return
  stop.wait(.05)
try:
 act('display.wake');act('app.home');worker=threading.Thread(target=keep_input);worker.start()
 with scratch_directory('glow-final',reserve_mib=24) as tmp:
  exe=Path(tmp)/'mic';subprocess.run(['c++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
  with (out/'audio.log').open('w') as log:
   recording=subprocess.Popen([str(exe),'/dev/null','15'],stdout=log,stderr=log)
   time.sleep(1);report['listening']=status();assert report['listening']['clip']=='Listening';capture('listening')
   target[:]=[True,242,320];time.sleep(.25)
   for i in range(25):target[:]=[True,242-int(37*i/24),320-int(240*i/24)];time.sleep(.06)
   time.sleep(.3);report['stretched']=status();assert report['stretched']['clip']=='Listening' and report['stretched']['touch_down'] and report['stretched']['slime_stretch']>.03
   capture('top-stretch')
   motion_start=status()
   for i in range(36):
    target[:]=[True,205+int(35*math.sin(i*.35)),100+int(25*math.cos(i*.35))];time.sleep(.08)
   motion_end=status()
   report['drag_fps']=(motion_end['presented_frames']-motion_start['presented_frames'])/(motion_end['uptime_s']-motion_start['uptime_s'])
   target[:]=[False,205,80]
   recording.wait(timeout=18);assert recording.returncode==0
  until=time.monotonic()+7
  while status()['usb_recording'] and time.monotonic()<until:time.sleep(.2)
  assert not status()['usb_recording']
  act('input.remote',dictating=False,feedback={'client':str(uuid.uuid4()),'seq':1,'kind':'end'})
  time.sleep(.9);report['reaction']=status();assert report['reaction']['emote_reaction_priority'] and report['reaction']['emote_joint_motion']>.01
  capture('reaction');act('display.sleep');time.sleep(5.8);report['sleep']=status()
  assert report['sleep']['sleeping'] and report['sleep']['render_parked'] and report['sleep']['effective_brightness']==10 and report['sleep']['audio_sleeping'];capture('sleep')
  target[:]=[True,205,280];time.sleep(.7);report['wake']=status();assert not report['wake']['sleeping'] and report['wake']['effective_brightness']==initial['brightness'];target[0]=False
finally:
 stop.set()
 if 'worker' in globals():worker.join(timeout=7)
 act('debug.touch',down=False,x=205,y=280);act('debug.motion',enabled=False);act('display.wake');act('app.home')
 if recording and recording.poll() is None:recording.wait(timeout=20)
 time.sleep(.4);report['after']=status();report['errors']={k:report['after'][k]-initial[k] for k in errors};report['input_errors']=fail
 (out/'device-regression.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'version':report['after']['version'],'stretched':report.get('stretched',{}).get('slime_stretch'),'reaction':report.get('reaction',{}).get('clip'),'sleep_parked':report.get('sleep',{}).get('render_parked'),'errors':report['errors'],'input_errors':fail},indent=2))
assert not fail and not any(report['errors'].values())
