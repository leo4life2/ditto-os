"""Stable-format native Core Audio onset, wake transient, acoustic gain check."""
import argparse,array,json,math,re,subprocess,sys,time,urllib.request,wave
from pathlib import Path
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def rpc(tool):
 r=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':{}}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(r,timeout=5))
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=3))
keys=['version','sleeping','audio_sleeping','mic_rms','mic_raw_rms','mic_right_rms','mic_settling','mic_dsp_max_us','usb_first_audio_ms','usb_audio_starts','usb_audio_packets','usb_audio_drops','audio_overflows','audio_rx_errors','audio_power_errors','audio_tx_errors','presented_frames','display_errors','uptime_s','internal_free']
results=[]
with scratch_directory('mic-response',reserve_mib=8) as temp:
 exe=temp/'probe';subprocess.run(['clang++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
 for mode in ['awake','awake-repeat','sleep']:
  rpc('display.sleep' if mode=='sleep' else 'display.wake')
  if mode=='sleep':
   end=time.monotonic()+10
   while time.monotonic()<end:
    if status()['audio_sleeping']:break
    time.sleep(.25)
  else:time.sleep(.4)
  before=status();raw=temp/'capture.pcm'
  with (a.output/(mode+'.log')).open('w') as log:
   proc=subprocess.Popen([str(exe),str(raw),'5'],stdout=log,stderr=log)
   time.sleep(1)
   # Built-in speaker gives a fixed short stimulus at an unchanged volume.
   # This checks gain/transport, not far-field speech intelligibility.
   rpc('audio.tone');time.sleep(1);rpc('audio.tone');proc.wait(timeout=10)
  after=status();pcm=array.array('h',raw.read_bytes())
  with wave.open(str(a.output/(mode+'.wav')),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(pcm)
  log=(a.output/(mode+'.log')).read_text();first=float(re.search(r'result first=([\d.]+)',log)[1])
  windows=[]
  for i in range(0,len(pcm)-319,320):
   v=pcm[i:i+320];windows.append({'t':i/16000,'rms':math.sqrt(sum(x*x for x in v)/320),'peak':max(map(abs,v))})
  result={'mode':mode,'first_callback_s':first,'samples':len(pcm),'first_200ms_peak':max(map(abs,pcm[:3200])),'noise_rms':math.sqrt(sum(x*x for x in pcm[8000:12800])/4800),'max_20ms_rms':max(x['rms'] for x in windows),'clipped_samples':sum(abs(x)>=32760 for x in pcm),'before':{k:before.get(k) for k in keys},'after':{k:after.get(k) for k in keys},'windows':windows}
  results.append(result);print({k:v for k,v in result.items() if k not in ['before','after','windows']},flush=True)
  time.sleep(2)
(a.output/'native-results.json').write_text(json.dumps(results,indent=2));rpc('display.wake')
