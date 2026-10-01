"""Whole-core hardware-counter attribution, not per-function or acoustic proof.

Requires the optional debug.cpu diagnostic and exclusive ownership of USB.
Each counter covers all tasks/interrupts on its core; event windows are separate
in time, so their ratios must not be added into a purported exact CPU budget.
"""
from pathlib import Path
import argparse,json,math,sys,threading,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device,DeviceError
from voice_transport import VoiceTransport

p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
a.output.parent.mkdir(parents=True,exist_ok=True)
report={'scope':'all work on each core, separate event windows','physical_touch':False,'cloud_voice':False,'phases':{},'errors':[]}
def save():a.output.write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 time.sleep(3);d.sync_clock();d.call('app.home');voice=VoiceTransport(d);voice.start()
 boot=d.boot_count
 try:
  for phase in ['control','duplex','control_repeat']:
   d.call('audio.capture.start' if phase=='duplex' else 'audio.capture.stop')
   stop=threading.Event();packets={'sent':0,'received':0};threads=[]
   def speaker():
    deadline=time.monotonic();started=deadline
    try:
     while not stop.is_set():
      voice.write_audio(bytes(640));packets['sent']+=1;deadline+=.020
      stop.wait(max(0,deadline-time.monotonic()))
    except Exception as e:report['errors'].append(repr(e))
    finally:packets['rate']=packets['sent']/(time.monotonic()-started)
   def mic():
    try:
     while not stop.is_set():
      if voice.read_audio():packets['received']+=1
    except Exception as e:report['errors'].append(repr(e))
   if phase=='duplex':
    threads=[threading.Thread(target=fn,daemon=True) for fn in [speaker,mic]]
    for t in threads:t.start()
   r={'events':[]};report['phases'][phase]=r
   try:
    for event in range(7):
     start=d.call('debug.cpu.start',{'event':event});assert 'error' not in start,start
     if phase=='control' and event==0:
      assert d.call('debug.cpu.start',{'event':1}).get('error')=='Counters already running'
     before=d.call('system.status');begin=time.monotonic()
     while time.monotonic()-begin<2:
      t=time.monotonic()-begin
      d.call('debug.touch',{'down':int(t*2)%4!=3,'x':205+int(65*math.sin(t*3)),'y':290+int(40*math.sin(t*2))})
      time.sleep(.018)
     counter=d.call('debug.cpu.stop');after=d.call('system.status')
     assert counter['valid'],counter
     for c in counter['cores']:
      assert 100<c['cycles']/c['elapsed_us']<250,c
      c['events_per_cycle']=c['count']/c['cycles']
     counter['fps']=(after['presented_frames']-before['presented_frames'])/(after['uptime_s']-before['uptime_s'])
     counter['before']=before;counter['after']=after
     r['events'].append(counter);save()
     assert not any(after[k]-before[k] for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','scan_deadline_misses','te_timeouts'])
     assert d.boot_count==boot
     print(phase,counter['event'],[round(c['events_per_cycle'],4) for c in counter['cores']],round(counter['fps'],2),flush=True)
   finally:
    stop.set()
    for t in threads:t.join(3)
    r['packets']=packets;save()
   if phase=='duplex':assert packets['rate']>=49,packets
  try:d.call('debug.cpu.start',{'event':7});raise AssertionError('Invalid event accepted')
  except DeviceError:pass
  assert d.call('debug.cpu.stop').get('error')=='Counters not running'
  assert not report['errors'];report['passed']=True
 finally:
  d.call('debug.cpu.stop');d.call('debug.touch',{'down':False,'x':205,'y':290})
  voice.close();d.call('app.home');report['logs']=d.logs;save()
