"""Bounded BLE qualification; never injects keyboard events. Retains metrics only."""
from pathlib import Path
import os,subprocess,json,urllib.request,argparse
ROOT=Path(__file__).resolve().parents[1];os.chdir(ROOT)
OUT=ROOT/'test-results/wireless-battery-0.11.51'
OUT.mkdir(parents=True,exist_ok=True)
installed=Path(os.environ.get('DITTO_APP_PATH','/Applications/Ditto Remote.app'))/'Contents/MacOS/ScoutRemote'
staged=ROOT/'dist/macos/.stage/Ditto Remote.app/Contents/MacOS/ScoutRemote'
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=5))
parser=argparse.ArgumentParser();parser.add_argument('seconds',nargs='?',type=float,default=30)
parser.add_argument('--audio',action='store_true');parser.add_argument('--reboot',action='store_true')
parser.add_argument('--awake',action='store_true')
parser.add_argument('--label');args=parser.parse_args();assert 5<=args.seconds<=120
before=status();assert not before['usb_recording'] and not before['touch_down'] and not before['remote_dictating']
# A suspended CoreBluetooth client still owns its GATT connection. Never
# SIGSTOP a live helper and test a second client against the same peripheral.
helpers=[]
for row in subprocess.check_output(['ps','-axo','pid=,command='],text=True).splitlines():
 p=row.strip().split(None,1)
 if len(p)==2 and (p[1]==str(installed) or p[1].startswith(str(installed)+' ')):helpers.append(int(p[0]))
assert not helpers,'Quit the running Ditto helper before an isolated BLE qualification.'
assert not (args.awake and args.reboot),'Use an awake performance run separately from a reboot test.'
if args.awake:
 req=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':'display.wake','arguments':{}}).encode(),headers={'Content-Type':'application/json'})
 urllib.request.urlopen(req,timeout=5).read()
if args.reboot:
 assert before['uptime_s']>=35,'Let the 30-second radio startup recovery guard settle before a deliberate reboot.'
 req=urllib.request.Request('http://127.0.0.1:8781/action',data=json.dumps({'tool':'system.reboot','arguments':{}}).encode(),headers={'Content-Type':'application/json'})
 urllib.request.urlopen(req,timeout=5).read()
command=[str(staged if staged.exists() else installed),'--bluetooth-probe',str(args.seconds)]
if args.audio:command+=['--audio']
mode=args.label or ('audio' if args.audio else 'control')
assert mode.replace('-','').replace('_','').isalnum()
with (OUT/(mode+'-probe.jsonl')).open('w') as f:
 result=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=args.seconds+20)
after=status()
metrics={'before':before,'after':after,'returncode':result.returncode,'reboot':args.reboot}
(OUT/(mode+'-device.json')).write_text(json.dumps(metrics,indent=2)+'\n')
print('probe status',result.returncode)
for k in ['uptime_s','bluetooth_audio_packets','bluetooth_audio_drops','bluetooth_controls','bluetooth_notify_errors','bluetooth_disconnects','internal_free','internal_low_water','render_fps','display_errors','audio_rx_errors','audio_overflows']:
 print(k,before.get(k),after.get(k))
raise SystemExit(result.returncode)
