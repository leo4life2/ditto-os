"""Stage, sign, test and atomically update the local Ditto menu-bar helper."""
import os,plistlib,subprocess,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'dist/macos'
OUT=Path(os.environ.get('DITTO_APP_PATH','/Applications/Ditto Remote.app'));STAGE=BASE/'.stage/Ditto Remote.app';OLD=BASE/'.rollback/Ditto Remote.app'
if STAGE.exists():shutil.rmtree(STAGE)
exe=STAGE/'Contents/MacOS/ScoutRemote';exe.parent.mkdir(parents=True,exist_ok=True)
resources=STAGE/'Contents/Resources';resources.mkdir()
for name in ['AppIcon.icns','AppIcon.png']:shutil.copy2(ROOT/'host/mac-remote/Assets'/name,resources/name)
(resources/'host').mkdir()
for file in (ROOT/'host').glob('*.py'):shutil.copy2(file,resources/'host'/file.name)
shutil.copy2(ROOT/'host/console.html',resources/'host/console.html')
info={'CFBundleIdentifier':'local.scout.remote','CFBundleName':'Ditto Remote','CFBundleDisplayName':'Ditto Remote',
 'CFBundleExecutable':'ScoutRemote','CFBundlePackageType':'APPL','CFBundleVersion':'12','CFBundleShortVersionString':'0.3.0',
 'NSHighResolutionCapable':True,'NSPrincipalClass':'NSApplication','LSUIElement':True,'CFBundleIconFile':'AppIcon',
 'NSBluetoothAlwaysUsageDescription':'Connect to your Ditto device for wireless microphone audio, hold-to-dictate, double-tap Enter, and shake-to-undo.',
 'ScoutPython':sys.executable}
(STAGE/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
obj=BASE/'.stage/WirelessAudio.o'
subprocess.run(['clang++','-std=c++17','-O2','-c',str(ROOT/'host/mac-remote/WirelessAudio.mm'),'-o',str(obj)],check=True)
sources=['Gestures.swift','Feedback.swift','Focus.swift','Bluetooth.swift','BluetoothProbe.swift','Menu.swift','main.swift']
subprocess.run(['swiftc','-O','-import-objc-header',str(ROOT/'host/mac-remote/WirelessAudio.h'),str(obj),'-lc++','-framework','AudioToolbox','-framework','CoreAudio','-framework','Cocoa','-framework','ApplicationServices','-framework','CoreBluetooth','-framework','Carbon',*[str(ROOT/'host/mac-remote'/f) for f in sources],'-o',str(exe)],check=True)
obj.unlink()
subprocess.run(['codesign','--force','--sign','-',str(STAGE)],check=True)
subprocess.run(['codesign','--verify','--strict','--verbose=2',str(STAGE)],check=True)
subprocess.run([str(exe),'--self-test'],check=True)
if "--stage-only" in sys.argv:
 print(STAGE);sys.exit(0)
# Do not replace the bundle of a running helper. --stage-only never installs.
running=subprocess.run(['pgrep','-f','^'+str(OUT/'Contents/MacOS/ScoutRemote')+'$'],capture_output=True).returncode==0
if running:raise SystemExit('Quit Ditto Remote before installing. The verified staged app is at '+str(STAGE))
if OLD.exists():shutil.rmtree(OLD)
OLD.parent.mkdir(exist_ok=True)
if OUT.exists():OUT.rename(OLD)
STAGE.rename(OUT)
print(OUT)
