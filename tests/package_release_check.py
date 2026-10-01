"""Packager fault injection with synthetic artifacts; no build/USB side effects."""
import json,sys,tempfile,zipfile
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_provenance import snapshot,artifacts,store_record,BuildMismatch,ARTIFACTS
from package_release import package,DOCS,digest
from validation_context import evidence_directory
checks=[]

def reject(call,out,label):
    try:call()
    except (ValueError,BuildMismatch):pass
    else:raise AssertionError(label)
    assert not out.exists(),label
    assert not list(out.parent.glob('.'+out.name+'-*')),'Unfinished staging directory leaked'
    checks.append(label)

with tempfile.TemporaryDirectory(prefix='scout-package-check-') as temp:
    base=Path(temp);root=base/'source';root.mkdir()
    for name in ['main','host','assets','build/config','test-results/selected','test-results/ditto-integration']:(root/name).mkdir(parents=True)
    (root/'main/main.cpp').write_text('source')
    (root/'host/control.py').write_text('host source')
    (root/'VERSION').write_text('test-version\n')
    (root/'assets/avatar-manifest.json').write_text('{"id":"ditto-prototype"}')
    for name in DOCS:(root/name).write_text('Synthetic document '+name)
    compiler=base/'compiler';compiler.write_text('synthetic compiler')
    (root/'build/config/sdkconfig.h').write_text('synthetic configuration')
    (root/'build/compile_commands.json').write_text(json.dumps([{'command':f'{compiler} -c main/main.cpp'}]))
    (root/'build/project_description.json').write_text('{}')
    for name in ARTIFACTS:
        p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(('synthetic '+name).encode())
    proof={'schema':1,'version':'test-version','source':snapshot(root),'artifacts':artifacts(root)}
    store_record(root,proof)
    (root/'test-results/selected/check.json').write_text('{"passed":true,"version":"test-version"}')
    (root/'test-results/ditto-integration/ancient.json').write_text('{"passed":true,"version":"old"}')
    first=base/'first';result=package(root,first,['test-results/selected'])
    assert result['evidence_files']==1 and result['overall_product_complete'] is False
    with zipfile.ZipFile(first/'scout-os-source.zip') as archive:
        names=archive.namelist()
        assert 'scout-os/test-results/selected/check.json' in names
        assert not any('ancient' in name for name in names)
        assert archive.read('scout-os/host/control.py')==b'host source'
    assert json.loads((first/'firmware/manifest.json').read_text())['images'][-1]['sha256']==proof['artifacts']['build/scout_os.bin']
    checks.append('explicit evidence only; source and binary hashes match; no completion inference')
    second=base/'without-evidence';assert package(root,second)['evidence_files']==0
    checks.append('default does not select old avatar evidence')
    old=digest(first/'scout-os-source.zip')
    try:package(root,first)
    except ValueError:pass
    else:raise AssertionError('Existing package overwritten')
    assert digest(first/'scout-os-source.zip')==old;checks.append('existing package preserved')
    (root/'test-results/live').mkdir();(root/'test-results/live/soak-progress.json').write_text('{"running":true}')
    out=base/'live';reject(lambda:package(root,out,['test-results/live']),out,'running evidence rejected before output')
    outside=base/'outside.json';outside.write_text('{}')
    out=base/'outside-package';reject(lambda:package(root,out,[outside]),out,'outside evidence rejected')
    (root/'test-results/escape.json').symlink_to(outside)
    out=base/'symlink-package';reject(lambda:package(root,out,['test-results/escape.json']),out,'escaping evidence symlink rejected')
    dirty=root/'main/main.cpp';original=dirty.read_bytes();dirty.write_bytes(b'changed')
    out=base/'dirty';reject(lambda:package(root,out),out,'changed firmware source rejected');dirty.write_bytes(original)
    original_write=zipfile.ZipFile.write
    for mode in ('change-doc','add-host-file','change-firmware'):
        fired=[False];out=base/mode;doc=root/'README.md';doc_before=doc.read_bytes();binary=root/'build/scout_os.bin';binary_before=binary.read_bytes()
        def edit_during_zip(archive,*args,**kwargs):
            result=original_write(archive,*args,**kwargs)
            if not fired[0]:
                fired[0]=True
                if mode=='change-doc':doc.write_bytes(b'changed while copying')
                if mode=='add-host-file':(root/'host/new.py').write_text('new source')
                if mode=='change-firmware':binary.write_bytes(b'replaced while copying')
            return result
        with patch.object(zipfile.ZipFile,'write',edit_during_zip):
            reject(lambda:package(root,out),out,'concurrent '+mode+' rejected and staging removed')
        doc.write_bytes(doc_before);binary.write_bytes(binary_before);(root/'host/new.py').unlink(missing_ok=True)
report={'passed':True,'scope':'Synthetic package artifacts and injected filesystem changes; no firmware or product qualification','checks':checks}
(evidence_directory(ROOT)/'package-release-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
