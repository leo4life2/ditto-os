"""Bound Scout evidence and keep render scratch inside this project.

Only explicit --prune-stale --apply deletes anything. It operates exclusively
on this helper's marked, abandoned directories in ROOT/.scratch, never /tmp.
"""
import argparse
import contextlib
import fcntl
import json
import os
import re
import shutil
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MIB = 1024**2
EVIDENCE_LIMIT = 1280*MIB
SCRATCH_LIMIT = 512*MIB
CAPTURE_LIMIT = 64*MIB
MARKER = '.scout-scratch.json'

def allocated(path):
    path = Path(path)
    if path.is_symlink() or not path.exists(): return 0
    if path.is_file(): return path.stat().st_blocks*512
    return sum(p.stat().st_blocks*512 for p in path.rglob('*')
               if p.is_file() and not p.is_symlink())

def check_budget(root=ROOT, output=None):
    sizes = {'test_results_bytes': allocated(root/'test-results'),
             'scratch_bytes': allocated(root/'.scratch')}
    if sizes['test_results_bytes'] > EVIDENCE_LIMIT:
        raise RuntimeError('test-results exceeds 1.25 GiB. Review completed runs and prune generated builds/raw dumps; preserve source, reports and useful evidence. See STORAGE-HYGIENE.md.')
    if sizes['scratch_bytes'] > SCRATCH_LIMIT:
        raise RuntimeError('.scratch exceeds 512 MiB. Inspect storage_hygiene.py --prune-stale; no files were deleted.')
    if output is not None and allocated(output) > CAPTURE_LIMIT:
        raise RuntimeError(f'Capture directory exceeds 64 MiB: {output}. Select bounded evidence before rendering more.')
    return sizes

def alive(pid):
    if not isinstance(pid,int) or pid <= 0: return True
    try: os.kill(pid,0)
    except ProcessLookupError: return False
    except PermissionError: return True
    return True

@contextlib.contextmanager
def locked(root):
    base = root/'.scratch'
    if base.is_symlink(): raise RuntimeError('Refusing symlinked scratch root')
    base.mkdir(mode=0o700, exist_ok=True)
    lock = base/'.lock'
    fd = os.open(lock, os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd,'a') as stream:
        fcntl.flock(stream,fcntl.LOCK_EX)
        yield base

def record(path, root):
    if path.is_symlink() or not path.is_dir() or path.parent != root/'.scratch': return None
    marker = path/MARKER
    if marker.is_symlink() or not marker.is_file() or marker.stat().st_uid != os.getuid(): return None
    try: data=json.loads(marker.read_text())
    except (OSError,ValueError): return None
    if not isinstance(data,dict) or data.get('schema')!=1 or data.get('root')!=str(root): return None
    if not isinstance(data.get('created'),(int,float)) or not isinstance(data.get('reserved_bytes'),int): return None
    return data

@contextlib.contextmanager
def scratch_directory(label, reserve_mib=96, root=ROOT):
    root=Path(root).resolve()
    if not re.fullmatch(r'[a-z0-9-]+',label): raise ValueError('Use a simple scratch label')
    reserved=int(reserve_mib*MIB)
    if not 0 < reserved <= SCRATCH_LIMIT: raise ValueError('Invalid scratch reservation')
    check_budget(root)
    with locked(root) as base:
        committed=0
        for p in base.iterdir():
            data=record(p,root)
            committed+=max(allocated(p),data['reserved_bytes'] if data else 0)
        if committed+reserved > SCRATCH_LIMIT:
            raise RuntimeError('Other render jobs or stale scratch exhaust the 512 MiB budget. Finish them or inspect --prune-stale.')
        path=Path(tempfile.mkdtemp(prefix=label+'-',dir=base))
        (path/MARKER).write_text(json.dumps({'schema':1,'root':str(root),'pid':os.getpid(),
                                          'created':time.time(),'reserved_bytes':reserved})+'\n')
    try: yield path
    finally:
        with locked(root): shutil.rmtree(path)

def stale_scratch(root=ROOT, apply=False, now=None):
    root=Path(root).resolve(); now=time.time() if now is None else now; result=[]
    with locked(root) as base:
        for p in sorted(base.iterdir()):
            data=record(p,root)
            if not data or now-data['created'] < 86400 or alive(data.get('pid')): continue
            # Conservative: a process may have inherited the directory from a dead parent.
            import subprocess
            opened=subprocess.run(['lsof','-n','+D',str(p)],capture_output=True,text=True)
            if opened.returncode!=1 or opened.stdout or opened.stderr: continue
            result.append({'path':str(p),'allocated_bytes':allocated(p),'deleted':apply})
            if apply: shutil.rmtree(p)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check',action='store_true')
    p.add_argument('--prune-stale',action='store_true')
    p.add_argument('--apply',action='store_true')
    args=p.parse_args()
    if args.apply and not args.prune_stale: p.error('--apply requires --prune-stale')
    try:
        result=stale_scratch(apply=args.apply) if args.prune_stale else check_budget()
        print(json.dumps(result,indent=2))
    except (OSError,RuntimeError) as error: raise SystemExit(str(error))
