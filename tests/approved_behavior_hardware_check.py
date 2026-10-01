"""Exercise the approved behavior through real firmware, fusion and renderer.

Inputs are USB-injected (not a claim of robotic physical touch/motion testing).
The initial noise interval uses the actual IMU. Restore real input on exit.
"""
import json, math, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT)
report={'passed':False,'runs':{}}
with Device() as d:
 time.sleep(3);d.sync_clock();d.call('app.home');d.call('display.wake')
 d.call('avatar.emote',{'name':'Idle'});d.call('voice.state.set',{'state':'idle','caption':'Ready for you'})
 baseline=d.call('system.status');report['baseline']=baseline
 assert baseline['version']=='0.9.0' and baseline['frames']>0,baseline
 def run(name,seconds,touch=None,motion=None):
  states=[];start=time.monotonic();deadline=start;iteration=0
  while time.monotonic()-start<seconds:
   t=time.monotonic()-start
   if touch is not None:
    down,x,y=touch(t);d.call('debug.touch',{'down':down,'x':round(x),'y':round(y)})
   if motion is not None:
    acc,gyro=motion(t);d.call('debug.motion',{'acceleration':acc,'gyro':gyro})
   if iteration%2==0:
    s=d.call('system.status');s['test_time']=time.monotonic()-start;states.append(s)
    assert s['face_x_eyes']==0 and 0<=s['face_oval']<=1.001 and 0<=s['face_melon']<=1.001,s
   iteration+=1;deadline+=1/50;time.sleep(max(0,deadline-time.monotonic()))
  report['runs'][name]=states
  print(name,states[-1]['behavior'],states[-1]['shake_damping'],flush=True)
  return states
 quiet=lambda t:([0,0,1],[0,0,0])
 off=lambda t:(False,205,310)
 def tap(t):return (t<.12,205,310)
 def screenshot(name):d.screenshot(OUT/(name+'-device.png'))
 try:
  noise=run('real-imu-noise',3)
  assert all(not s['shake_active'] and s['shake_damping']==1 for s in noise)
  run('settle',2,off,quiet)
  states=run('single-tap',3.3,tap,quiet)
  contact=next(s['uptime_s'] for s in states if s['behavior']=='tap')
  oval=[s for s in states if .25<s['uptime_s']-contact<.80]
  melon=[s for s in states if 1.20<s['uptime_s']-contact<1.60]
  assert len(oval)>4 and min(s['face_oval'] for s in oval)>.98
  assert len(melon)>3 and min(s['face_melon'] for s in melon)>.98
  assert states[-1]['behavior']=='idle' and states[-1]['face_melon']<.01
  assert states[-1]['tap_count']>baseline['tap_count']
  # Separate capture passes, because a framebuffer download affects timing.
  run('tap-oval-capture',.34,tap,quiet);screenshot('tap-oval');time.sleep(3)
  run('tap-melon-capture',1.25,tap,quiet);screenshot('tap-melon');time.sleep(3)
  def pull(t):return (t<2.5,205-70*min(1,t/.9),310-45*min(1,t/.9))
  states=run('pull-hold-release',5,pull,quiet)
  held=[s for s in states if 1.9<s['test_time']<2.4]
  assert any(s['behavior']=='pull' for s in states)
  assert all(s['behavior']=='hold' and abs(s['face_gaze'][0])<.025 for s in held)
  assert states[-1]['behavior']=='idle'
  states=run('flick',3.4,lambda t:(t<.24,205+min(85,500*t),310),quiet)
  assert any(s['behavior']=='flick' for s in states)
  before=d.call('system.status')['tap_count']
  states=run('repeat-pokes',4.8,lambda t:(t<1.4 and t%.60<.13,205,310),quiet)
  assert max(s['behavior_contacts'] for s in states)==3
  assert states[-1]['tap_count']-before==3 and states[-1]['behavior']=='idle'
  states=run('poke-then-drag',4.5,lambda t:(t<.12 or .6<t<2,205-60*max(0,min(1,(t-.6)/.8)),310),quiet)
  assert any(s['behavior']=='pull' for s in states) and states[-1]['behavior']=='idle'
  for axis in range(3):
   def shake(t):
    acc=[0,0,1];acc[axis]+=.5*math.sin(2*math.pi*4*t);return acc,[0,0,0]
   states=run('shake-'+str(axis),2.5,off,shake)
   assert any(s['shake_active'] for s in states),axis
   assert all(s['shake_damping']==1 for s in states if s['shake_active'])
   tail=run('stop-'+str(axis),3,off,quiet)
   assert any(s['shake_recovering'] and s['shake_damping']>3.49 for s in tail),axis
   assert max(abs(x) for x in tail[-1]['motion_wave'])<.025,tail[-1]
   gentle=run('gentle-resume-'+str(axis),.6,off,
     lambda t:([math.sin(.16*t),0,math.cos(.16*t)],[0,-.16,0]))
   assert gentle[-1]['shake_damping']==1 and not gentle[-1]['shake_recovering']
   resume=run('resume-'+str(axis),.4,off,shake)
   assert resume[-1]['shake_damping']==1
   run('reset-motion-'+str(axis),.3,off,lambda t:([.2,0,1],[0,0,0]))
   run('quiet-motion-'+str(axis),3,off,quiet)
  # Slow swing is intentionally below the shake classifier reversal rate.
  states=run('slow-swing',3.5,off,lambda t:([.15*math.sin(2*math.pi*.7*t),0,1],[0,0,0]))
  assert not any(s['shake_active'] for s in states) and states[-1]['shake_damping']==1
  run('pre-sleep',3,off,quiet);d.call('debug.motion',{'enabled':False})
  d.call('display.sleep');states=run('sleep-settling',3)
  assert any(.05<s['face_sleep']<.95 for s in states)
  asleep=states[-1]
  assert asleep['sleeping'] and asleep['render_parked'] and asleep['effective_brightness']==10,asleep
  assert abs(asleep['face_eye_open']-.24)<.001 and abs(asleep['face_eye_width']-1.65)<.001,asleep
  screenshot('sleep-settled');time.sleep(2)
  parked=d.call('system.status')
  assert parked['physics_steps']==asleep['physics_steps'] and parked['audio_input_blocks']==asleep['audio_input_blocks']
  assert parked['imu_samples']>asleep['imu_samples'] and parked['imu_sample_age_ms']<150
  report['parked']=parked
  # The clock may refresh; all non-clock pixels and animation time stay frozen.
  image=d.screenshot(OUT/'sleep-before-clock-device.png')
  now=time.time();d.call('clock.sync',{'epoch':now+65,'timezone':'UTC7'})
  time.sleep(1.2);after=d.screenshot(OUT/'sleep-after-clock-device.png')
  assert image.crop((0,150,410,502)).tobytes()==after.crop((0,150,410,502)).tobytes()
  assert image.tobytes()!=after.tobytes(),'Clock did not update'
  d.sync_clock()
  states=run('touch-wake',.65,lambda t:(t<.10,50,60))
  assert not states[-1]['sleeping'] and states[-1]['effective_brightness']==states[-1]['brightness']
  d.call('display.sleep');time.sleep(2.5)
  states=run('pickup-wake',.7,off,lambda t:([.6 if t<.25 else 0,0,1],[0,0,0]))
  assert not states[-1]['sleeping'] and states[-1]['motion_wakes']>baseline['motion_wakes']
  run('after-wake',3,off,quiet)
  d.call('voice.state.set',{'state':'speaking','caption':'Testing the shared character'})
  states=run('voice-plus-poke',2.8,tap,quiet)
  assert states[-1]['face_melon']>.25 and all(s['face_x_eyes']==0 for s in states)
  screenshot('speaking')
  d.call('voice.state.set',{'state':'idle','caption':'Ready for you'})
  states=run('voice-release',1,off,quiet)
  assert states[-1]['behavior']=='idle' and states[-1]['face_melon']<.01
  report['passed']=True
 finally:
  d.call('debug.touch',{'down':False,'x':205,'y':310});d.call('debug.motion',{'enabled':False})
  d.call('avatar.emote',{'name':'Idle'});d.call('voice.state.set',{'state':'idle','caption':'Ready for you'})
  d.call('app.home');d.call('display.wake');d.sync_clock();time.sleep(.6)
  report['final']=d.call('system.status');report['logs']=d.logs
  (OUT/'approved-hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
  screenshot('approved-home')
 assert all(report['final'][k]==baseline[k] for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','te_timeouts','scan_deadline_misses'))
 assert report['final']['imu_available'] and not report['final']['motion_simulated']
print('Approved behavior, actual-IMU noise, injected gestures/fusion, sleep freeze, minute update and wake passed.')
