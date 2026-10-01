"""Large live telemetry must survive the 4 KiB USB transmit-ring boundary."""
import sys,time,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
out=evidence_directory(ROOT);raw=bytearray();replies=[]
with Device() as d:
 original=d.serial.read
 def read(n=1):
  b=original(n);raw.extend(b);return b
 d.serial.read=read
 time.sleep(2);d.sync_clock();d.call('app.home');d.call('avatar.emote',{'name':'Idle'})
 try:
  for i in range(60):
   if i==0:d.call('debug.touch',{'down':True,'x':105,'y':282})
   if i==4:d.call('debug.touch',{'down':False,'x':105,'y':282})
   s=d.call('system.status',timeout=2);replies.append(s);time.sleep(.02)
  assert replies[-1]['frames']>replies[0]['frames']+30
 finally:
  d.call('debug.touch',{'down':False,'x':105,'y':282});d.call('app.home')
lines=[line.split(b'SCOUT ',1)[1] for line in bytes(raw).splitlines() if b'SCOUT ' in line]
parsed=[json.loads(line) for line in lines];sizes=[len(line) for line,p in zip(lines,parsed) if 'frames' in p.get('result',{})]
assert max(sizes)>4096,(min(sizes),max(sizes))
report={'status_replies':len(sizes),'maximum_payload_bytes':max(sizes),'minimum_payload_bytes':min(sizes),'malformed_replies':0,'final':replies[-1]}
(out/'usb-reply-size.json').write_text(json.dumps(report,indent=2)+'\n')
print('All',len(sizes),'status replies parsed; largest',max(sizes),'bytes.')
