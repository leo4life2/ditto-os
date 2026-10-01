"""Exercise every oval route and sustained-shake dizziness on the real renderer.

Touches and motion are injected through firmware input paths. Captures are
separate marked pauses in observation, so this is not an optical latency test.
"""
from pathlib import Path
import json,math,sys,time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from device import Device
OUT=Path(sys.argv[1]).resolve();OUT.mkdir(parents=True,exist_ok=False)
report={'passed':False,'runs':{},'scope':__doc__}
def save():(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 try:
  time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(2)
  base=d.call('system.status');boot=d.boot_count;report['baseline']=base
  assert base['version']==(ROOT/'VERSION').read_text().strip()
  for name,duration in [('tap',.12),('flick',.23),('pull',1.6),('hold',.85),('swing',1.2),('short-shake',.8),('dizzy',3.5)]:
   samples=[];start=time.monotonic();deadline=start
   def sample():
    s=d.call('system.status');s['test_time']=time.monotonic()-start;samples.append(s)
    assert d.boot_count==boot
    assert 0<=s['face_dizzy']<=1 and s['face_oval']+s['face_melon']<1.001
    return s
   iteration=0
   while time.monotonic()-start<duration:
    t=time.monotonic()-start
    if name in ['tap','flick','pull','hold']:
     x=202+(min(90,t*500) if name=='flick' else -min(65,t*45) if name=='pull' else 0)
     d.call('debug.touch',{'down':True,'x':round(x),'y':306})
    else:
     amp=.18 if name=='swing' else .65
     hz=.8 if name=='swing' else 4
     d.call('debug.motion',{'acceleration':[amp*math.sin(2*math.pi*hz*t),0,1],'gyro':[0,0,0]})
    if iteration%3==0:sample()
    iteration+=1;deadline+=.020;time.sleep(max(0,deadline-time.monotonic()))
   d.call('debug.touch',{'down':False,'x':202,'y':306})
   d.call('debug.motion',{'enabled':False})
   stopped=time.monotonic();captured=False
   while time.monotonic()-stopped<7.5:
    s=sample();age=time.monotonic()-stopped
    if not captured and age>2:
     capture_state=s
     d.screenshot(OUT/(name+'.png'));captured=True
    time.sleep(.055)
   run={'samples':samples,'release_time':stopped-start,'capture_status':capture_state}
   report['runs'][name]=run;save()
   if name!='dizzy':
    held=[s for s in samples if .7<s['test_time']-run['release_time']<2.1]
    assert len(held)>12 and min(s['face_oval'] for s in held)>.98,(name,[(s['test_time'],s['behavior'],s['face_oval']) for s in held])
    assert max(s['face_dizzy'] for s in samples)==0,name
   else:
    assert capture_state['face_dizzy']>.99 and capture_state['face_eye_open']==1
    full=[s for s in samples if s['face_dizzy']>.99]
    assert len(full)>15
    run['dizzy_presented_fps']=(full[-1]['presented_frames']-full[0]['presented_frames'])/(full[-1]['uptime_s']-full[0]['uptime_s'])
   assert samples[-1]['face_dizzy']==0 and samples[-1]['behavior']=='idle',name
   run['passed']=True;save();print(name,'passed',run.get('dizzy_presented_fps',''),flush=True)
  # A tap interrupts dizzy face immediately and still applies its own impulse.
  start=time.monotonic()
  while time.monotonic()-start<3:
   t=time.monotonic()-start
   d.call('debug.motion',{'acceleration':[.65*math.sin(2*math.pi*4*t),0,1],'gyro':[0,0,0]});time.sleep(.018)
  assert d.call('system.status')['face_dizzy']>.98
  d.call('debug.motion',{'enabled':False})
  before=d.call('system.status')
  d.call('debug.touch',{'down':True,'x':202,'y':306});time.sleep(.1)
  d.call('debug.touch',{'down':False,'x':202,'y':306});time.sleep(.15)
  after=d.call('system.status');assert after['face_dizzy']==0 and after['tap_count']==before['tap_count']+1
  report['interrupt']={'before':before,'after':after}
  report['final']=after
  keys=['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','scan_deadline_misses','te_timeouts']
  report['fault_deltas']={k:after[k]-base[k] for k in keys};assert not any(report['fault_deltas'].values())
  report['passed']=True;save()
 except Exception as e:
  report['error']=repr(e);save();raise
 finally:
  d.call('debug.touch',{'down':False,'x':202,'y':306});d.call('debug.motion',{'enabled':False})
  report['logs']=list(d.logs);save()
