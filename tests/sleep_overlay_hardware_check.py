"""Measure actual firmware workload; no current or battery-life claim."""
import datetime,json,sys,time,threading,os
from pathlib import Path
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];VERSION=os.environ.get('SCOUT_EXPECTED_VERSION',(ROOT/'VERSION').read_text().strip());OUT=ROOT/'test-results'/os.environ.get('SCOUT_EVIDENCE_NAME','lightweight-sleep-'+VERSION);OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'host'))
from device import Device
from voice_transport import VoiceTransport
report={'current_measurement':False,'physical_touch_test':False,'samples':[]}
def save(): (OUT/'hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
def cpu(a,b):
 old={t['id']:t for t in a['tasks']};dt=b['uptime_us']-a['uptime_us']
 usage={t['name']:100*((t['runtime_us']-old[t['id']]['runtime_us'])%2**32)/dt for t in b['tasks'] if t['id'] in old}
 return {'average_busy_percent':100-(usage.get('IDLE0',0)+usage.get('IDLE1',0))/2,'tasks_percent_of_one_core':usage,'interval_s':dt/1e6,'a':a,'b':b}
with Device() as d:
 try:
  time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(2)
  base=d.call('system.status');report['baseline']=base;boot=d.boot_count
  assert base['version']==VERSION and base['frames']>10
  d.screenshot(OUT/'home.png')
  d.call('display.sleep');time.sleep(3)
  a=d.call('system.status');pa=d.call('debug.power');time.sleep(12);pb=d.call('debug.power');b=d.call('system.status')
  report['sleep']={'start':a,'end':b,'cpu':cpu(pa,pb)}
  assert b['sleeping'] and b['render_parked'] and b['sleep_overlay_active'] and b['effective_brightness']==10
  assert a['physics_steps']==b['physics_steps'] and a['audio_input_blocks']==b['audio_input_blocks']
  assert a['frames']==b['frames'] and b['sleep_overlay_frames']-a['sleep_overlay_frames']>=110
  assert report['sleep']['cpu']['average_busy_percent']<10
  d.screenshot(OUT/'sleep-a.png');time.sleep(1.2);d.screenshot(OUT/'sleep-b.png')
  ia=Image.open(OUT/'sleep-a.png').convert('RGB');ib=Image.open(OUT/'sleep-b.png').convert('RGB')
  diff=ImageChops.difference(ia,ib);outside=changed=0
  for y in range(502):
   for x in range(410):
    if diff.getpixel((x,y))!=(0,0,0):
     changed+=1
     if not(y<216 or 276<=y<332):outside+=1
  report['image_difference']={'changed_pixels':changed,'outside_clock_and_overlay_bands':outside}
  assert changed>50 and outside==0
  diff.save(OUT/'sleep-difference.png')
  # Midnight refresh without waking the body; restore system time afterward.
  midnight=datetime.datetime.now().astimezone().replace(hour=23,minute=59,second=58,microsecond=0)
  d.call('clock.sync',{'epoch':midnight.timestamp()});time.sleep(.3);d.screenshot(OUT/'before-midnight.png')
  c=d.call('system.status');time.sleep(2.4);d.screenshot(OUT/'after-midnight.png');e=d.call('system.status')
  assert e['sleeping'] and c['physics_steps']==e['physics_steps'];d.sync_clock()
  report['midnight']={'before':c,'after':e};save()
  # A quick gesture must wake, retain all moves, and leave no overlay trails.
  start=time.monotonic();d.call('debug.touch',{'down':True,'x':205,'y':290})
  for x in [225,245,265,285]:d.call('debug.touch',{'down':True,'x':x,'y':290});time.sleep(.004)
  d.call('debug.touch',{'down':False,'x':285,'y':290});time.sleep(.35)
  awake=d.call('system.status');report['wake']=awake
  assert not awake['sleeping'] and not awake['sleep_overlay_active'] and awake['effective_brightness']==base['brightness']
  d.screenshot(OUT/'wake.png')
  for i in range(3):
   d.call('display.sleep');time.sleep(2.4);assert d.call('system.status')['sleep_overlay_active']
   # Small physical sample injected through the real motion activity detector.
   # A new synthetic lease starts a fresh detector; prime its rest reference.
   for j in range(7):d.call('debug.motion',{'acceleration':[0,0,1],'gyro':[0,0,0]});time.sleep(.025)
   for j in range(10):d.call('debug.motion',{'acceleration':[.4,0,1],'gyro':[0,0,.4]});time.sleep(.025)
   time.sleep(.25);assert not d.call('system.status')['sleeping']
   d.call('debug.motion',{'enabled':False});time.sleep(2)
  voice=VoiceTransport(d);voice.start();counts={'mic_packets':0,'speaker_packets':0};stop=threading.Event()
  def mic():
   while not stop.is_set():
    if voice.read_audio():counts['mic_packets']+=1
  thread=threading.Thread(target=mic,daemon=True);thread.start()
  for i in range(100):voice.write_audio(bytes(640));counts['speaker_packets']+=1;time.sleep(.020)
  stop.set();thread.join(1);voice.close();report['audio']=counts
  assert counts['mic_packets']>50;d.call('app.home')
  # Keep one boot through standby's monitor-mode bus transitions.
  d.call('display.sleep');time.sleep(3)
  for i in range(60):
   q=d.call('debug.touch.status');report['samples'].append(q)
   if i%10==0:save();print({'step':i,'sleep_cpu':report['sleep']['cpu']['average_busy_percent'],'touch':{k:q[k] for k in ['irq_count','errors','recoveries','idle_bus_deferrals','overflow']}},flush=True)
   assert d.boot_count==boot;time.sleep(1)
  report['final']=d.call('system.status');report['input_final']=d.call('debug.touch.status');report['logs']=d.logs
  for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','scan_deadline_misses','te_timeouts']:
   assert report['final'][k]==base[k],(k,base[k],report['final'][k])
  assert report['input_final']['overflow']==0 and report['input_final']['recoveries']==0
  report['passed']=True;save();d.call('app.home');d.sync_clock()
 except Exception as e:
  report['error']=repr(e);report['logs']=d.logs;save();raise
