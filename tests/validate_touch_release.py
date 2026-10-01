"""Sequential hardware owners, bounded physics validation, then restore console."""
import hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results/ditto-emotes-0.6.0'
env=dict(os.environ,SCOUT_EVIDENCE_NAME=OUT.name)
results=[]
identity={'firmware_sha256':hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest(),
          'asset_sha256':hashlib.sha256((ROOT/'assets/avatar.a3d').read_bytes()).hexdigest()}
(OUT/'build-identity.json').write_text(json.dumps(identity,indent=2)+'\n')
try:
    for name,args in [('touch-hardware',['touch_hardware_check.py']),('acceptance',['hardware_acceptance.py']),('timer-navigation',['timer_navigation_check.py']),('touch-soak',['touch_soak.py','300'])]:
        (OUT/'validation-progress.json').write_text(json.dumps({'phase':name,'state':'running'})+'\n')
        with (OUT/(name+'.log')).open('w') as log:
            p=subprocess.run([sys.executable,str(ROOT/'tests'/args[0]),*args[1:]],env=env,stdout=log,stderr=subprocess.STDOUT)
        results.append({'phase':name,'exit_code':p.returncode})
        (OUT/'validation-phases.json').write_text(json.dumps(results,indent=2)+'\n')
        if p.returncode:raise RuntimeError(name+' failed')
        if name=='acceptance':shutil.copy2(ROOT/'test-results/acceptance.json',OUT/'acceptance.json')
    assert hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest()==identity['firmware_sha256'], 'Firmware artifact changed during validation'
    assert hashlib.sha256((ROOT/'assets/avatar.a3d').read_bytes()).hexdigest()==identity['asset_sha256'], 'Avatar artifact changed during validation'
    r=json.loads((OUT/'soak.json').read_text());b=r['baseline'];e=r['measurement_end'];n=e['frames']-b['frames'];duration=e['uptime_s']-b['uptime_s']
    m={**identity,'duration_s':duration,'frames':n,'average_fps':n/duration,'within25_percent':100*(e['frames_within25']-b['frames_within25'])/n,
       'over33ms':e['missed_frames']-b['missed_frames'],'errors':r['errors'],'unexpected_resets':r['unexpected_resets'],
       'physics_max_sample_ms':max(s['physics_ms'] for s in r['samples']),'audio':r['audio'],
       'touch_errors':e['touch_errors'],'touch_retries':e.get('touch_retries'),'two_hour_test':False,'physical_gestures_verified':False}
    m['performance_passed']=m['average_fps']>=29.7 and m['within25_percent']>=95 and m['over33ms']/n<=.01
    (OUT/'final-metrics.json').write_text(json.dumps(m,indent=2)+'\n')
    (OUT/'validation-progress.json').write_text(json.dumps({'phase':'validation','state':'completed','performance_passed':m['performance_passed']})+'\n')
    if not m['performance_passed']:raise RuntimeError('Touch animation performance gates failed; see final-metrics.json')
except BaseException as e:
    (OUT/'validation-progress.json').write_text(json.dumps({'phase':'validation','state':'failed','error':str(e)})+'\n')
    raise
finally:
    with (OUT/'bridge.log').open('a') as log:
        p=subprocess.Popen([sys.executable,str(ROOT/'host/bridge.py')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    (OUT/'bridge.pid').write_text(str(p.pid)+'\n')
