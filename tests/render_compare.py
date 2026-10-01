"""Compare archived renderer headers with the candidate; quantify RGB565 error."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from array import array
from PIL import Image,ImageChops,ImageEnhance
import argparse,subprocess,tempfile,hashlib,json,filecmp,math
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('output',type=Path)
p.add_argument('--max-channel-step',type=int,default=0)
p.add_argument('--max-changed-fraction',type=float,default=0)
args=p.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
reference=args.reference.resolve()/'components/a3d/src'
assert (reference/'backends/soft/a3d_soft_backend.h').exists()
with tempfile.TemporaryDirectory(prefix='scout-render-compare-') as folder:
 folder=Path(folder)
 def render(name,old):
  exe=folder/name;raw=folder/(name+'.rgb565');body=folder/(name+'.body')
  includes=(['-I',str(reference)] if old else [])+['-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src')]
  subprocess.run(['c++','-std=c++17','-O2','-DSCOUT_LIGHTING_FADE_TEST',*includes,str(ROOT/'tools/render_eye_interactions.cpp'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(body)],check=True)
  return raw,body
 with ThreadPoolExecutor(max_workers=2) as pool:
  jobs=[pool.submit(render,'reference',True),pool.submit(render,'candidate',False)]
  a,b=[j.result() for j in jobs]
 body_equal=filecmp.cmp(a[1],b[1],shallow=False)
 first=array('H');second=array('H')
 with a[0].open('rb') as f:first.fromfile(f,a[0].stat().st_size//2)
 with b[0].open('rb') as f:second.fromfile(f,b[0].stat().st_size//2)
 assert len(first)==len(second)==480*410*502
 changed=0;maximum=0;squared_error=0;per_frame=[0]*480;first_change=[None]*480
 for pos,(x,y) in enumerate(zip(first,second)):
  if x==y:continue
  changed+=1;frame=pos//(410*502);per_frame[frame]+=1
  if first_change[frame] is None:first_change[frame]=pos%(410*502)
  for xx,yy,scale in [(x>>11,y>>11,31),((x>>5)&63,(y>>5)&63,63),(x&31,y&31,31)]:
   maximum=max(maximum,abs(xx-yy));squared_error+=(xx*255//scale-yy*255//scale)**2
 worst=max(range(480),key=lambda f:per_frame[f]);where=first_change[worst] or (250*410+205)
 xx,yy=where%410,where//410;cx=max(0,min(410-96,xx-48));cy=max(0,min(502-96,yy-48))
 frames=[]
 for name,data in [('reference',first),('candidate',second)]:
  rgb=bytearray()
  for value in data[worst*410*502:(worst+1)*410*502]:rgb.extend(((value>>11)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
  image=Image.frombytes('RGB',(410,502),bytes(rgb));frames.append(image)
  image.save(out/(name+'-worst-frame.png'))
  image.crop((cx,cy,cx+96,cy+96)).resize((288,288),Image.Resampling.NEAREST).save(out/(name+'-300pct.png'))
 ImageEnhance.Brightness(ImageChops.difference(*frames)).enhance(12).save(out/'difference-amplified.png')
 report={'passed':body_equal and maximum<=args.max_channel_step and changed/len(first)<=args.max_changed_fraction,
  'frames':480,'pixels':len(first),'changed_pixels':changed,'changed_fraction':changed/len(first),
  'max_rgb565_channel_step':maximum,'body_identical':body_equal,'worst_frame':worst,
  'worst_frame_changed_pixels':per_frame[worst],'psnr_db':10*math.log10(255**2/(squared_error/(len(first)*3))) if squared_error else None,
  'limits':{'channel_step':args.max_channel_step,'changed_fraction':args.max_changed_fraction},
  'reference_sha256':hashlib.sha256(first.tobytes()).hexdigest(),'candidate_sha256':hashlib.sha256(second.tobytes()).hexdigest()}
 (out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
 assert report['passed'],report
