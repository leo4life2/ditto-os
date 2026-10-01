"""Focused checks, two-minute repeated-tap/audio/Wi-Fi load, restore USB bridge."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results'/os.environ.get('SCOUT_EVIDENCE_NAME','ditto-sharp-taps-0.6.1')
env=dict(os.environ,SCOUT_EVIDENCE_NAME=OUT.name,SCOUT_TOUCH_PATTERN='taps')
identity={'firmware_sha256':hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest(),'asset_sha256':hashlib.sha256((ROOT/'assets/avatar.a3d').read_bytes()).hexdigest()}
(OUT/'build-identity.json').write_text(json.dumps(identity,indent=2)+'\n');results=[]
try:
    for name,args in [('sharp-tap-check',['sharp_tap_check.py']),('tap-load',['touch_soak.py',os.environ.get('SCOUT_SOAK_SECONDS','120')])]:
        (OUT/'validation-progress.json').write_text(json.dumps({'phase':name,'state':'running'})+'\n')
        with (OUT/(name+'.log')).open('w') as f:p=subprocess.run([sys.executable,str(ROOT/'tests'/args[0]),*args[1:]],env=env,stdout=f,stderr=subprocess.STDOUT)
        results.append({'phase':name,'exit_code':p.returncode});(OUT/'validation-phases.json').write_text(json.dumps(results,indent=2)+'\n')
        if name=='tap-load' and (OUT/'soak.json').exists():
            subprocess.run([sys.executable,str(ROOT/'tools/summarize_touch_run.py')],env=env,check=True)
        if p.returncode:raise RuntimeError(name+' failed; evidence retained')
    m=json.loads((OUT/'final-metrics.json').read_text())
    (OUT/'validation-progress.json').write_text(json.dumps({'phase':'complete','state':'completed','performance_passed':m['performance_passed']})+'\n')
finally:
    with (OUT/'bridge.log').open('a') as f:p=subprocess.Popen([sys.executable,str(ROOT/'host/bridge.py')],stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
    (OUT/'bridge.pid').write_text(str(p.pid)+'\n')
