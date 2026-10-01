"""Native USB capture with concurrent render, speaker, mute and sleep checks."""
import array,json,math,subprocess,time,urllib.request,wave
from pathlib import Path
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results/mic-response-0.11.8/final-sustained';OUT.mkdir(exist_ok=True)
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=4))
def rpc(tool,args={}):
 return json.load(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'}),timeout=5))
original=status();rpc('display.wake');rpc('app.home');rpc('audio.microphone.set',{'muted':False});rpc('app.home');rows=[]
with scratch_directory('mic-soak',reserve_mib=8) as p:
 exe=p/'probe';subprocess.run(['clang++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
 with (OUT/'capture.log').open('w') as log:
  proc=subprocess.Popen([str(exe),str(p/'capture.pcm'),'32'],stdout=log,stderr=log)
  origin=time.monotonic()
  try:
   for sec in range(32):
    time.sleep(max(0,origin+sec-time.monotonic()))
    if sec in (2,15,26):rpc('audio.tone')
    if sec==8:print('mute request',time.monotonic()-origin,flush=True);rpc('audio.microphone.set',{'muted':True});rpc('app.home')
    if sec==11:print('unmute request',time.monotonic()-origin,flush=True);rpc('audio.microphone.set',{'muted':False});rpc('app.home')
    if sec==21:rpc('display.sleep')
    if sec==24:rpc('display.wake')
    rows.append({'t':time.monotonic()-origin,**status()})
   proc.wait(timeout=5)
  finally:
   if proc.poll() is None:proc.terminate();proc.wait()
   rpc('audio.microphone.set',{'muted':original['muted']});rpc('app.home');rpc('display.wake')
 raw=(p/'capture.pcm').read_bytes();pcm=array.array('h',raw)
 with wave.open(str(OUT/'capture.wav'),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(raw)
 mute=pcm[9*16000:10*16000]
 result={'version':rows[-1]['version'],'recorded_s':len(pcm)/16000,'muted_interval_peak':max(map(abs,mute)),'clipped_samples':sum(abs(v)>=32760 for v in pcm),'awake_recording_fps':(rows[20]['presented_frames']-rows[3]['presented_frames'])/(rows[20]['uptime_s']-rows[3]['uptime_s']),'counter_deltas':{k:rows[-1][k]-rows[0][k] for k in ['audio_overflows','audio_rx_errors','audio_tx_errors','audio_power_errors','usb_audio_drops','display_errors','usb_audio_packets']},'states':rows}
 (OUT/'results.json').write_text(json.dumps(result,indent=2))
 assert not any(result['counter_deltas'][k] for k in result['counter_deltas'] if k!='usb_audio_packets'),result['counter_deltas']
 assert result['muted_interval_peak']==0
 assert result['recorded_s']>30
 (OUT/'results.json').write_text(json.dumps(result,indent=2));print({k:v for k,v in result.items() if k!='states'})
