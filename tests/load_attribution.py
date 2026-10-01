"""Attribute real-device work without changing resolution, voice pose or input cadence.

Synthetic PCM/USB gestures; this is not acoustic or fingertip-latency evidence.
Run with exclusive USB ownership. The voice provider/API key is not used.
"""
from pathlib import Path
import argparse,json,math,statistics,sys,threading,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device

p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--seconds',type=float,default=12)
args=p.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
report={'synthetic_touch':True,'synthetic_pcm':True,'phases':{},'errors':[]}
def save():args.output.write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 time.sleep(3);d.sync_clock();d.call('app.home');d.call('voice.state.set',{'state':'listening'})
 boot=d.boot_count;stop=threading.Event()
 def heartbeat():
  while not stop.wait(2):
   try:d.call('voice.keepalive')
   except Exception as e:report['errors'].append(repr(e));return
 beat=threading.Thread(target=heartbeat,daemon=True);beat.start()
 try:
  # Identical injected drag cadence in every phase; an ending control exposes
  # drift from sensor pose, interaction history or temperature over the run.
  for name,mic,speaker,scan in [('control',False,False,False),('mic',True,False,False),
      ('speaker',False,True,False),('duplex',True,True,False),
      ('duplex_scan',True,True,True),('control_repeat',False,False,False)]:
   d.call('audio.capture.start' if mic else 'audio.capture.stop');d.call('audio.clear')
   finished=threading.Event();counts={'sent':0,'received':0};packets=d.subscribe('audio.input',256)
   def output():
    import base64
    pcm=base64.b64encode(bytes(640)).decode();deadline=time.monotonic()
    try:
     while not finished.is_set():
      d.call('audio.play',{'pcm':pcm});counts['sent']+=1;deadline+=.020
      finished.wait(max(0,deadline-time.monotonic()))
    except Exception as e:report['errors'].append(repr(e))
   thread=threading.Thread(target=output,daemon=True) if speaker else None
   if thread:thread.start()
   time.sleep(2)
   if scan:d.call('wifi.scan')
   before=d.call('system.status');power_before=d.call('debug.power');start=time.monotonic();next_sample=0;samples=[]
   try:
    while time.monotonic()-start<args.seconds:
     t=time.monotonic()-start
     d.call('debug.touch',{'down':int(t*2)%6!=5,'x':205+int(70*math.sin(t*3)),'y':290+int(45*math.sin(t*2))})
     if t>=next_sample:samples.append(d.call('system.status'));next_sample+=1
     while not packets.empty():packets.get_nowait();counts['received']+=1
     time.sleep(.018)
    after=d.call('system.status');power_after=d.call('debug.power')
   finally:
    finished.set()
    if thread:thread.join(3)
    d.unsubscribe(packets);d.call('debug.touch',{'down':False,'x':205,'y':290})
   previous={t['id']:t for t in power_before['tasks']};elapsed=power_after['uptime_us']-power_before['uptime_us']
   cpu={t['name']:100*((t['runtime_us']-previous[t['id']]['runtime_us'])%2**32)/elapsed for t in power_after['tasks'] if t['id'] in previous}
   fields=['frame_ms','frame_p95_ms','raster0_ms','raster1_ms','anim_ms','bin_ms','copy_ms','present_pixels','internal_free']
   r={'version':before['version'],'fps':(after['presented_frames']-before['presented_frames'])/(after['uptime_s']-before['uptime_s']),
      'cpu':cpu,'mean':{k:statistics.mean(s[k] for s in samples) for k in fields},'samples':samples,'before':before,'after':after,'packets':counts}
   errors={k:after[k]-before[k] for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','scan_deadline_misses','te_timeouts']}
   r['new_errors']=errors;report['phases'][name]=r;save()
   assert not any(errors.values()),errors
   assert d.boot_count==boot,'Unexpected reboot'
   if speaker:assert counts['sent']>args.seconds*40
   if mic:assert counts['received']>args.seconds*40
   print(name,json.dumps({'fps':r['fps'],'cpu':cpu,'mean':r['mean']}),flush=True)
  assert not report['errors'];report['passed']=True
 finally:
  stop.set();beat.join(3)
  d.call('audio.capture.stop');d.call('audio.clear');d.call('voice.state.set',{'state':'offline'});d.call('app.home')
  report['logs']=d.logs;save()
