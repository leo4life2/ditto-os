"""Short stage profile of real firmware; injected gestures are not finger latency."""
from pathlib import Path
import sys,time,json,threading,statistics,math,argparse,hashlib
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from voice_transport import VoiceTransport
from build_provenance import verify
from frame_budget import summarize
from audio_cadence import AudioCadence
def positive_seconds(value):
 value=float(value)
 if not math.isfinite(value) or value<=0:raise argparse.ArgumentTypeError('duration must be positive and finite')
 return value
parser=argparse.ArgumentParser();parser.add_argument('output',type=Path)
parser.add_argument('--idle-seconds',type=positive_seconds,default=12)
parser.add_argument('--drag-seconds',type=positive_seconds,default=12)
parser.add_argument('--mixed-seconds',type=positive_seconds,default=20)
args=parser.parse_args()
if args.output.exists():parser.error('Use a fresh output path; previous evidence will not be overwritten')
seal=verify();args.output.parent.mkdir(parents=True,exist_ok=True)
out={'phases':{},'errors':[],'physical_touch':False,'cloud_voice':False,'run_complete':False,
     'expected_version':seal['version'],'expected_local_app_sha256':seal['artifacts']['build/scout_os.bin'],
     'identity_scope':'Local sealed artifact and reported firmware version; no device runtime-image-hash endpoint',
     'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
     'host_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'host/device.py',ROOT/'host/voice_transport.py',ROOT/'host/skills.py',ROOT/'tools/frame_budget.py',ROOT/'tools/audio_cadence.py']},
     'requested_durations_s':{'idle':args.idle_seconds,'drag':args.drag_seconds,'mixed':args.mixed_seconds}}
def save():
 temp=args.output.with_suffix(args.output.suffix+'.tmp');temp.write_text(json.dumps(out,indent=2)+'\n');temp.replace(args.output)
save()
with Device() as d:
 voice=None;stop=threading.Event();th=[];boot=d.boot_count
 try:
  time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(3);boot=d.boot_count
  def sample_phase(name,seconds,touch=False):
   power_begin=d.call('debug.power');b=d.call('system.status');samples=[];start=time.monotonic();nextsample=0
   assert b['version']==seal['version'],'Device differs from sealed source'
   while time.monotonic()-start<seconds:
    t=time.monotonic()-start
    if touch:d.call('debug.touch',{'down':int(t*2)%6!=5,'x':205+int(70*math.sin(t*3)),'y':290+int(45*math.sin(t*2))})
    if t>=nextsample:samples.append(d.call('system.status'));nextsample+=1
    time.sleep(.018 if touch else .1)
   if touch:d.call('debug.touch',{'down':False,'x':205,'y':290})
   e=d.call('system.status');dt=e['uptime_s']-b['uptime_s'];keys=['frame_ms','frame_p95_ms','physics_ms','anim_ms','bin_ms','draw_ms','raster0_ms','raster1_ms','compose_ms','upload_ms','copy_ms','diff_ms','dma_wait_ms','present_ms','internal_free']
   report={'start':b,'end':e,'samples':samples,'presented_fps':(e['presented_frames']-b['presented_frames'])/dt,'mean':{k:statistics.mean(s[k] for s in samples) for k in keys}}
   report['frame_budget']=summarize(b,e)
   report['mean_scope']='Sampled stage means; frame_p95_ms is the mean of rolling p95 snapshots, not a whole-window percentile'
   power_end=d.call('debug.power');old={t['id']:t for t in power_begin['tasks']};elapsed=power_end['uptime_us']-power_begin['uptime_us']
   report['cpu_percent_one_core']={t['name']:100*((t['runtime_us']-old[t['id']]['runtime_us'])%2**32)/elapsed for t in power_end['tasks'] if t['id'] in old}
   report['new_errors']={k:e[k]-b[k] for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','scan_deadline_misses','te_timeouts']}
   if any(report['new_errors'].values()):out['errors'].append({name:report['new_errors']})
   if d.boot_count!=boot:out['errors'].append({name:'Unexpected reboot'})
   report['frames_within25_percent']=100*(e['frames_within25']-b['frames_within25'])/max(1,e['frames']-b['frames'])
   report['speaker_blocks_per_second']=(e['audio_output_blocks']-b['audio_output_blocks'])/dt
   out['phases'][name]=report;save();print(name,json.dumps({'presented_fps':report['presented_fps'],'mean':report['mean'],'cpu_percent_one_core':report['cpu_percent_one_core'],'new_errors':report['new_errors']}),flush=True)
  sample_phase('idle',args.idle_seconds);sample_phase('drag',args.drag_seconds,True)
  voice=VoiceTransport(d);voice.start();stop=threading.Event();cadence=AudioCadence();audio={'mic_packets':0,'speaker_packets':0}
  def mic():
   try:
    while not stop.is_set():
     if voice.read_audio():audio['mic_packets']+=1
   except Exception as e:out['errors'].append(repr(e))
  def speaker():
   begin=deadline=time.monotonic()
   try:
    while not stop.is_set():
     started=time.monotonic()
     assert voice.write_audio(bytes(640)), 'Playback canceled'
     now=time.monotonic();audio['speaker_packets']+=1
     cadence.observe(deadline,started,now)
     deadline+=.020
     # 320 samples at 16 kHz means one packet each 20 ms INCLUDING USB time.
     # Sleeping after the blocking round trip silently underfed older profiles.
     stop.wait(max(0,deadline-time.monotonic()))
   except Exception as e:out['errors'].append(repr(e))
   finally:
    audio.update(cadence.summary())
    audio['elapsed_s']=time.monotonic()-begin
    audio['speaker_packets_per_second']=audio['speaker_packets']/audio['elapsed_s']
  th=[threading.Thread(target=f,daemon=True) for f in [mic,speaker]]
  for t in th:t.start()
  try:d.call('wifi.scan');sample_phase('mixed',args.mixed_seconds,True)
  finally:
   stop.set()
   for t in th:t.join(9)
   voice.close();voice=None;d.call('app.home')
  out['audio_transport']=audio
  if not audio.get('delivery_timing_met',False):out['errors'].append('PCM delivery timing budget exceeded; inspect audio_transport outliers')
  audio['mic_packets_per_second']=audio['mic_packets']/max(.001,audio.get('elapsed_s',0))
  if audio['mic_packets_per_second']<40:out['errors'].append('Microphone transport did not maintain packet delivery')
  if audio.get('speaker_packets_per_second',0)<49:out['errors'].append('Speaker source did not maintain nominal 16 kHz PCM')
  if out['phases']['mixed']['speaker_blocks_per_second']<49:out['errors'].append('Device playback did not sustain nominal 16 kHz PCM')
  out['boot_count_start']=boot;out['boot_count_end']=d.boot_count;out['logs']=d.logs;out['run_complete']=True;save()
  assert boot==d.boot_count,'Unexpected reboot'
  assert not out['errors'],out['errors']
 except BaseException as error:
  out['errors'].append(type(error).__name__+': '+str(error));save()
  raise
 finally:
  stop.set()
  for thread in th:
   thread.join(9)
   if thread.is_alive():out['errors'].append('Audio worker did not stop during cleanup')
  try:
   if voice is not None:voice.close()
   if not d.failure and d.boot_count==boot:
    d.call('debug.touch',{'down':False,'x':205,'y':290});d.call('app.home')
  except Exception as error:out['errors'].append('cleanup: '+repr(error))
  out['boot_count_end']=d.boot_count;save()
assert not out['errors'],out['errors']
