"""Exercise scoped deletion, preservation, reservations, and failure cleanup."""
import json, os, subprocess, sys, tempfile, time
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import storage_hygiene as s

with tempfile.TemporaryDirectory(prefix='scout-hygiene-test-') as tmp:
    root=Path(tmp).resolve()/'project';root.mkdir();outside=Path(tmp).resolve()/'unrelated';outside.mkdir()
    (outside/'keep').write_text('keep')
    with s.scratch_directory('normal',root=root) as p:
        (p/'raw').write_bytes(b'123');assert p.parent==root/'.scratch'
    assert not p.exists()
    try:
        with s.scratch_directory('failure',root=root) as p:
            (p/'raw').write_bytes(b'123');raise ValueError('intentional failure')
    except ValueError:pass
    assert not p.exists()
    with s.scratch_directory('large',reserve_mib=400,root=root):
        try:
            with s.scratch_directory('overflow',reserve_mib=200,root=root):pass
        except RuntimeError:pass
        else:raise AssertionError('Concurrent reservations exceeded cap')
    base=root/'.scratch'
    def marked(name, pid=-1, age=90000, owner=None):
        p=base/name;p.mkdir();(p/'raw').write_text('data')
        (p/s.MARKER).write_text(json.dumps({'schema':1,'root':str(owner or root),
            'pid':pid,'created':time.time()-age,'reserved_bytes':100}))
        return p
    stale=marked('stale',pid=99999999)
    live=marked('live',pid=os.getpid())
    fresh=marked('fresh',pid=99999999,age=5)
    foreign=marked('foreign',pid=99999999,owner=outside)
    invalid=marked('invalid')
    unmarked=base/'unmarked';unmarked.mkdir();(unmarked/'keep').write_text('keep')
    linked=base/'linked';linked.symlink_to(outside,target_is_directory=True)
    # An abandoned directory may contain symlinks; deletion must never follow them.
    (stale/'outside-link').symlink_to(outside,target_is_directory=True)
    with patch('subprocess.run',return_value=subprocess.CompletedProcess([],1,'','')):
        found=s.stale_scratch(root)
        assert [r['path'] for r in found]==[str(stale)] and stale.exists()
        deleted=s.stale_scratch(root,apply=True)
        assert deleted[0]['deleted'] and not stale.exists()
    for p in (live,fresh,foreign,invalid,unmarked,linked,outside/'keep'):assert p.exists(),p
    busy=marked('busy',pid=99999999)
    with patch('subprocess.run',return_value=subprocess.CompletedProcess([],0,'open files','')):
        assert s.stale_scratch(root,apply=True)==[] and busy.exists()
    with patch('subprocess.run',return_value=subprocess.CompletedProcess([],1,'','inspection failed')):
        assert s.stale_scratch(root,apply=True)==[] and busy.exists()
    with patch.object(s,'EVIDENCE_LIMIT',0):
        (root/'test-results').mkdir();(root/'test-results/keep').write_text('evidence')
        try:s.check_budget(root)
        except RuntimeError:pass
        else:raise AssertionError('Evidence cap not enforced')
        assert (root/'test-results/keep').exists()
    # Symlinked scratch roots must fail before enumeration or deletion.
    another=Path(tmp)/'another';another.mkdir();(another/'.scratch').symlink_to(outside,target_is_directory=True)
    try:s.stale_scratch(another,apply=True)
    except RuntimeError:pass
    else:raise AssertionError('Symlinked root accepted')
    assert (outside/'keep').read_text()=='keep'
print('PASS: scoped pruning, live/foreign/unmarked/symlink preservation, budget reservations and exception cleanup')
