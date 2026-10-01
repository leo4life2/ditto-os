"""Check actual rendered joints after contact and accepted remote feedback.

Synthetic touchscreen input stays local: this does not send Mac keystrokes.
Native USB audio is captured to /dev/null and never retained.
"""
import json,time,urllib.request,uuid,subprocess,math
from pathlib import Path
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'test-results/emote-recovery-0.11.28'
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=4))
def act(tool,**args):
 req=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=5))
def touch(down):act('debug.touch',down=down,x=242,y=280)
def capture(name):
 urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/capture',data=b'{}'),timeout=15).read()
 (out/name).write_bytes(urllib.request.urlopen('http://127.0.0.1:8781/frame.png',timeout=4).read())
client=str(uuid.uuid4());seq=0
def feedback(kind):
 global seq
 seq+=1
 r=act('input.remote',dictating=False,feedback={'client':client,'seq':seq,'kind':kind})
 assert r['feedback_ack']==seq
def hold(seconds):
 start=time.monotonic()
 while time.monotonic()-start<seconds:touch(True);time.sleep(.07)
def reaction(kind):
 feedback(kind);start=time.monotonic();rows=[];captured=False
 while time.monotonic()-start<2.8:
  s=status();rows.append({k:s[k] for k in ('uptime_s','clip','touch_down','usb_recording','behavior','emote_reaction_priority','emote_rig_active','emote_joint_motion','emote_seconds','face_oval','face_dizzy')})
  if not captured and time.monotonic()-start>.85:
   capture('device-'+kind+'.png');captured=True
  time.sleep(.07)
 assert any(s['emote_reaction_priority'] and s['emote_rig_active'] and s['emote_joint_motion']>.02 for s in rows),rows
 active=[s for s in rows if s['emote_reaction_priority']]
 assert active and all(s['face_oval']==0 and s['face_dizzy']==0 for s in active)
 result={'kind':kind,'clip':active[-1]['clip'],'peak_joint_motion':max(s['emote_joint_motion'] for s in active),'samples':rows}
 print('PASS:',kind,result['clip'],'joint motion',result['peak_joint_motion'],flush=True)
 return result
report={'before':status(),'reactions':[]};initial=report['before']
assert initial['version']=='0.11.29' and not initial['touch_down'] and not initial['usb_recording']
try:
 act('display.wake');act('app.home');time.sleep(.3)
 with scratch_directory('emote-release-mic',reserve_mib=12) as tmp:
  exe=tmp/'mic'
  subprocess.run(['c++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
  with (out/'release-mic.log').open('w') as log:
   mic=subprocess.Popen([str(exe),'/dev/null','9'],stdout=log,stderr=log)
   try:
    time.sleep(.8);hold(2.5)
    report['held']=status();assert report['held']['clip']=='Listening' and report['held']['touch_down'] and report['held']['usb_recording']
    touch(False);time.sleep(.09)
    report['reactions'].append(reaction('end'))
    assert any(s['usb_recording'] for s in report['reactions'][-1]['samples'])
    # A fresh held contact must interrupt the semantic gesture immediately.
    hold(.35);s=status();report['interrupted']=s
    assert not s['emote_reaction_priority']
    touch(False)
   finally:
    touch(False);mic.wait(timeout=12)
  until=time.monotonic()+7
  while status()['usb_recording'] and time.monotonic()<until:time.sleep(.15)
  assert not status()['usb_recording']
 act('app.home');time.sleep(.25)
 # Replay the short edges of a real double-tap, then accepted Enter feedback.
 touch(True);time.sleep(.06);touch(False);time.sleep(.11);touch(True);time.sleep(.06);touch(False);time.sleep(.09)
 report['reactions'].append(reaction('enter'))
 report['reactions'].append(reaction('undo'))
 # Real IMU motion legitimately wakes the board. Isolate the sleep-priority
 # check with a bounded stationary diagnostic feed, restored in finally.
 g=status()['motion_specific_g'];length=math.sqrt(sum(v*v for v in g));g=[v/length for v in g]
 def stationary(seconds):
  start=time.monotonic()
  while time.monotonic()-start<seconds:
   act('debug.motion',acceleration=g,gyro=[0,0,0]);time.sleep(.06)
 stationary(1)
 act('display.sleep');stationary(.2);feedback('undo');stationary(5.4)
 report['sleep']=status();assert report['sleep']['sleeping'] and report['sleep']['render_parked'] and not report['sleep']['emote_reaction_priority']
finally:
 touch(False);act('debug.motion',enabled=False);act('display.wake');act('app.home');time.sleep(.2)
 report['after']=status()
 report['errors']={k:report['after'][k]-initial[k] for k in ('usb_audio_drops','audio_rx_errors','audio_tx_errors','audio_overflows','display_errors','scan_deadline_misses','te_timeouts')}
 (out/'release-device.json').write_text(json.dumps(report,indent=2)+'\n')
print('Checked error deltas:',report['errors'],flush=True)
assert not any(report['errors'].values())
print('PASS: held USB Listening/release, real contact edges, joint motion, expression ownership, new touch, sleep; no Mac keys sent',flush=True)
