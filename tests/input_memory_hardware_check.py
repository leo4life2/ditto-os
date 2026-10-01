"""Actual USB command capacity/stack-margin test. Sole USB owner required."""
from pathlib import Path
import argparse,json,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('output',type=Path);args=p.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True);report={'checks':[]}
with Device() as d:
 time.sleep(3);d.sync_clock();boot=d.boot_count;baseline=d.call('system.status')
 empty={'id':'','tool':'system.status','arguments':{}}
 overhead=len(json.dumps(empty,separators=(',',':')).encode())
 request_id='maximum-'+('a'*(4095-overhead-8))
 assert len(json.dumps({**empty,'id':request_id},separators=(',',':')).encode())==4095
 s=d.call('system.status',request_id=request_id);assert s['version']==baseline['version']
 report['checks'].append('maximum 4095-byte JSON command and large response ID')
 q=d.subscribe('protocol.error')
 try:
  with d.lock:d.serial.write(b' '*4096+b'\n')
  event=q.get(timeout=5);assert event['message']=='Command too long',event
  assert d.call('system.status')['version']==baseline['version']
 finally:d.unsubscribe(q)
 report['checks'].append('overflow discarded and next command succeeds')
 nested=0
 for _ in range(16):nested=[nested]
 d.call('system.status',{'ignored':nested,'values':list(range(500))})
 report['checks'].append('nested and broad parsed values, including heap fallback')
 for tool in ['system.tools','debug.power','debug.touch.status','app.home']:
  d.call(tool)
 d.screenshot(args.output.with_suffix('.png'))
 final=d.call('system.status')
 assert d.boot_count==boot
 report.update({'passed':False,'baseline':baseline,'final':final,'minimum_main_stack_bytes':final['control_stack_free'],'logs':d.logs})
 args.output.write_text(json.dumps(report,indent=2)+'\n')
 assert final['control_stack_free']>=2048,final['control_stack_free']
 for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','scan_deadline_misses','te_timeouts']:
  assert final[k]==baseline[k],(k,baseline[k],final[k])
 report.update({'passed':True,'baseline':baseline,'final':final,'minimum_main_stack_bytes':final['control_stack_free'],'logs':d.logs})
 args.output.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'passed':True,'checks':report['checks'],'stack_free':final['control_stack_free'],'internal_free':final['internal_free'],'internal_low_watermark':final['internal_low_watermark']},indent=2))
