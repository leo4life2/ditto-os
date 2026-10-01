"""Exercise stale timestamps, changed flags/artifacts, failures and flash ordering."""
from pathlib import Path
import sys,tempfile,json,os,subprocess,shutil
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_provenance import run_build,verify,BuildMismatch,STAMP,ARTIFACTS,inputs

checks=[]
def reject(fn,label):
    try:fn()
    except BuildMismatch:checks.append(label)
    else:raise AssertionError(label)

with tempfile.TemporaryDirectory(prefix='scout-build-proof-') as td:
    root=Path(td);(root/'main').mkdir();(root/'build').mkdir();(root/'tools').mkdir()
    source=root/'main/code.cpp';source.write_text('original source\n');(root/'VERSION').write_text('1.2.3\n')
    compiler=root/'compiler';compiler.write_bytes(b'test compiler')
    events=[];flags='-O3';fail=False;edit=False
    def invoke(args):
        action=args[-1];events.append(action)
        if action=='reconfigure':
            (root/'build/config').mkdir(exist_ok=True)
            (root/'build/config/sdkconfig.h').write_text('#define CONFIG_TEST 1\n')
            (root/'build/project_description.json').write_text('{}')
            (root/'build/compile_commands.json').write_text(json.dumps([{'command':f'{compiler} {flags} -c main/code.cpp'}]))
        elif action in {'clean','fullclean'}:
            for p in ARTIFACTS:(root/p).unlink(missing_ok=True)
        elif action=='build':
            if fail:raise subprocess.CalledProcessError(1,['idf.py','build'])
            # Emulate the stale object bug: existing output stays unchanged
            # regardless of source contents until clean explicitly removes it.
            data=json.dumps(inputs(root),sort_keys=True).encode()+flags.encode()
            for p in ARTIFACTS:
                target=root/p;target.parent.mkdir(parents=True,exist_ok=True)
                if not target.exists():target.write_bytes(data)
            if edit:source.write_text('changed while compiling\n')
        elif action in {'flash','app-flash','monitor'}:
            verify(root) # Must be sealed before the first flash side effect.
        else:raise AssertionError(args)
    def clean_count():return events.count('clean')
    run_build(root,['build'],invoke);first=verify(root);assert clean_count()==1;checks.append('untrusted initial build cleaned and sealed')
    os.utime(source,(1,1));run_build(root,['build'],invoke);assert clean_count()==1;checks.append('mtime-only change reused matching contents')
    source.write_text('changed! source\n');os.utime(source,(1,1));reject(lambda:verify(root),'changed source rejected despite old mtime')
    run_build(root,['build'],invoke);assert clean_count()==2 and verify(root)['artifacts']!=first['artifacts'];checks.append('old-mtime source forced clean recompilation')
    flags='-O2 -DNEW_FLAG=1';run_build(root,['build'],invoke);assert clean_count()==3;checks.append('changed compiler command forced clean')
    (root/ARTIFACTS[0]).write_bytes(b'replaced');reject(lambda:verify(root),'replaced binary rejected')
    run_build(root,['-p','testport','flash'],invoke);assert clean_count()==4 and events[-1]=='flash';checks.append('verified record exists before flash')
    fail=True
    try:run_build(root,['build'],invoke)
    except subprocess.CalledProcessError:pass
    else:raise AssertionError('failed compiler accepted')
    assert not (root/STAMP).exists();checks.append('failed compiler invalidates prior record');fail=False
    edit=True;reject(lambda:run_build(root,['build'],invoke),'concurrent source edit rejected');assert not (root/STAMP).exists();edit=False
    run_build(root,['build'],invoke);source.unlink();reject(lambda:verify(root),'removed input rejected');source.write_text('restored source\n')
    run_build(root,['build'],invoke);(root/'main/added.h').write_text('new dependency');reject(lambda:verify(root),'added input rejected')
    before=len(events);reject(lambda:run_build(root,['-B','elsewhere','flash'],invoke),'alternate output directory rejected before commands');assert len(events)==before
    (root/STAMP).write_text('{bad json');reject(lambda:verify(root),'malformed record rejected')
    # The real packaging entry point must fail before creating output folders.
    for p in ['package_release.py','build_provenance.py','validation_context.py']:shutil.copyfile(ROOT/'tools'/p,root/'tools'/p)
    out=root/'must-not-exist'
    p=subprocess.run([sys.executable,str(root/'tools/package_release.py'),str(out)],capture_output=True,text=True)
    assert p.returncode!=0 and not out.exists();checks.append('packager rejects invalid provenance before writing output')
print(json.dumps({'passed':True,'checks':checks},indent=2))
