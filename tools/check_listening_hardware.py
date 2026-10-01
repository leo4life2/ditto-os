"""Bounded, capture-free USB microphone/listening performance check."""
from pathlib import Path
import sys,subprocess,time,json,urllib.request
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
label=sys.argv[1];out=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else ROOT/'test-results/status-compact-0.11.16'
out.mkdir(parents=True,exist_ok=True)
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=3))
def act(tool,**args):
 req=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':tool,'arguments':args}).encode(),headers={'Content-Type':'application/json'})
 return json.load(urllib.request.urlopen(req,timeout=3))
initial=status();assert not initial['usb_recording'],'Finish the current recording before running this check.'
assert initial['conversation'] in ('idle','offline')
rows=[]
with scratch_directory('listening-hardware',reserve_mib=24) as tmp:
 exe=Path(tmp)/'mic'
 subprocess.run(['c++','-std=c++17','-O2',str(ROOT/'tools/mic_probe.cpp'),'-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation','-o',str(exe)],check=True)
 act('display.wake');act('app.home')
 if label=='baseline':act('voice.state.set',state='listening')
 try:
  with (out/(label+'-audio.log')).open('w') as log:
   capture=subprocess.Popen([str(exe),'/dev/null','23'],stdout=log,stderr=log)
   time.sleep(1)
   start=time.monotonic()
   while time.monotonic()-start<20:
    if label=='baseline':act('voice.keepalive')
    rows.append(status());time.sleep(.4)
   if len(sys.argv)>3 and sys.argv[3]=='capture':
    # Outside the measured interval, while the audio client is still open.
    req=urllib.request.Request('http://127.0.0.1:8781/capture',data=b'{}')
    urllib.request.urlopen(req,timeout=15).read()
    (out/(label+'-device.png')).write_bytes(urllib.request.urlopen('http://127.0.0.1:8781/frame.png',timeout=4).read())
   capture.wait(timeout=6);assert capture.returncode==0
 finally:
  if label=='baseline':act('voice.state.set',state=initial['conversation'])
summary={'version':initial['version'],'seconds':rows[-1]['uptime_s']-rows[0]['uptime_s'],'fps':(rows[-1]['presented_frames']-rows[0]['presented_frames'])/(rows[-1]['uptime_s']-rows[0]['uptime_s']),'clip_names':list(sorted(set(r['clip'] for r in rows))),'usb_recording_all':all(r['usb_recording'] for r in rows),'errors':{k:rows[-1][k]-rows[0][k] for k in ('usb_audio_drops','audio_rx_errors','audio_tx_errors','audio_overflows','display_errors','scan_deadline_misses','te_timeouts')},'internal_free_min':min(r['internal_free'] for r in rows),'mean_bin_ms':sum(r['bin_ms'] for r in rows)/len(rows),'mean_draw_ms':sum(r['draw_ms'] for r in rows)/len(rows)}
(out/(label+'-performance.json')).write_text(json.dumps({'summary':summary,'samples':rows},indent=2)+'\n')
print(json.dumps(summary,indent=2))
