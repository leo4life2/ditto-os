"""Verify the real USB input using a bounded firmware ramp and mute transitions.

Uses native CoreAudio HAL, independent of Flow and AudioQueue conversion. The
firmware diagnostic expires after 10 seconds and honors microphone mute.
"""
import argparse,array,collections,json,re,subprocess,time,urllib.request,wave
from pathlib import Path
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('label');args=p.parse_args()
assert all(c.isalnum() or c in '-_' for c in args.label)
OUT=ROOT/'test-results/mic-response-0.11.8'/args.label;OUT.mkdir(exist_ok=True)
def state():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=5))
def rpc(tool,arguments={}):
 return json.load(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':arguments}).encode(),headers={'Content-Type':'application/json'}),timeout=5))
original=state();rpc('display.wake');rpc('audio.microphone.set',{'muted':False});rpc('app.home')
with scratch_directory('usb-pcm',reserve_mib=8) as d:
 exe=d/'probe';subprocess.run(['clang++','-O2','-std=c++17',str(ROOT/'tools/mic_hal_probe.cpp'),'-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
 with (OUT/'capture.log').open('w') as log:
  proc=subprocess.Popen([str(exe),str(d/'capture.pcm'),'12'],stderr=log);start=time.monotonic();origin_mono=time.clock_gettime(time.CLOCK_MONOTONIC);events=[]
  try:
   for at,tool,a in [(1,'debug.usb_pattern',{}),(4,'audio.microphone.set',{'muted':True}),(7,'audio.microphone.set',{'muted':False})]:
    time.sleep(max(0,start+at-time.monotonic()));requested=time.monotonic()-start;rpc(tool,a);events.append({'requested_s':requested,'completed_s':time.monotonic()-start,'tool':tool,'state':state()})
   proc.wait(timeout=15)
  finally:
   if proc.poll() is None:proc.terminate();proc.wait()
   rpc('audio.microphone.set',{'muted':original['muted']});rpc('app.home')
 raw=(d/'capture.pcm').read_bytes();pcm=array.array('h',raw)
 with wave.open(str(OUT/'capture.wav'),'wb') as w:w.setparams((1,2,16000,0,'NONE',''));w.writeframes(raw)
 a=pcm[2*16000:3*16000];delta=collections.Counter((b-a)%8192 for a,b in zip(a,a[1:]))
 result={'version':original['version'],'seconds':len(pcm)/16000,'muted_peak':max(map(abs,pcm[5*16000:6*16000])),'ramp_delta_histogram':delta.most_common(12),'ramp_sequential_fraction':delta[1]/(len(a)-1),'events':events,'origin_mono':origin_mono,'final':state()}
 log=(OUT/'capture.log').read_text();capture_origin=float(re.search(r'origin_mono ([\d.]+)',log)[1]);offset=capture_origin-origin_mono
 blocks=[];pos=0
 for m in re.finditer(r'block ([\d.]+) (\d+) ',log):
  n=int(m[2]);blocks.append((pos,pos+n,offset+float(m[1])));pos+=n
 mute_done=events[1]['completed_s'];unmute_start=events[2]['requested_s'];last_audio=None
 for lo,hi,at in blocks:
  if at>mute_done and at<unmute_start and any(pcm[lo:hi]):last_audio=at
 result['mute_tail_ms']=max(0,1000*(last_audio-mute_done)) if last_audio else 0
 result['capture_origin_offset_s']=offset
 (OUT/'results.json').write_text(json.dumps(result,indent=2));print({k:v for k,v in result.items() if k not in ('events','final')})
