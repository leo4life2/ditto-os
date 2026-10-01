"""Device visual evidence for dot eyes, gesture/emotion changes and drag release."""
import sys,time,json,threading,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={'emotes':{},'moods':{},'drags':{}}
with Device() as d:
 time.sleep(2);d.sync_clock();d.call('app.home');d.call('display.wake');d.call('avatar.expression',{'name':'neutral'})
 baseline=d.call('system.status');assert baseline['version']==os.environ.get('SCOUT_EXPECTED_VERSION','0.9.0');report['baseline']=baseline
 def held_capture(x,y,path):
  stop=threading.Event();errors=[]
  def feed():
   try:
    while not stop.wait(.025):d.call('debug.touch',{'down':True,'x':x,'y':y})
   except Exception as e:errors.append(str(e))
  thread=threading.Thread(target=feed);thread.start()
  try:
   time.sleep(.7);s=d.call('system.status')
  finally:stop.set();thread.join(3)
  assert not errors,errors
  # Snapshot the held pose now. Screenshot transmission blocks USB command
  # dispatch, so a status read after that transfer describes a later release.
  # Stop the injector first to avoid queued touches becoming a second press.
  d.screenshot(path)
  return s
 try:
  for name in ['Idle','Happy','Giggle','Celebrate','Wink','Sad','Curious']:
   d.call('avatar.emote',{'name':name});time.sleep(1.25)
   s=d.call('system.status');assert s['clip']==name,(name,s)
   d.screenshot(OUT/(name.lower()+'-device.png'));report['emotes'][name]=s
  # Confirm UI mood changes no longer independently squash/rotate the eyes.
  for name in ['curious','sad','angry','neutral']:
   d.call('avatar.emote',{'name':'Idle'});d.call('avatar.expression',{'name':name});time.sleep(.5)
   d.screenshot(OUT/(name+'-mood-device.png'));report['moods'][name]=d.call('system.status')
  for name,delta in [('left',-34),('right',34),('stretch',90)]:
   d.call('avatar.emote',{'name':'Idle'});time.sleep(.4)
   for i in range(12):
    d.call('debug.touch',{'down':True,'x':205+int(delta*i/11),'y':250});time.sleep(.02)
   before=d.call('system.status');assert before['tap_hit'] and before['slime_stretch']>.07,before
   if name=='stretch':
    # The approved handoff retains dots through stretching and holding.
    before=held_capture(205+delta,250,OUT/'stretch-held-device.png')
    assert before['face_x_eyes']==0 and before['face_eye_width']==1,before
   d.call('debug.touch',{'down':False,'x':205+delta,'y':250});time.sleep(.07)
   after=d.call('system.status');assert after['physics_activity']>.05,after
   d.screenshot(OUT/(name+'-release-device.png'));report['drags'][name]={'held':before,'released':after}
   time.sleep(1.8);d.screenshot(OUT/(name+'-recovered-device.png'))
  # Interrupt a wink with a happy emote and return to idle: the closure owner
  # must change instead of multiplying old blink/wink/expression transforms.
  d.call('avatar.emote',{'name':'Wink'});time.sleep(1.2)
  d.call('avatar.emote',{'name':'Happy'});time.sleep(.15);d.screenshot(OUT/'wink-to-happy-device.png')
  d.call('avatar.emote',{'name':'Idle'});time.sleep(.35);d.screenshot(OUT/'transition-idle-device.png')
  d.call('display.sleep');time.sleep(2.3);s=d.call('system.status');assert s['sleeping'] and s['effective_brightness']==10 and s['render_parked'],s
  d.screenshot(OUT/'sleep-device.png')
  d.call('debug.touch',{'down':True,'x':50,'y':60});d.call('debug.touch',{'down':False,'x':50,'y':60});time.sleep(.4)
  s=d.call('system.status');assert not s['sleeping'] and s['effective_brightness']==baseline['brightness'],s
  report['sleep_wake']=True
 finally:
  d.call('debug.touch',{'down':False,'x':205,'y':250});d.call('display.wake');d.call('app.home');d.call('avatar.emote',{'name':'Idle'});d.call('avatar.expression',{'name':'neutral'});time.sleep(.4)
  final=d.call('system.status');report['final']=final;report['device_logs']=list(d.logs)
  (OUT/'eye-hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
  d.screenshot(OUT/'home-device.png')
 assert final['imu_available'] and not final['motion_simulated'] and final['touch_available']
 assert all(final[k]==baseline[k] for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_power_errors'))
print('Happy/Giggle/Celebrate/Wink, UI moods, left/right/strong drags, release, interrupted emotes and sleep/wake captured; live sensors restored.')
