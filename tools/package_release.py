"""Export an immutable engineering checkpoint, never infer product acceptance.

Evidence is selected explicitly; an avatar name cannot identify a firmware run.
No USB access, flashing, preview mutation or qualification-status edits occur.
"""
import argparse,hashlib,json,os,shutil,tempfile,zipfile
from pathlib import Path
from build_provenance import verify,BuildMismatch
from storage_hygiene import check_budget

ROOT=Path(__file__).resolve().parents[1]
SOURCE_DIRS=('main','components','managed_components','host','tests','tools','examples','assets')
SOURCE_FILES=('CMakeLists.txt','VERSION','build.sh','sdkconfig','sdkconfig.defaults','partitions.csv','dependencies.lock')
DOCS=('README.md','TEST-REPORT.md','THIRD-PARTY.md','WORK-STATUS.md',
      'NATIVE-RENDER-REPORT.md','DEFINITION-OF-DONE.md','APP-DEVELOPMENT.md',
      'CHARACTER-STUDY.md','MOTION-PHYSICS.md','TOUCH-PHYSICS.md','POWER-AND-SLEEP.md',
      'LIGHTWEIGHT-SLEEP.md','TOUCH-LATENCY.md','HOME-LAYOUT.md','AVATAR-PIPELINE.md',
      'INTERACTION-DESIGN.md','INTEGRATION-REPORT.md','LOCAL-ACCEPTANCE.md',
      'RIG-AND-EMOTES.md','STORAGE-HYGIENE.md','AGENTS.md','FLOW-REMOTE.md','WIRELESS-AUDIO-AND-BATTERY.md')
EVIDENCE_SUFFIXES={'.json','.png','.log','.md','.csv','.mp4','.wav','.jsonl'}

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def inside(path,root):
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'Path leaves the permitted source tree: {path}')
    return path

def source_files(root):
    paths=set()
    for folder in SOURCE_DIRS:
        for path in (root/folder).rglob('*'):
            if (path.is_file() and not any(x in {'.git','__pycache__'} for x in path.parts)
                and path.suffix!='.pyc' and (folder!='tools' or path.suffix or path.name in {'LICENSE','NOTICE'})):
                paths.add(inside(path,root))
    for name in (*SOURCE_FILES,*DOCS):
        path=root/name
        if path.is_file():paths.add(inside(path,root))
    return paths

def select_evidence(root,selections):
    base=(root/'test-results').resolve();files=set();groups=[]
    for selection in selections:
        path=Path(selection)
        if not path.is_absolute():path=root/path
        path=inside(path,base).resolve()
        if not path.exists():raise ValueError(f'Evidence does not exist: {selection}')
        candidates=sorted(path.rglob('*')) if path.is_dir() else [path]
        selected=[]
        for item in candidates:
            if not item.is_file() or item.suffix not in EVIDENCE_SUFFIXES:continue
            inside(item,base)
            if any(p in {'__pycache__','.git'} for p in item.parts):continue
            if item.suffix=='.json':
                try:value=json.loads(item.read_text())
                except (ValueError,UnicodeError):raise ValueError(f'Malformed evidence JSON: {item}')
                if isinstance(value,dict) and (value.get('running') is True or value.get('state') in {'running','started'}):
                    raise ValueError(f'Evidence still reports an active run: {item}')
            selected.append(item);files.add(item)
        if not selected:raise ValueError(f'No supported evidence files in: {selection}')
        groups.append({'selection':str(path.relative_to(root)),
                       'files':[str(p.relative_to(root)) for p in selected],
                       'qualification':'Attached for review; applicability and acceptance are not inferred'})
    return files,groups

def package(root,output,selections=()):
    root=root.resolve();output=Path(os.path.abspath(output));selections=tuple(selections)
    check_budget(root)
    # Fail before writing anything if the firmware and source do not agree.
    proof=verify(root)
    if os.path.lexists(output):raise ValueError('Output already exists; choose a new checkpoint directory')
    if output.resolve().is_relative_to(root):raise ValueError('Place the package outside the source project')
    evidence,groups=select_evidence(root,selections)
    paths=source_files(root)|evidence
    avatar=json.loads((root/'assets/avatar-manifest.json').read_text())
    identities={str(p.relative_to(root)):digest(p) for p in sorted(paths)}
    for name,sha in proof['source']['inputs'].items():
        if identities.get(name)!=sha:raise BuildMismatch(f'Sealed input missing or changed before packaging: {name}')
    evidence_index={'selections':groups,'files':{str(p.relative_to(root)):identities[str(p.relative_to(root))] for p in sorted(evidence)},
                    'overall_product_complete':False,'scope':'Engineering checkpoint only; review WORK-STATUS.md and the individual reports'}
    manifest={'schema':2,'kind':'engineering-checkpoint','version':proof['version'],
              'build_record':'verified-build.json','chip':'ESP32-S3','flash_size':'32MB',
              'flash_mode':'dio','flash_frequency':'80m','idf_version':'5.5.5','avatar':avatar['id'],
              'distribution':'Private local prototype; provenance in THIRD-PARTY.md',
              'overall_product_complete':False,'images':[]}
    output.parent.mkdir(parents=True,exist_ok=True)
    staging=Path(tempfile.mkdtemp(prefix='.'+output.name+'-',dir=output.parent))
    try:
        (staging/'firmware').mkdir()
        for address,name in [('0x0','bootloader/bootloader.bin'),('0x8000','partition_table/partition-table.bin'),('0x10000','scout_os.bin')]:
            source=root/'build'/name;dest=staging/'firmware'/source.name
            shutil.copyfile(source,dest);sha=digest(dest)
            if sha!=proof['artifacts']['build/'+name]:raise BuildMismatch(f'Firmware changed while copying: {name}')
            manifest['images'].append({'address':address,'file':dest.name,'bytes':dest.stat().st_size,'sha256':sha})
        (staging/'firmware/verified-build.json').write_text(json.dumps(proof,indent=2)+'\n')
        (staging/'firmware/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        for name in DOCS:
            if (root/name).is_file():shutil.copyfile(root/name,staging/name)
        for path in sorted(evidence):
            dest=staging/'evidence'/path.relative_to(root/'test-results')
            dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest)
            if digest(dest)!=identities[str(path.relative_to(root))]:raise BuildMismatch(f'Evidence changed while copying: {path}')
        (staging/'evidence-index.json').write_text(json.dumps(evidence_index,indent=2)+'\n')
        (staging/'source-files.json').write_text(json.dumps(identities,indent=2)+'\n')
        overview='''# Engineering checkpoint

This package matches its sealed firmware source. It does not declare the OS or its qualification complete.

Read WORK-STATUS.md for remaining work and DEFINITION-OF-DONE.md for the acceptance standard. The firmware manifest records flash offsets and image hashes. source-files.json records the source and selected evidence hashes; evidence-index.json lists exactly what was selected. Historical paths mentioned in documents may refer to evidence not included here. A report's presence does not qualify it for this binary.

No evidence is selected automatically by avatar name. No API key or network credential is requested by packaging. The source bundle excludes the build directory, Python caches, executable tools without source extensions, and unselected test evidence. The external SDK/toolchain must be installed as described in README.md.
'''
        (staging/'PACKAGE.md').write_text(overview)
        archive_path=staging/'scout-os-source.zip'
        with zipfile.ZipFile(archive_path,'w',zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(paths):archive.write(path,Path('scout-os')/path.relative_to(root))
            archive.writestr('scout-os/PACKAGE.md',overview)
            archive.writestr('scout-os/evidence-index.json',json.dumps(evidence_index,indent=2)+'\n')
            archive.writestr('scout-os/source-files.json',json.dumps(identities,indent=2)+'\n')
        with zipfile.ZipFile(archive_path) as archive:
            if archive.testzip() is not None:raise BuildMismatch('Source archive failed its CRC check')
            for name,sha in identities.items():
                with archive.open('scout-os/'+name) as stream:copied=hashlib.file_digest(stream,'sha256').hexdigest()
                if copied!=sha or digest(root/name)!=sha:raise BuildMismatch(f'Source/evidence changed during export: {name}')
        for name in DOCS:
            if (root/name).is_file() and digest(staging/name)!=identities[name]:raise BuildMismatch(f'Document changed during export: {name}')
        reselected,_=select_evidence(root,selections)
        if source_files(root)|reselected!=paths:raise BuildMismatch('Source/evidence file set changed during export')
        verify(root,proof)
        summary={'version':proof['version'],'kind':'engineering-checkpoint','overall_product_complete':False,
                 'source_files':len(paths),'source_zip_bytes':archive_path.stat().st_size,'source_zip_sha256':digest(archive_path),
                 'evidence_files':len(evidence),'app_sha256':manifest['images'][-1]['sha256']}
        (staging/'checkpoint.json').write_text(json.dumps(summary,indent=2)+'\n')
        if os.path.lexists(output):raise ValueError('Output appeared while packaging; refusing to replace it')
        staging.rename(output)
        return {**summary,'output':str(output)}
    finally:
        if staging.exists():shutil.rmtree(staging)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--evidence',action='append',default=[],help='Explicit file/directory under test-results; repeat for multiple snapshots. Active-run reports are rejected.')
    args=parser.parse_args()
    try:print(json.dumps(package(ROOT,args.output,args.evidence),indent=2))
    except (BuildMismatch,ValueError,OSError) as error:raise SystemExit(str(error))

if __name__=='__main__':main()
