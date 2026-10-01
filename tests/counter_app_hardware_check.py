"""Qualify the optional Counter example after flashing its isolated build.

Does not install the example. Restore the verified base firmware afterward.
"""
from pathlib import Path
import sys,time,json,threading,argparse
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('example_source',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
sys.path.insert(0,str(args.example_source.resolve()/'host'))
from device import Device,DeviceError
from skills import execute,BY_NAME
from voice_transport import VoiceTransport
out=args.output;out.mkdir(parents=True,exist_ok=True);report={'passed':False,'checks':[],'physical_touch':False,'cloud_voice':False}
def record(label):report['checks'].append(label)
def save():(out/'counter-hardware.json').write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 try:
  time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(1);base=d.call('system.status');boot=d.boot_count
  report['baseline']=base;assert 'counter' in base,'Counter firmware is not installed'
  assert 'counter.set' in d.call('system.tools') and 'counter_set' in BY_NAME;record('firmware and host action catalogs expose counter')
  for value in [0,42,9999]:
   s=execute(d,'counter_set',{'value':value});assert s['counter']==value and s['app']=='counter'
   time.sleep(.45);d.screenshot(out/f'counter-{value}.png')
  record('0, 42 and 9999 accepted, observed and captured')
  for value in [-1,10000,1.5,True,'42',None]:
   try:execute(d,'counter_set',{'value':value})
   except DeviceError:pass
   else:raise AssertionError(f'Invalid counter accepted: {value!r}')
   assert d.call('system.status')['counter']==9999
  record('six invalid values rejected without changing counter')
  execute(d,'app_home',{});s=execute(d,'app_open',{'name':'counter'});assert s['counter']==9999 and s['app']=='counter';record('home and app navigation preserve value')
  d.call('timer.start',{'seconds':20});execute(d,'counter_set',{'value':42});s=d.call('system.status');assert s['timer_remaining']>0 and s['counter']==42;record('background timer remains active in counter')
  voice=VoiceTransport(d);voice.start();stop=threading.Event();packets=[];errors=[]
  def read():
   try:
    while not stop.is_set():
     data=voice.read_audio()
     if data:packets.append(len(data))
   except Exception as e:errors.append(repr(e))
  t=threading.Thread(target=read,daemon=True);t.start()
  try:
   for i in range(100):
    voice.write_audio(bytes(640))
    if i%10==0:assert execute(d,'counter_set',{'value':i})['counter']==i
    time.sleep(.02)
  finally:stop.set();t.join(1);voice.close()
  assert len(packets)>50 and not errors;report['mic_packets']=len(packets);record('counter updates during full-duplex silent PCM')
  d.call('timer.cancel');d.call('app.home');final=d.call('system.status');report['final']=final
  for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','scan_deadline_misses','te_timeouts']:assert final[k]==base[k],k
  assert d.boot_count==boot;record('one boot and no new display/audio/TE errors')
  report['passed']=True;report['logs']=d.logs;save()
 except Exception as e:report['error']=repr(e);report['logs']=d.logs;save();raise
