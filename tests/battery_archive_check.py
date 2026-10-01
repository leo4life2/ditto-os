import sys,tempfile,json,threading
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from battery import report,read_records,Archive
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from storage_hygiene import scratch_directory
rows=[]
for seq in range(1,65):
 rows.append(dict(sequence=seq,boot=1,flags=6 if seq<64 else 7,percent=100-seq//4,span_ms=60000,awake_ms=30000,sleep_ms=30000,battery_ms=60000 if seq>1 else 0,cpu_elapsed_ms=60000,idle0_us=30000000,idle1_us=60000000,firmware='test'))
text=report(rows,{'device_id':'001122334455'})
assert '25.0%' in text and 'reconnected' in text and 'Gauge decline:' in text and '**1.05 h observed**' in text
class Device:
 connected=True
 def call(self,tool,args=None):
  if tool=='system.status':return {}
  if tool=='power.log.info':return dict(available=True,device_id='001122334455',latest_sequence=64)
  if tool=='power.log.read':return dict(records=[r for r in rows if r['sequence']>args['after']][:8])
with scratch_directory('battery-archive',reserve_mib=2) as root:
 a=Archive(Device(),threading.Event(),root);a.sync();a.sync();path=root/'001122334455.jsonl';assert len(read_records(path))==64
 with path.open('a') as f:f.write('{"sequence":')
 assert len(read_records(path))==64
 a.sync();assert path.read_bytes().endswith(b'\n') and len(read_records(path))==64
 assert (root/'Battery Report.md').exists()
print('PASS: archive catch-up, duplicate-free reconnect, truncated tail recovery, CPU accounting and battery session report')
