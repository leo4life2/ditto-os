"""Input history and continuous digitizer health. USB gestures are synthetic."""
import json,sys,time,math,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from device import Device
VERSION=os.environ.get('SCOUT_EXPECTED_VERSION',(ROOT/'VERSION').read_text().strip())
OUT=ROOT/'test-results'/os.environ.get('SCOUT_EVIDENCE_NAME','touch-response-'+VERSION);OUT.mkdir(exist_ok=True)
report={'input':'USB injections through production input queue; not fingertip latency','checks':[],'samples':[]}
def save(): (OUT/'hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 try:
  time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(2)
  report['baseline']=d.call('system.status');report['input_baseline']=d.call('debug.touch.status')
  assert report['baseline']['version']==VERSION and report['baseline']['frames']>10
  boot=d.boot_count
  for direction in [-1,1]:
   before=d.call('debug.touch.status');start=time.monotonic()
   d.call('debug.touch',{'down':True,'x':205,'y':290})
   for i in range(1,5):
    d.call('debug.touch',{'down':True,'x':205+direction*i*20,'y':290});time.sleep(.003)
   d.call('debug.touch',{'down':False,'x':205+direction*80,'y':290})
   duration=(time.monotonic()-start)*1000;time.sleep(.060)
   after=d.call('debug.touch.status')
   assert after['seen_press']==after['press'] and not after['down'] and after['stretch']>.01
   assert after['tap_count']==before['tap_count'] and after['consumed']-before['consumed']>=6
   report['checks'].append({'gesture':'right' if direction>0 else 'left','injected_duration_ms':duration,'after':after});save()
   time.sleep(2)
  d.screenshot(OUT/'home.png')
  d.call('display.sleep');time.sleep(3)
  asleep=d.call('system.status');report['sleep']=asleep
  assert asleep['sleeping'] and asleep['render_parked'] and asleep['effective_brightness']==10
  d.screenshot(OUT/'sleep.png')
  d.call('debug.touch',{'down':True,'x':205,'y':290});time.sleep(.08)
  d.call('debug.touch',{'down':False,'x':205,'y':290});time.sleep(.4)
  awake=d.call('system.status');report['wake']=awake
  assert not awake['sleeping'] and awake['effective_brightness']==awake['brightness']
  d.screenshot(OUT/'wake.png')
  # Keep this one connection open through quiet awake/standby intervals. Sparse
  # diagnostics reveal whether bus faults occur only while the IC has no IRQ.
  start=time.monotonic()
  for i in range(120):
   if i==25:d.call('display.sleep')
   if i==65:d.call('display.wake')
   if i==95:d.call('wifi.scan')
   sample=d.call('debug.touch.status');sample['elapsed_s']=time.monotonic()-start
   report['samples'].append(sample)
   assert d.boot_count==boot,'Unexpected reset during continuous connection'
   if i%10==0:
    save();print({k:sample[k] for k in ['elapsed_s','samples','errors','recoveries','irq_count','bus_errors','decode_errors','overflow','task_stack_free']},flush=True)
   time.sleep(1)
  d.call('app.home');report['final']=d.call('system.status');report['input_final']=d.call('debug.touch.status')
  report['logs']=d.logs;report['boot_count']=d.boot_count
  assert report['final']['display_errors']==0 and report['input_final']['overflow']==0
  report['passed']=True;save()
 except Exception as e:
  report['error']=repr(e);report['logs']=d.logs;save();raise
