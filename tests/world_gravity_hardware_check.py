"""World gravity on the real renderer, followed by real IMU restoration."""
import json,math,sys,time,threading
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={'directions':{}}
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def unit(a):return [x/math.sqrt(dot(a,a)) for x in a]
def transform(s,v):
 x,y,z=s['material_scale'];xy,xz,yz=s['material_shear']
 return [dot([x,xy,xz],v),dot([xy,y,yz],v),dot([xz,yz,z],v)]
with Device() as d:
 time.sleep(2);d.sync_clock();d.call('display.wake');d.call('app.home');d.call('avatar.emote',{'name':'Idle'})
 base=d.call('system.status');assert base['version']=='0.8.1';report['baseline']=base
 # Keep the diagnostic lease alive even while taking complete framebuffer captures.
 lock=threading.Lock();feed={'a':None,'w':[0,0,0]};stop=threading.Event();errors=[]
 def injector():
  try:
   while not stop.wait(.016):
    with lock:
     if feed['a'] is not None:
      a,w=feed['a'],feed['w']
      if feed.get('rotation_start') is not None:
       angle=(time.monotonic()-feed['rotation_start'])*math.pi/2
       a=[math.sin(angle),math.cos(angle),0];w=[0,0,math.pi/2]
      d.call('debug.motion',{'acceleration':a,'gyro':w})
  except Exception as e:errors.append(str(e))
 thread=threading.Thread(target=injector);thread.start()
 def orientation(g):
  with lock:feed['a']=None;feed['rotation_start']=None;d.call('debug.motion',{'enabled':False})
  time.sleep(.2)
  with lock:feed['a']=[-x for x in unit(g)];feed['w']=[0,0,0]
  time.sleep(.22);d.call('display.sleep');time.sleep(.12);d.call('display.wake')
 def status():
  s=d.call('system.status');assert abs(s['material_volume']-1)<.00002,s
  assert s['motion_simulated'] and not s['sleeping'],s
  return s
 try:
  for name,g in [('down',[0,-1,0]),('up',[0,1,0]),('left',[-1,0,0]),('right',[1,0,0]),('into-screen',[0,0,-1]),('out-of-screen',[0,0,1]),('diagonal',[-1,-1,0]),('diagonal-depth',[1,-2,-3])]:
   orientation(g);start=time.monotonic();rows=[]
   while time.monotonic()-start<2.25:rows.append(status());time.sleep(.05)
   s=rows[-1];g=unit(g);body=unit(s['material_gravity']);axial=dot(transform(s,body),body)
   assert dot(s['motion_gravity_g'],g)>.999,s
   assert .78<axial<.85,(name,axial,s)
   assert dot(s['material_support'],body)>.55,s
   report['directions'][name]={'axial_scale':axial,'samples':rows,'final':s}
   print(name,'compression',round(axial,4),'fps',round(s['fps'],2),flush=True)
   d.screenshot(OUT/(name+'-gravity-device.png'))
  # Rotate continuously through 360 degrees; gyro carries the gravity estimate.
  orientation([0,-1,0]);time.sleep(1);start=time.monotonic();rows=[]
  # Produce matching acceleration and gyro at each injection, rather than
  # repeating an old orientation while a separate status loop advances it.
  with lock:feed['rotation_start']=start
  while time.monotonic()-start<4:
   s=status();rows.append(s);time.sleep(.02)
  report['rotation']=rows
  assert all(abs(dot(s['motion_gravity_g'],s['motion_gravity_g'])-1)<.002 for s in rows)
  assert max(math.sqrt(dot([s['motion_gravity_g'][k]+s['motion_specific_g'][k] for k in range(3)],[s['motion_gravity_g'][k]+s['motion_specific_g'][k] for k in range(3)])) for s in rows)<.08
  orientation([0,0,-1]);time.sleep(2.3)
  d.call('display.sleep');time.sleep(2);a=d.call('system.status');time.sleep(.3);b=d.call('system.status')
  assert b['sleeping'] and b['effective_brightness']==10 and b['audio_sleeping'] and b['render_parked'],b
  assert b['material_scale']==[1,1,1] and b['material_shear']==[0,0,0] and b['physics_steps']==a['physics_steps'],b
  with lock:feed['w']=[0,1,0]
  time.sleep(.3);s=d.call('system.status');assert not s['sleeping'] and s['motion_wakes']>b['motion_wakes'],s
  report['sleep_motion_wake']={'asleep':b,'awake':s}
 finally:
  stop.set();thread.join(3);assert not thread.is_alive()
  d.call('debug.motion',{'enabled':False});d.call('display.wake');d.call('app.home');d.call('avatar.emote',{'name':'Idle'});time.sleep(.5)
  real=[]
  for _ in range(35):real.append(d.call('system.status'));time.sleep(.03)
  report['live_samples']=real;report['final']=real[-1];report['logs']=list(d.logs);report['injector_errors']=errors
  (OUT/'world-gravity-hardware.json').write_text(json.dumps(report,indent=2)+'\n')
  d.screenshot(OUT/'world-gravity-home-device.png')
  assert not errors and all(s['imu_available'] and not s['motion_simulated'] and s['imu_sample_age_ms']<150 for s in real)
  assert len({s['imu_timestamp'] for s in real})>30
  assert all(real[-1][k]==base[k] for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_power_errors'))
print('Eight gravity directions, continuous rotation, unit volume, parked sleep, motion wake and real IMU restoration passed.')
