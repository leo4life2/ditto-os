"""Content-based guard for this project's default ESP-IDF build directory.

Tracks project/managed component inputs and generated compiler configuration.
The external SDK is identified by ESP-IDF's recorded path/version; use the pinned
SDK described in README. This is build consistency, not supply-chain attestation.
"""
import hashlib,json,os,shlex,subprocess,sys,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
STAMP='build/verified-build.json'
INPUT_DIRS=('main','components','managed_components','assets')
INPUT_FILES=('CMakeLists.txt','VERSION','sdkconfig','sdkconfig.defaults',
             'partitions.csv','dependencies.lock','build.sh','tools/build_provenance.py')
CONFIG_FILES=('build/compile_commands.json','build/config/sdkconfig.h',
              'build/project_description.json')
ARTIFACTS=('build/scout_os.bin','build/scout_os.elf','build/bootloader/bootloader.bin',
           'build/partition_table/partition-table.bin')
BUILD_ACTIONS={'build','all','flash','app-flash','bootloader-flash','partition-table-flash'}
OTHER_ACTIONS={'reconfigure','clean','fullclean','size','size-components','size-files','monitor'}
READ_ACTIONS={'size','size-components','size-files','monitor'}

class BuildMismatch(RuntimeError):pass

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def inputs(root):
    paths=set()
    for folder in INPUT_DIRS:
        for p in (root/folder).rglob('*'):
            if p.is_file() and not any(x in {'.git','__pycache__'} for x in p.parts) and p.suffix!='.pyc':paths.add(p)
    paths.update(root/p for p in INPUT_FILES if (root/p).is_file())
    return {str(p.relative_to(root)):digest(p) for p in sorted(paths)}

def snapshot(root):
    config={p:digest(root/p) for p in CONFIG_FILES if (root/p).is_file()}
    if len(config)!=len(CONFIG_FILES):raise BuildMismatch('Compiler configuration is missing; run bash build.sh build.')
    # Record the actual compiler selected by CMake, not merely a PATH alias.
    commands=json.loads((root/CONFIG_FILES[0]).read_text())
    compilers={shlex.split(c['command'])[0] for c in commands if c.get('command')}
    compiler_hashes={p:digest(Path(p)) for p in sorted(compilers)}
    return {'inputs':inputs(root),'configuration':config,'compilers':compiler_hashes}

def artifacts(root):
    try:return {p:digest(root/p) for p in ARTIFACTS}
    except FileNotFoundError as e:raise BuildMismatch('Built firmware is missing; run bash build.sh build.') from e

def read_record(root):
    try:return json.loads((root/STAMP).read_text())
    except (FileNotFoundError,json.JSONDecodeError):return None

def verify(root=ROOT,record=None):
    record=read_record(root) if record is None else record
    if not record or record.get('schema')!=1:raise BuildMismatch('No verified build record; run bash build.sh build before packaging or flashing.')
    if record.get('source')!=snapshot(root):raise BuildMismatch('Source or compiler configuration differs from the verified build; run bash build.sh build.')
    if record.get('artifacts')!=artifacts(root):raise BuildMismatch('Firmware artifacts differ from the verified build; run bash build.sh build.')
    return record

def store_record(root,record):
    target=root/STAMP;target.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.verified-',suffix='.json',dir=target.parent)
    try:
        with os.fdopen(fd,'w') as f:json.dump(record,f,indent=2);f.write('\n')
        os.replace(name,target)
    finally:
        if os.path.exists(name):os.unlink(name)

def run_build(root,args,invoke=None):
    """Build first and seal it before invoking any requested flash/monitor."""
    args=args or ['build']
    invoke=invoke or (lambda a:subprocess.run(['idf.py',*a],cwd=root,check=True))
    if any(a in ('-B','--build-dir','-C','--project-dir') or a.startswith(('--build-dir=','--project-dir=')) or (a.startswith(('-B','-C')) and len(a)>2) for a in args):
        raise BuildMismatch('Verified builds use this project and its default build/ directory. Run the wrapper from the intended source copy.')
    if any(a in {'-h','--help','--version'} for a in args):return invoke(args)
    actions=[a for a in args if a in BUILD_ACTIONS|OTHER_ACTIONS]
    if not any(a in BUILD_ACTIONS for a in actions):
        if not actions or any(a not in READ_ACTIONS for a in actions):(root/STAMP).unlink(missing_ok=True)
        return invoke(args)
    globals_=[a for a in args if a not in BUILD_ACTIONS|OTHER_ACTIONS]
    prior=read_record(root)
    (root/STAMP).unlink(missing_ok=True) # Failure can never leave a trusted old seal.
    if 'fullclean' in actions:invoke([*globals_,'fullclean'])
    invoke([*globals_,'reconfigure'])
    before=snapshot(root)
    reusable=False
    if prior and 'clean' not in actions and 'fullclean' not in actions:
        try:verify(root,prior);reusable=True
        except BuildMismatch:pass
    if not reusable:
        print('Build inputs changed or are unverified: forcing clean compilation.',flush=True)
        invoke([*globals_,'clean'])
    invoke([*globals_,'build'])
    after=snapshot(root)
    if after!=before:raise BuildMismatch('Build inputs changed while compiling; no verified record was created. Build again.')
    record={'schema':1,'source':after,'artifacts':artifacts(root),
            'version':(root/'VERSION').read_text().strip(),'clean_compilation':not reusable}
    store_record(root,record)
    print('Verified build:',record['version'],record['artifacts']['build/scout_os.bin'],flush=True)
    follow=[a for a in actions if a not in {'build','all','reconfigure','clean','fullclean'}]
    if follow:
        verify(root)
        invoke([*globals_,*follow])
        verify(root)
    return record

if __name__=='__main__':
    try:
        if sys.argv[1:]==['--verify']:print(json.dumps(verify(),indent=2))
        else:run_build(ROOT,sys.argv[1:])
    except (BuildMismatch,subprocess.CalledProcessError) as e:
        print('Build verification failed:',e,file=sys.stderr);sys.exit(1)
