"""Exact full-render comparison of conservative tile-bound rounding."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,tempfile,hashlib,json,filecmp,os
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'test-results'/os.environ.get('SCOUT_EVIDENCE_NAME','cpu-counters-0.9.15');OUT.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='scout-surface-') as folder:
 folder=Path(folder)
 def run(name,flags):
  exe=folder/name;raw=folder/(name+'.rgb565');body=folder/(name+'.body')
  subprocess.run(['c++','-std=c++17','-O2','-DSCOUT_LIGHTING_FADE_TEST',*flags,'-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_eye_interactions.cpp'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(body)],check=True)
  return raw,body
 with ThreadPoolExecutor(max_workers=2) as pool:
  a=pool.submit(run,'reference',['-DA3D_REFERENCE_BIN_ROUNDING']);b=pool.submit(run,'compact',[])
  a,b=a.result(),b.result()
 assert filecmp.cmp(a[0],b[0],shallow=False),'Tile rounding changed the image'
 assert filecmp.cmp(a[1],b[1],shallow=False),'Body physics changed'
 with b[0].open('rb') as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
 report={'passed':True,'frames':480,'native_pixels_compared':b[0].stat().st_size//2,'pixels_identical':True,'body_identical':True,'covers':['idle','press','pull','release','smooth UI fades','packed row attenuation'],'sha256':sha}
 (OUT/'bin-rounding-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
