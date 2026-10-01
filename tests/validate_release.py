"""One serial owner at a time: functional checks, sustained load, then console.

Completing this runner does not declare product acceptance. Its report separates
functional/reliability results, performance gates and untested credentials/voice.
"""
import argparse,datetime,hashlib,json,shutil,statistics,subprocess,sys,time,urllib.request
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from validation_context import evidence_directory
ROOT=Path(__file__).resolve().parents[1];OUT=evidence_directory(ROOT);OUT.mkdir(exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--soak-seconds',type=int,default=7200);args=parser.parse_args()
results=[]
def phase(label,state,**details):
    record={'phase':label,'state':state,'time':datetime.datetime.now().astimezone().isoformat(),**details}
    (OUT/'validation-progress.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record),flush=True)
def keep(*names):
    for name in names:
        p=ROOT/'test-results'/name
        if p.exists():shutil.copy2(p,OUT/name)
def run(name,script,arguments=(),evidence=()):
    phase(name,'running');start=time.monotonic()
    with (OUT/(name+'.log')).open('w') as log:
        p=subprocess.run([sys.executable,str(ROOT/'tests'/script),*arguments],stdout=log,stderr=subprocess.STDOUT)
    keep(*evidence);result={'name':name,'exit_code':p.returncode,'seconds':round(time.monotonic()-start,1)};results.append(result)
    (OUT/'validation-phases.json').write_text(json.dumps(results,indent=2)+'\n')
    phase(name,'passed' if p.returncode==0 else 'failed',**result)
    if p.returncode:raise RuntimeError(f'{name} failed; inspect its log')
def identity():
    d=json.loads((ROOT/'build/project_description.json').read_text())
    out={'project':d['project_name'],'version':d['project_version'],'target':d['target'],'recorded_at':datetime.datetime.now().astimezone().isoformat(),'files':{}}
    for n in ['build/scout_os.bin','build/bootloader/bootloader.bin','build/partition_table/partition-table.bin','assets/avatar.a3d','main/ui.cpp','main/services.cpp','main/scout.h','sdkconfig','host/device.py','host/connection.py','host/voice_transport.py','tests/soak.py','components/a3d/src/backends/soft/a3d_soft_backend.h']:
        b=(ROOT/n).read_bytes();out['files'][n]={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    (OUT/'build-identity.json').write_text(json.dumps(out,indent=2)+'\n');return out
try:
    ident=identity()
    run('voice-interruption','voice_interrupt_check.py')
    run('voice-session','voice_session_check.py')
    run('host-recovery','recovery_host_check.py')
    run('acceptance','hardware_acceptance.py',evidence=('acceptance.json','home.png','brightness.png','wifi-edit.png','device-tone-capture.wav'))
    run('timer-navigation','timer_navigation_check.py')
    run('audio-interruption-hardware','audio_interrupt_hardware_check.py')
    run('recovery','recovery_hardware_check.py')
    run('persistence','persistence_idle.py',evidence=('persistence-idle.json','home-restored.png'))
    run('layout','layout_check.py',evidence=('layout-profile.json','layered-home.png','layered-volume.png','layered-brightness.png','layered-wifi.png'))
    run('soak-two-hour','soak.py',(str(args.soak_seconds),))
    soak=json.loads((ROOT/'test-results/soak.json').read_text())
    (OUT/'soak-two-hour.json').write_text(json.dumps(soak,indent=2)+'\n')
    keep('home-final.png','expression-neutral.png','expression-curious.png','expression-sad.png','expression-angry.png')
    b,e=soak['baseline'],soak['measurement_end'];n=e['frames']-b['frames'];seconds=e['uptime_s']-b['uptime_s'];samples=soak['samples']
    count=max(1,len(samples)//8);settled=samples[min(60,len(samples)//4):];early=settled[:count];late=samples[-count:]
    memory={}
    for k in ('internal_free','psram_free'):
        memory[k]={'min':min(s[k] for s in samples),'early_median':statistics.median(s[k] for s in early),'late_median':statistics.median(s[k] for s in late)}
        memory[k]['median_change']=memory[k]['late_median']-memory[k]['early_median']
    under25=(e['frames_within25']-b['frames_within25'])/n*100
    over33=e['missed_frames']-b['missed_frames'];fps=n/seconds
    metrics={'duration_s':seconds,'frames':n,'average_fps':fps,'frames_within25_percent':under25,'frames_over33_333ms':over33,'frames_within33_333_percent':100*(1-over33/n),'max_sampled_rolling_p95_ms':max(s['frame_p95_ms'] for s in samples),'command_p95_ms':soak['command_latency_p95_ms'],'audio':soak['audio'],'memory':memory,'errors':soak['errors'],'unexpected_resets':soak['unexpected_resets'],'requested_duration_completed':soak['completed_requested_duration'],'two_hours_completed':seconds>=7200,'gates':{'30_fps_with_1_percent_tolerance':fps>=29.7,'95_percent_within25ms':under25>=95,'99_percent_within33_333ms':over33/n<=.01,'secured_wifi_verified':False,'cloud_conversation_verified':False},'firmware_sha256':ident['files']['build/scout_os.bin']['sha256']}
    assert hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest()==metrics['firmware_sha256'],'Firmware artifact changed during the run'
    (OUT/'final-metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
    phase('console','starting')
    log=(OUT/'bridge.log').open('a')
    bridge=subprocess.Popen([sys.executable,str(ROOT/'host/bridge.py')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    (OUT/'bridge.pid').write_text(str(bridge.pid)+'\n')
    for i in range(30):
        if bridge.poll() is not None:raise RuntimeError('Console could not start')
        try:
            with urllib.request.urlopen('http://127.0.0.1:8781/status',timeout=3) as r:json.load(r)
            break
        except Exception:time.sleep(.5)
    else:raise RuntimeError('Console did not connect to the board')
    run('bridge-check','bridge_check.py',evidence=('bridge-check.json','console-frame.png'))
    report=f'''# Current engineering validation

Firmware version {ident['version']}; asset: {json.loads((ROOT/'assets/avatar-manifest.json').read_text())['id']}. This report records executed checks, not a declaration that the overall product is finished.

- Functional, recovery, persistence and layout phases: passed.
- Mixed workload duration: {seconds:.1f} seconds; two-hour requirement: {metrics['two_hours_completed']}.
- Native-resolution average: {fps:.2f} FPS over {n:,} frames.
- Frames within 25 ms: {under25:.2f}%; required stricter headroom target: 95%.
- Frames over 33.333 ms: {over33:,} ({100*over33/n:.3f}%).
- Display/audio errors and unexpected resets: see the counters and assertions in `{OUT.relative_to(ROOT)}/soak-two-hour.json`.
- Heap early/late medians and minimum free bytes: `{OUT.relative_to(ROOT)}/final-metrics.json`.
- Firmware SHA-256: `{metrics['firmware_sha256']}`.

The current console is at http://127.0.0.1:8781. It uses the same shared actions, synchronizes Mac time, recovers USB connections and captures the actual device framebuffer.

Secured Wi-Fi join/reconnect, real cloud conversation and hands-on physical-panel review remain unverified. The current avatar is the requested Ditto; physical review remains a separate acceptance step. Static captures and silent playback do not establish real voice quality, echo cancellation, panel flicker or battery life. The strict performance gates above remain explicit even when reliability passes.
'''
    (ROOT/'INTEGRATION-REPORT.md').write_text(report)
    sys.path.insert(0,str(ROOT/'tools'))
    from finalize_validation import finalize
    phase('package','running')
    package=finalize(ROOT,metrics)
    phase('validation','completed',metrics_file=str(OUT/'final-metrics.json'),console='http://127.0.0.1:8781',package=str(package),overall_product_complete=False)
except BaseException as e:
    phase('validation','failed',error=str(e));raise
