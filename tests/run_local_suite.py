"""Run USB hardware checks serially and retain model-specific evidence."""
import json,shutil,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results/labubu-integration'
checks=[('acceptance','hardware_acceptance.py','acceptance.json'),('recovery','recovery_hardware_check.py',None),('persistence','persistence_idle.py','persistence-idle.json')]
results=[]
for name,script,result in checks:
    print('START',name,flush=True);started=time.monotonic()
    with (OUT/(name+'.log')).open('w') as log:
        completed=subprocess.run([sys.executable,str(ROOT/'tests'/script)],stdout=log,stderr=subprocess.STDOUT)
    if result and (ROOT/'test-results'/result).exists():shutil.copy2(ROOT/'test-results'/result,OUT/result)
    results.append({'name':name,'exit_code':completed.returncode,'seconds':round(time.monotonic()-started,1)})
    (OUT/'suite.json').write_text(json.dumps(results,indent=2)+'\n')
    print('FINISH',results[-1],flush=True)
    if completed.returncode:sys.exit(completed.returncode)
for name in ['home.png','brightness.png','wifi-edit.png','home-restored.png','device-tone-capture.wav']:
    if (ROOT/'test-results'/name).exists():shutil.copy2(ROOT/'test-results'/name,OUT/name)
