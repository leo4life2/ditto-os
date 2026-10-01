"""Private-buffer DMA experiment; never changes production display ownership."""
from pathlib import Path
import json,sys,time,argparse
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('output',type=Path);args=p.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
report={'production_copy_changed':False,'runs':[],'passed':False}
def save():args.output.write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 try:
  time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(2)
  report['baseline']=d.call('system.status');boot=d.boot_count
  for state in ['sleep','awake','sleep','awake']:
   d.call('display.sleep' if state=='sleep' else 'app.home');time.sleep(3)
   r=d.call('debug.copy.dma',timeout=20)
   report['runs'].append({'state':state,'result':r,'status':d.call('system.status')});save()
   print(json.dumps({'state':state,'result':r}),flush=True)
   assert r.get('valid'),r
   assert d.boot_count==boot,'Unexpected reboot'
  report['final']=d.call('system.status')
  for key in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','scan_deadline_misses','te_timeouts']:
   assert report['final'][key]==report['baseline'][key],key
  report['passed']=True
 except Exception as e:
  report['error']=repr(e);raise
 finally:
  report['logs']=d.logs;save()
  d.call('app.home')
