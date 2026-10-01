"""Compare optimized and full native rendering for every character clip."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
from storage_hygiene import scratch_directory

ROOT=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
results=[]
with scratch_directory('all-clip-tile-reference',reserve_mib=128) as tmp:
    tmp=Path(tmp);exe=tmp/'render';raw=tmp/'frames.rgb565'
    subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined',
        '-I'+str(ROOT/'main'),'-I'+str(ROOT/'components/a3d/src'),
        str(ROOT/'tools/render_listening.cpp'),'-o',str(exe)],check=True)
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
    for clip in range(16):
        hashes=[]
        for optimized in (1,0):
            subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),'2',str(optimized),str(clip)],
                env=env,check=True,capture_output=True)
            hashes.append(digest(raw))
        assert hashes[0]==hashes[1],f'Optimized rendering changed clip {clip}'
        results.append({'clip':clip,'frames':210,'sha256':hashes[0]})
        print(f'Clip {clip}: all 210 frames identical',flush=True)
out.write_text(json.dumps({'frames_compared':3360,'all_pixels_identical':True,
    'sanitizers':'ASan/UBSan','clips':results},indent=2)+'\n')
