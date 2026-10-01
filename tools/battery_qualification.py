"""Observe real persistence/standby behavior without altering battery source."""
import json,time,urllib.request
from pathlib import Path
out=Path('test-results/wireless-battery-0.11.51')
def call(tool,args={}):
 data=json.dumps({'tool':tool,'arguments':args}).encode();r=urllib.request.Request('http://127.0.0.1:8781/action',data,{'Content-Type':'application/json'});return json.load(urllib.request.urlopen(r,timeout=8))
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=5))
rows=[];start=time.monotonic()
while time.monotonic()-start<130:
 s=status();p=call('power.log.info');rows.append({'seconds':time.monotonic()-start,'uptime':s['uptime_s'],'reset':s['reset_reason'],'fps':s['fps'],'sleeping':s['sleeping'],'heap':s['internal_free'],'radio':s['bluetooth_available'],'journal':p})
 print(json.dumps(rows[-1]),flush=True);time.sleep(10)
(out/'battery-observation.json').write_text(json.dumps(rows,indent=2))
assert all(b['uptime']>a['uptime'] for a,b in zip(rows,rows[1:])),'Unexpected reboot'
assert rows[-1]['journal']['latest_sequence']>=rows[0]['journal']['latest_sequence']+2
assert all(r['journal']['write_errors']==0 for r in rows)
print('PASS: continuous runtime and two durable minute records without reboot/write error',flush=True)
