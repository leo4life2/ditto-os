"""Observe an uninterrupted real rest interval; handling restarts the timer."""
import json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
out=evidence_directory(ROOT);report={'passed':False,'samples':[]}
with Device() as d:
 time.sleep(2);d.sync_clock();d.call('app.home');d.call('display.wake')
 start=time.monotonic();parked=None
 try:
  while time.monotonic()-start<180:
   s=d.call('system.status');report['samples'].append(s)
   assert not s['motion_simulated'] and s['imu_available'] and s['imu_sample_age_ms']<150,s
   if s['sleeping'] and s['effective_brightness']==10 and s['power_saving']:
    if parked is None:parked=s
    elif s['uptime_s']-parked['uptime_s']>3:
     assert s['physics_steps']==parked['physics_steps']
     assert s['audio_input_blocks']==parked['audio_input_blocks']
     assert s['imu_samples']>parked['imu_samples']+50
     report['parked_start']=parked;report['parked_end']=s;report['passed']=True;break
   else:parked=None
   time.sleep(.3)
  assert report['passed'],'No uninterrupted rest interval within 180 seconds; inspect physical activity in samples'
  d.call('debug.touch',{'down':True,'x':50,'y':60});time.sleep(.07)
  d.call('debug.touch',{'down':False,'x':50,'y':60});time.sleep(.4)
  s=d.call('system.status');assert not s['sleeping'] and s['effective_brightness']==s['brightness'] and not s['audio_sleeping']
  report['tap_wake']=s
 finally:
  report['final']=d.call('system.status');report['logs']=list(d.logs)
  (out/'quiet-sleep-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'passed':report['passed'],'seconds_to_park':report['parked_start']['uptime_s']-report['samples'][0]['uptime_s'],'sleep_idle_for_s':report['parked_start']['idle_for_s'],'tap_wake':True},indent=2))
