"""Measure native USB microphone startup without Wispr's own capture pipeline."""
import argparse,array,json,math,subprocess,time,urllib.request,wave,threading
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--trials',type=int,default=3);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def rpc(tool):
 r=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':{}}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(r,timeout=5))
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=3))
rate=16000
tone=array.array('h',(round(4500*min(1,i/480)*math.sin(2*math.pi*997*i/rate)) for i in range(rate*9)))
with wave.open(str(a.output/'onset-stimulus.wav'),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(tone)
results=[]
for trial in range(a.trials):
 mode='asleep' if trial%2 else 'awake';rpc('display.sleep' if mode=='asleep' else 'display.wake');time.sleep(.8)
 before=status();polls=[];stop=threading.Event();start=time.monotonic()
 def poll():
  while not stop.is_set():
   try:
    s=status();polls.append({'t':time.monotonic()-start,**{k:s.get(k) for k in ['usb_recording','audio_sleeping','audio_input_blocks','usb_audio_packets','usb_audio_drops','mic_rms']}})
   except Exception:pass
   stop.wait(.04)
 player=subprocess.Popen(['afplay','-v','0.6',str(a.output/'onset-stimulus.wav')]);time.sleep(.6)
 start=time.monotonic();thread=threading.Thread(target=poll);thread.start()
 with (a.output/f'{trial}-{mode}.log').open('w') as log:
  proc=subprocess.Popen(['ffmpeg','-hide_banner','-probesize','32','-analyzeduration','0','-f','avfoundation','-i',':Ditto Microphone','-t','5','-ac','1','-ar','16000','-c:a','pcm_s16le','-f','s16le','-flush_packets','1','pipe:1'],stdout=subprocess.PIPE,stderr=log)
  chunks=[];first=None
  while True:
   chunk=proc.stdout.read1(4096)
   if not chunk:break
   if first is None:first=time.monotonic()-start
   chunks.append(chunk)
  proc.wait(timeout=12)
 stop.set();thread.join();player.terminate();player.wait()
 raw=b''.join(chunks);pcm=array.array('h',raw)
 with wave.open(str(a.output/f'{trial}-{mode}.wav'),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(raw)
 windows=[]
 for i in range(0,len(pcm)-319,320):
  x=pcm[i:i+320];windows.append({'t':i/rate,'rms':math.sqrt(sum(v*v for v in x)/len(x)),'zero':sum(v==0 for v in x)/len(x)})
 r={'mode':mode,'before':{k:before.get(k) for k in ['sleeping','usb_recording','audio_sleeping','version']},'first_pcm_wall_s':first,'first_nonzero_sample_s':next((i/rate for i,v in enumerate(pcm) if v!=0),None),'first_signal_20ms_s':next((w['t'] for w in windows if w['rms']>30),None),'rms':math.sqrt(sum(v*v for v in pcm)/max(1,len(pcm))),'peak':max(map(abs,pcm),default=0),'windows':windows,'polls':polls}
 results.append(r);print({k:v for k,v in r.items() if k not in ['windows','polls']},flush=True)
 time.sleep(1)
(a.output/'startup.json').write_text(json.dumps(results,indent=2))
rpc('display.wake')
