"""Shared material response on-device: load settling, touch and live IMU restore."""
import json,os,sys,time,math,threading
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={}
with Device() as d:
 time.sleep(2);d.sync_clock();d.call('app.home');d.call('avatar.emote',{'name':'Idle'})
 base=d.call('system.status');assert base['version']==os.environ.get('SCOUT_EXPECTED_VERSION','0.8.1')
 def neutral():d.call('debug.motion',{'acceleration':[0,1,0],'gyro':[0,0,0]})
 def volume(s):return s['material_volume']
 def capture(path):
  # Frame capture can outlast the 250 ms diagnostic lease. Keep this known
  # orientation active while capturing instead of reverting to the real IMU.
  stop=threading.Event();errors=[]
  def feed():
   try:
    while not stop.wait(.03):neutral()
   except Exception as e:errors.append(str(e))
  thread=threading.Thread(target=feed);thread.start()
  try:d.screenshot(path)
  finally:stop.set();thread.join(3)
  assert not errors,errors
 def run(seconds,touch=None):
  start=time.monotonic();rows=[]
  while time.monotonic()-start<seconds:
   neutral()
   if touch:d.call('debug.touch',touch(time.monotonic()-start))
   s=d.call('system.status');s['test_seconds']=time.monotonic()-start;rows.append(s)
   assert abs(volume(s)-1)<.0001
   time.sleep(.018)
  return rows
 try:
  d.call('display.sleep');time.sleep(2);assert d.call('system.status')['material_scale']==[1,1,1]
  neutral();d.call('display.wake');rows=run(2.15);report['weight_settling']=rows
  assert rows[0]['material_scale'][1]>.95,rows[0]
  final=rows[-1];assert .78<final['material_scale'][1]<.83,final
  assert final['material_scale'][0]>1.09 and final['material_scale'][2]>1.09,final
  capture(OUT/'settled-device.png')
  run(1);before=d.call('system.status');d.call('debug.touch',{'down':True,'x':205,'y':290})
  run(.065);d.call('debug.touch',{'down':False,'x':205,'y':290});tap=run(.45)
  report['tap']=tap;assert any(s['material_scale'][2]<before['material_scale'][2]-.025 for s in tap)
  run(2)
  rows=run(1.8,lambda t:{'down':True,'x':205,'y':245+int(80*min(1,t/.7))})
  report['down_drag']=rows;assert rows[-1]['tap_hit'] and rows[-1]['slime_stretch']>.35,rows[-1]
  assert rows[-1]['material_scale'][1]<.74,rows[-1]
  capture(OUT/'pressed-device.png')
  d.call('debug.touch',{'down':False,'x':205,'y':325});report['release']=run(2.5)
  assert report['release'][-1]['material_scale'][1]>.76
  rows=run(1.5,lambda t:{'down':True,'x':205,'y':270-int(70*min(1,t/.6))})
  report['up_drag']=rows;assert rows[-1]['tap_hit'] and rows[-1]['material_scale'][1]>.91,rows[-1]
  capture(OUT/'pulled-device.png')
  d.call('debug.touch',{'down':False,'x':205,'y':200});run(2)
  perf0=d.call('system.status');run(5);perf1=d.call('system.status')
  report['fps']=(perf1['frames']-perf0['frames'])/(perf1['uptime_s']-perf0['uptime_s']);assert report['fps']>28
  assert perf1['display_errors']==base['display_errors']
  d.call('display.sleep');time.sleep(2);s=d.call('system.status');assert s['sleeping'] and s['effective_brightness']==10 and s['material_scale']==[1,1,1]
  d.call('debug.touch',{'down':True,'x':50,'y':60});time.sleep(.08);d.call('debug.touch',{'down':False,'x':50,'y':60});time.sleep(.2)
  s=d.call('system.status');assert not s['sleeping'] and s['effective_brightness']==base['brightness'];report['sleep_wake']=True
 finally:
  d.call('debug.motion',{'enabled':False});d.call('debug.touch',{'down':False,'x':205,'y':290});d.call('app.home');d.call('display.wake');time.sleep(.5)
  real=[]
  for i in range(35):real.append(d.call('system.status'));time.sleep(.03)
  report['live_samples']=real;report['final']=real[-1];report['logs']=list(d.logs)
  (OUT/'material-hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
  assert all(s['imu_available'] and not s['motion_simulated'] and s['imu_sample_age_ms']<150 for s in real)
  assert len({s['imu_timestamp'] for s in real})>30 and len({tuple(s['motion_specific_g']) for s in real})>5
  assert report['final']['touch_available'];d.screenshot(OUT/'home-device.png')
print('Force-driven settling, unit volume, tap pressure, opposing drags, release, 30 FPS, upright sleep/tap wake and live IMU passed.')
