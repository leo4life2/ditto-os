import json,time,urllib.request,urllib.error,uuid
from pathlib import Path
from build_provenance import verify
root=Path(__file__).resolve().parents[1];out=root/'test-results/status-spacing-0.11.27'
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=4))
def act(tool,**args):
 return json.load(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'}),timeout=4))
def capture(name):
 urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/capture',data=b'{}'),timeout=15).read()
 (out/name).write_bytes(urllib.request.urlopen('http://127.0.0.1:8781/frame.png').read())
s=status();assert not s['touch_down'] and not s['usb_recording']
report={'before':s,'artifact':verify()['artifacts']['build/scout_os.bin'],'actions':[]}
client=str(uuid.uuid4());allowed={'Yes','Celebrate','Giggle','Happy','Surprise','Wink','Thinking'}
try:
 act('display.wake');act('app.home');act('voice.state.set',state='offline');time.sleep(.2)
 for i,kind in enumerate(('end','enter','undo'),1):
  ack=act('input.remote',dictating=False,feedback={'client':client,'seq':i,'kind':kind})
  time.sleep(.25);s=status();assert ack['feedback_ack']==i and s['clip'] in allowed
  report['actions'].append({'kind':kind,'clip':s['clip']})
 count=s['remote_reaction_serial']
 for bad in ({'seq':0},{'seq':-1},{'seq':1.5},{'seq':'one'},{'kind':'sad'},{'client':'x'*41}):
  payload={'client':client,'seq':4,'kind':'enter',**bad}
  try:act('input.remote',feedback=payload)
  except urllib.error.HTTPError as e:assert e.code==400
  else:raise AssertionError('Invalid feedback accepted')
 assert status()['remote_reaction_serial']==count
 start=time.monotonic();wave=None
 while time.monotonic()-start<55:
  s=status()
  if s['clip']=='Wave':
   time.sleep(.85);capture('device-final-wave.png');wave=status();break
  time.sleep(.3)
 assert wave is not None,'No wave while voice service offline'
 report['offline_wave']=wave
finally:
 act('voice.state.set',state='idle');act('display.wake');act('app.home');time.sleep(.3)
 report['after']=status();capture('device-final-home.png')
 (out/'final-device-check.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: final sealed build; three action types while offline; malformed feedback rejected; offline idle wave; returned home',flush=True)
