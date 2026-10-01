"""Package a completed numerical validation; never imply overall product completion."""
import argparse,hashlib,json,shutil,subprocess,sys,time
from pathlib import Path
from validation_context import evidence_directory

def publish_finished_progress(out, metrics, soak):
    # Preserve the measured numbers when the live sampler stops. The preview
    # reads this same JSON before and after completion.
    progress = {
        'running': False,
        'elapsed_s': metrics['duration_s'],
        'target_s': soak['requested_duration_s'],
        'last': soak['measurement_end'],
        'summary': metrics,
        'errors': soak['errors'],
        'unexpected_resets': soak['unexpected_resets'],
    }
    path = out / 'soak-progress.json'
    pending = path.with_suffix('.tmp')
    pending.write_text(json.dumps(progress, indent=2) + '\n')
    pending.replace(path)

def finalize(ROOT,metrics):
    OUT=evidence_directory(ROOT)
    seconds=metrics['duration_s'];fps=metrics['average_fps'];n=metrics['frames']
    under25=metrics['frames_within25_percent'];over33=metrics['frames_over33_333ms']
    assert hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest()==metrics['firmware_sha256'],'Binary changed after validation'
    status_path=ROOT/'WORK-STATUS.md';status=status_path.read_text()
    heading='## Running now' if '## Running now' in status else '## Latest sustained validation'
    a=status.index(heading);b=status.index('## Still open',a)
    status=status[:a]+f'''## Latest sustained validation

The mixed workload ran for **{seconds:.1f} seconds** and delivered **{fps:.2f} FPS** over **{n:,} native frames**. The functional, recovery, persistence, layout and bridge checks passed. Full evidence and the exact firmware identity are in `{OUT.relative_to(ROOT)}/`.

**{under25:.2f}%** of frames finished within 25 ms; the stricter target remains 95%. **{over33:,}** frames exceeded 33.333 ms. Performance and reliability are separate gates. See `INTEGRATION-REPORT.md` and `final-metrics.json` for the measured limits and heap comparisons.

The local console is running at http://127.0.0.1:8781. Final captures are available for visual inspection; static captures do not prove physical panel behavior.

'''+status[b:]
    status=status.replace('- Finish and evaluate the two-hour run, retain the final captures and package its exact firmware/source/evidence.','- Inspect the final captures and physical panel; source, firmware, and numerical evidence are packaged as an engineering checkpoint.')
    if metrics['two_hours_completed']:
        status=status.replace('- Complete the new sustained two-hour acceptance run on the chosen firmware/asset. Both earlier interrupted tests are preserved and explicitly incomplete.','- The two-hour run is complete; evaluate the recorded performance gates independently of reliability.')
    status_path.write_text(status)
    soak=json.loads((OUT/'soak-two-hour.json').read_text())
    publish_finished_progress(OUT,metrics,soak)
    # README points to the report rather than embedding avatar-specific timings.
    # Keeping the measured duration here avoids presenting a short check as a
    # completed two-hour acceptance run.
    gallery=ROOT.parents[2]/'outputs/esp32-amoled-analysis/native-resolution'/OUT.name.removesuffix('-integration')
    if gallery.exists():
        for source,dest in [('home-final.png','home.png'),('layered-wifi.png','wifi.png'),('layered-brightness.png','brightness.png')]:
            shutil.copy2(OUT/source,gallery/dest)
        page=gallery/'index.html'
        page.write_text(page.read_text().replace('Hardware qualification is using USB. The console will return automatically when the run finishes.','The local control console is running. The current validation report keeps performance, reliability and unverified conversation checks separate.'))
    package=ROOT.parents[2]/('outputs/scout-os-'+OUT.name.removesuffix('-integration'))
    with (OUT/'package.log').open('w') as log:
        subprocess.run([sys.executable,str(ROOT/'tools/package_release.py'),str(package)],stdout=log,stderr=subprocess.STDOUT,check=True)
    return package

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--wait',action='store_true');args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];out=evidence_directory(root)
    if args.wait:
        while True:
            state=json.loads((out/'validation-progress.json').read_text())
            if state['phase']=='validation' and state['state']=='failed':raise SystemExit('Validation failed; package not generated')
            if state['phase']=='validation' and state['state']=='completed':break
            time.sleep(10)
    metrics=json.loads((out/'final-metrics.json').read_text())
    package=finalize(root,metrics)
    (out/'package-status.json').write_text(json.dumps({'state':'completed','path':str(package),'overall_product_complete':False},indent=2)+'\n')
    print(package,flush=True)
