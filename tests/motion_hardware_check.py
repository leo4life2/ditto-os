"""Six-axis diagnostics through the real firmware, followed by live-sensor restore."""
import json,math,os,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device,DeviceError
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={}
with Device() as d:
 time.sleep(2);d.sync_clock();d.call('app.home');d.call('avatar.emote',{'name':'Idle'})
 base=d.call('system.status');assert base['version']==os.environ.get('SCOUT_EXPECTED_VERSION','0.8.1');report['live_start']=base
 assert base['imu_available'] and base['imu_configured'] and base['motion_age_ms']<150,base
 def drive(acc,gyro,seconds):
  stop=time.monotonic()+seconds
  while time.monotonic()<stop:
   d.call('debug.motion',{'acceleration':acc,'gyro':gyro});time.sleep(.016)
  return d.call('system.status')
 def neutral():
  d.call('debug.motion',{'enabled':False});time.sleep(.18);return drive([0,0,1],[0,0,0],5)
 try:
  states={}
  for name,acc in [('left',[.7,0,.714]),('right',[-.7,0,.714]),('up',[0,-.7,.714]),('down',[0,.7,.714])]:
   # A gap starts this diagnostic sequence with its actual known orientation.
   d.call('debug.motion',{'enabled':False});time.sleep(.18)
   s=drive(acc,[0,0,0],1.1);states[name]=s
   axis=0 if name in ('left','right') else 1
   assert s['motion_offset'][axis]*acc[axis]<-.02,s
   d.screenshot(OUT/(name+'-tilt-device.png'))
  report['tilts']=states;impulses={};rotations={}
  for axis in range(3):
   neutral();acc=[0,0,1];acc[axis]+=1.3
   s=drive(acc,[0,0,0],.17);impulses[str(axis)]=s
   assert s['motion_linear_g'][axis]>.6 and s['motion_offset'][axis]>.015,s
   if axis==2:d.screenshot(OUT/'depth-slosh-device.png')
   neutral();gyro=[0,0,0];gyro[axis]=3
   s=drive([0,0,1],gyro,.22);rotations[str(axis)]=s
   assert s['motion_spin'][axis]<-.015,s
  report['linear_impulses']=impulses;report['rotations']=rotations
  s=neutral();assert max(abs(v) for v in s['motion_spin'])<.012
  try:d.call('debug.motion',{'acceleration':[0,0,1],'gyro':[100,0,0]})
  except DeviceError:report['invalid_rejected']=True
  else:raise AssertionError('Invalid gyro accepted')
  d.call('display.sleep');drive([0,0,1],[0,0,0],2);s=d.call('system.status')
  assert s['sleeping'] and s['effective_brightness']==10 and max(abs(v) for v in s['motion_offset'])==0 and max(abs(v) for v in s['motion_spin'])==0,s
  drive([1,0,0],[2,1,2],.5);assert not d.call('system.status')['sleeping']
  neutral();d.call('display.sleep');drive([0,0,1],[0,0,0],2)
  d.call('debug.motion',{'enabled':False});d.call('debug.touch',{'down':True,'x':50,'y':60});d.call('debug.touch',{'down':False,'x':50,'y':60});time.sleep(.3)
  s=d.call('system.status');assert not s['sleeping'] and s['effective_brightness']==base['brightness'];report['sleep_and_wake']=True
  # A stopped injector cannot leave fake motion active.
  drive([0,0,1],[0,0,0],.1);time.sleep(.5);s=d.call('system.status');assert not s['motion_simulated'] and s['motion_age_ms']<150 and s['imu_samples']>base['imu_samples'];report['lease_recovery']=True
  real=[]
  for i in range(40):real.append(d.call('system.status'));time.sleep(.025)
  report['live_samples']=real
  assert all(s['imu_available'] and not s['motion_simulated'] and s['imu_sample_age_ms']<150 for s in real)
  assert len({s['imu_timestamp'] for s in real})>30
  assert len({tuple(s['motion_specific_g']) for s in real})>5
  assert len({tuple(s['motion_gyro_rads']) for s in real})>5
  assert all(abs(sum(v*v for v in s['motion_gravity_g'])-1)<.02 for s in real)
 finally:
  d.call('debug.motion',{'enabled':False});d.call('debug.touch',{'down':False,'x':50,'y':60});d.call('app.home');d.call('display.wake');d.call('avatar.emote',{'name':'Idle'});time.sleep(.5)
  report['final']=d.call('system.status');report['device_logs']=d.logs
  (OUT/'motion-hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
  d.screenshot(OUT/'motion-home-device.png')
print('Live IMU, four gravity directions, all six impulses, sleep/wake, malformed input and diagnostic expiry passed.')
