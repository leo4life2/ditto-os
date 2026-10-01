"""Cross real minute boundaries while parked, with radio enabled; restore Mac time."""
from pathlib import Path
import urllib.request,json,time,sys
out=Path(sys.argv[1]);base='http://127.0.0.1:8781';rows=[]
def status():return json.load(urllib.request.urlopen(base+'/status',timeout=4))
def act(tool,**args):return json.load(urllib.request.urlopen(urllib.request.Request(base+'/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'}),timeout=6))
s=status();assert not s['touch_down'] and not s['usb_recording'];assert s['bluetooth_available']
try:
 act('app.home')
 for cycle in range(3):
  # Begin eight seconds before a minute boundary, leaving time to settle.
  act('clock.sync',epoch=int(time.time())//60*60+52);act('display.sleep');time.sleep(3)
  before=status();assert before['sleeping'] and before['render_parked']
  time.sleep(7);after=status()
  assert after['uptime_s']>before['uptime_s'] and after['reset_reason']==s['reset_reason']
  assert after['sleeping'] and after['render_parked'] and after['epoch']//60>before['epoch']//60
  assert after['effective_brightness']==10 and after['bluetooth_available']
  assert after['sleep_overlay_frames']>before['sleep_overlay_frames']
  rows.append({'before':before,'after':after});print('PASS: parked clock minute boundary',cycle+1,flush=True)
finally:
 act('clock.sync',epoch=time.time());act('display.wake');act('app.home')
 out.write_text(json.dumps({'version':s['version'],'cycles':rows,'restored':status()},indent=2)+'\n')
