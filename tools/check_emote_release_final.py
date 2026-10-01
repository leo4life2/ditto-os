"""Final hardware reaction/expiry check, without Mac keyboard events."""
import json,time,urllib.request,uuid,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'test-results/emote-recovery-0.11.28'
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=4))
def act(tool,**args):
 req=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=5))
def touch(down):act('debug.touch',down=down,x=242,y=280)
errors=('usb_audio_drops','audio_rx_errors','audio_tx_errors','audio_overflows','display_errors','scan_deadline_misses','te_timeouts')
initial=status();assert initial['version'] in ('0.11.29','0.11.30') and not initial['usb_recording'] and not initial['touch_down']
client=str(uuid.uuid4());report={'before':initial,'cases':[]}
try:
 act('display.wake');act('app.home');time.sleep(.25)
 g=status()['motion_specific_g'];n=math.sqrt(sum(v*v for v in g));g=[v/n for v in g]
 for seq,kind in enumerate(('end','enter','undo'),1):
  if kind=='end':
   start=time.monotonic()
   while time.monotonic()-start<1.5:touch(True);time.sleep(.07)
   touch(False)
  elif kind=='enter':
   touch(True);time.sleep(.06);touch(False);time.sleep(.1);touch(True);time.sleep(.06);touch(False)
  time.sleep(.08)
  act('input.remote',dictating=False,feedback={'client':client,'seq':seq,'kind':kind})
  rows=[];start=time.monotonic();expired=False
  case={'kind':kind,'samples':rows,'expired_during_motion':False};report['cases'].append(case)
  while time.monotonic()-start<6:
   # Keep the scheduler's motion-busy path active across clip expiration.
   act('debug.motion',acceleration=g,gyro=[0,0,.8*math.sin((time.monotonic()-start)*5)])
   s=status();rows.append({k:s[k] for k in ('uptime_s','clip','emote_reaction_priority','emote_rig_active','emote_joint_motion','emote_seconds','motion_active','face_oval',*errors)})
   if time.monotonic()-start>4 and s['clip']=='Idle':
    assert s['motion_active'] and not s['emote_reaction_priority']
    expired=True;break
   time.sleep(.01)
  active=[s for s in rows if s['emote_reaction_priority']]
  assert active and max(s['emote_joint_motion'] for s in active)>.02 and expired
  assert all(s['face_oval']==0 for s in active)
  case['expired_during_motion']=True
  print('PASS:',kind,active[0]['clip'],'visible joints; face returns to physical control during motion',flush=True)
finally:
 touch(False);act('debug.motion',enabled=False);act('display.wake');act('app.home');time.sleep(.25)
 report['after']=status();report['errors']={k:report['after'][k]-initial[k] for k in errors}
 (out/'final-device.json').write_text(json.dumps(report,indent=2)+'\n')
print('Error deltas:',report['errors'],flush=True)
assert not any(report['errors'].values())
