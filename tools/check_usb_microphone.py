"""Acoustic loopback: Mac speaker test tones -> physical Ditto mic -> native USB."""
import argparse,array,json,math,subprocess,time,urllib.request,wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'test-results'/('usb-mic-'+(ROOT/'VERSION').read_text().strip()))
OUT=parser.parse_args().output;OUT.mkdir(parents=True,exist_ok=True)
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=3))
rate=16000;samples=array.array('h')
for hz in [700,1100,1700]:
 for i in range(rate):
  t=i/rate;env=min(1,t/.03,(1-t)/.03)
  samples.append(round(4500*env*math.sin(2*math.pi*hz*t)))
with wave.open(str(OUT/'acoustic-stimulus.wav'),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(samples)
before=status()
with (OUT/'acoustic-capture.log').open('w') as log:
 p=subprocess.Popen(['ffmpeg','-hide_banner','-f','avfoundation','-i',':Ditto Microphone','-t','7','-c:a','pcm_s16le','-y',str(OUT/'acoustic-capture.wav')],stdout=log,stderr=log)
 time.sleep(1.5)
 subprocess.run(['afplay','-v','0.6',str(OUT/'acoustic-stimulus.wav')],check=True)
 p.wait(timeout=15)
after=status()
with wave.open(str(OUT/'acoustic-capture.wav')) as w:a=array.array('h',w.readframes(w.getnframes()))
def band(hz):
 # Peak half-second Fourier magnitude; tone frequency is unique to test.
 best=0
 for start in range(0,len(a)-8000,1600):
  re=im=0
  for i,v in enumerate(a[start:start+8000]):
   angle=2*math.pi*hz*i/rate;re+=v*math.cos(angle);im+=v*math.sin(angle)
  best=max(best,2*math.hypot(re,im)/8000)
 return best
result={'samples':len(a),'seconds':len(a)/rate,'peak':max(abs(v) for v in a),'rms':math.sqrt(sum(v*v for v in a)/len(a)),
 'tone_amplitudes':{str(f):band(f) for f in [700,1100,1700]},'off_band_amplitudes':{str(f):band(f) for f in [500,900,1400]},
 'before':{k:before[k] for k in ['version','uptime_s','presented_frames','usb_audio_packets','usb_audio_drops','audio_overflows','display_errors']},
 'after':{k:after[k] for k in ['version','uptime_s','presented_frames','usb_audio_packets','usb_audio_drops','audio_overflows','display_errors']}}
(OUT/'acoustic-check.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
