"""Compare actual old/new renderers, including shared vertices across tiles."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,subprocess,tempfile,hashlib,json,filecmp
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
reference=args.reference.resolve()/'components/a3d/src'
assert (reference/'backends/soft/a3d_soft_backend.h').exists()
with tempfile.TemporaryDirectory(prefix='scout-render-workers-') as folder:
 folder=Path(folder)
 def run(name,old,workers):
  exe=folder/name;raw=folder/(name+'.rgb565');body=folder/(name+'.body')
  flags=['-DSCOUT_LIGHTING_FADE_TEST',f'-DSCOUT_RENDER_WORKERS={workers}']
  includes=(['-I',str(reference)] if old else [])+['-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src')]
  subprocess.run(['c++','-std=c++17','-O2',*flags,*includes,str(ROOT/'tools/render_eye_interactions.cpp'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(body)],check=True)
  return raw,body
 with ThreadPoolExecutor(max_workers=3) as pool:
  jobs=[pool.submit(run,'reference',True,2),pool.submit(run,'candidate-two-workers',False,2),pool.submit(run,'candidate-one-worker',False,1)]
  a,b,c=[job.result() for job in jobs]
 for current in [b,c]:
  assert filecmp.cmp(a[0],current[0],shallow=False),'Renderer changed native pixels'
  assert filecmp.cmp(a[1],current[1],shallow=False),'Renderer changed deformation'
 with b[0].open('rb') as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
 report={'passed':True,'frames_per_run':480,'native_pixels_per_comparison':b[0].stat().st_size//2,'pixels_identical':True,'body_identical':True,'worker_counts':[1,2],'covers':['moving normal transforms','idle','press','pull','release','neighboring tiles','smooth UI fades','packed row attenuation'],'sha256':sha}
 args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
