"""Render the production controller under the approved traces, not scripted faces."""
import argparse,subprocess,json,csv,concurrent.futures
from storage_hygiene import scratch_directory, check_budget
from pathlib import Path
from array import array
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,required=True);args=p.parse_args();out=args.output_dir.resolve();out.mkdir(exist_ok=True,parents=True)
check_budget(ROOT,out)
names=['tap','slow-pull','quick-flick','hold-release','tilt','swing','shake-stop','repeated-pokes','poke-to-drag','quiet-idle','sleep','wake-speech']
frames=[[48,73,108],[74,100,135],[43,70,115],[90,124,168],[55,107,180],[58,116,167],[76,113,153],[76,108,156],[75,103,150],[68,115,190],[40,66,156],[65,88,114]]
with scratch_directory('approved',reserve_mib=224) as td:
 exe=td/'render'
 subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_approved_interactions.cpp'),'-o',str(exe)],check=True)
 def render(i):
  raw=td/(names[i]+'.raw');trace=out/(names[i]+'.csv')
  subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(i),str(trace)],check=True)
  if names[i]=='sleep':
   with raw.open('rb') as f:
    size=410*502*2;f.seek(100*size);frozen=f.read(size)
    for frame in range(101,240):assert f.read(size)==frozen,'Sleep animation did not freeze'
  sheet=Image.new('RGB',(1230,527),(16,16,20));draw=ImageDraw.Draw(sheet)
  with raw.open('rb') as f:
   for column,frame in enumerate(frames[i]):
    f.seek(frame*410*502*2);v=array('H');v.frombytes(f.read(410*502*2));im=Image.new('RGB',(410,502));im.putdata([((q>>11)*255//31,((q>>5)&63)*255//63,(q&31)*255//31) for q in v]);sheet.paste(im,(column*410,25));draw.text((column*410+12,7),f'{names[i]} / {frame/30:.2f}s',fill='white')
  sheet.save(out/(names[i]+'-review.png'))
  subprocess.run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb565le','-video_size','410x502','-framerate','30','-i',str(raw),'-c:v','libx264','-crf','18','-pix_fmt','yuv420p',str(out/(names[i]+'.mp4'))],check=True)
  raw.unlink();return {'scene':names[i],'frames':240,'contact_picks_checked':True,'finite_vertices_checked':True}
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
  futures=[pool.submit(render,i) for i in range(len(names))];results=[]
  for f in futures:
   result=f.result();results.append(result);print(result,flush=True)
 (out/'approved-render-check.json').write_text(json.dumps({'scenes':results,'frames':sum(r['frames'] for r in results),'native_size':[410,502],'lighting':'current glossy shader','controller':'production event-driven controller','note':'Synthetic inputs exercise controller behavior. Actual sensor fusion and panel output require hardware checks.'},indent=2)+'\n')
check_budget(ROOT,out)
