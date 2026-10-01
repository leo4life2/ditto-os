"""Flash a sealed app while retaining the installed helper's enabled/TCC state.

The helper is suspended briefly after a read-only idle check. Its stale-input
watchdog releases modifiers on resume; the USB bridge is restarted either way.
"""
from pathlib import Path
import os,signal,subprocess,time,json,sys,urllib.request
import serial
from serial.tools import list_ports
from build_provenance import verify
ROOT=Path(__file__).resolve().parents[1];os.chdir(ROOT)
seal=verify();version=seal['version']
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/('test-results/flash-'+version)
out.mkdir(parents=True,exist_ok=True)
s=json.load(urllib.request.urlopen('http://127.0.0.1:8781/status'))
assert not s['usb_recording'] and not s['touch_down'],'Device is in use; defer flash.'
processes=subprocess.check_output(['ps','-axo','pid=,command='],text=True).splitlines()
helper_path=str(Path(os.environ.get('DITTO_APP_PATH','/Applications/Ditto Remote.app'))/'Contents/MacOS/ScoutRemote')
helpers=[]
for process in processes:
 parts=process.strip().split(None,1)
 if len(parts)==2 and (parts[1]==helper_path or parts[1].startswith(helper_path+' ')):
  helpers.append(int(parts[0]))
assert len(helpers)<=1,helpers
bridge=int(subprocess.check_output(['lsof','-t','-iTCP:8781','-sTCP:LISTEN'],text=True).strip())
bcmd=subprocess.check_output(['ps','-p',str(bridge),'-o','command='],text=True).strip();assert bcmd.endswith(' host/bridge.py')
for helper in helpers:os.kill(helper,signal.SIGSTOP)
try:
 os.kill(bridge,signal.SIGTERM);time.sleep(.4)
 ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x303a,0x4001)];assert len(ports)==1,ports
 port=serial.Serial(port=None,baudrate=1200,timeout=.1,exclusive=True);port.dtr=False;port.rts=False;port.port=ports[0];port.open();port.close()
 rom=[]
 for _ in range(50):
  rom=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x303a,0x1001)]
  if rom:break
  time.sleep(.1)
 assert len(rom)==1,rom
 with (out/('flash-'+version+'.log')).open('w') as log:
  subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','--port',rom[0],'--before','no-reset','--after','watchdog-reset','--connect-attempts','2','write-flash','--flash-mode','dio','--flash-freq','80m','--flash-size','32MB','0x10000','build/scout_os.bin'],stdout=log,stderr=subprocess.STDOUT,check=True)
 verify();(out/'build-identity.json').write_text(json.dumps({'version':version,'artifacts':seal['artifacts']},indent=2)+'\n')
 print('Flash/readback verified:',version,flush=True)
finally:
 with (out/'bridge.log').open('a') as log:
  p=subprocess.Popen([sys.executable,'host/bridge.py'],stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
 time.sleep(.5)
 for helper in helpers:os.kill(helper,signal.SIGCONT)
 print('Bridge restarted:',p.pid,'; existing helper resumed.',flush=True)
