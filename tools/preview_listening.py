"""Review the production listening rig/halo during pull, hold and release."""
from pathlib import Path
import subprocess,sys,hashlib,json,os
from array import array
from PIL import Image,ImageDraw
from storage_hygiene import scratch_directory,check_budget
ROOT=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'test-results/status-compact-0.11.16';out.mkdir(parents=True,exist_ok=True)
with scratch_directory('listening-review',reserve_mib=160) as tmp:
 tmp=Path(tmp);exe=tmp/'render';raw=tmp/'listening.rgb565'
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_listening.cpp'),'-o',str(exe)],check=True)
 env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
 result=subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),'2'],check=True,capture_output=True,text=True,env=env)
 digest=hashlib.sha256(raw.read_bytes()).hexdigest()
 subprocess.run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb565le','-video_size','410x502','-framerate','30','-i',str(raw),'-c:v','libx264','-crf','14','-pix_fmt','yuv420p','-movflags','+faststart',str(out/'listening-stretch.mp4')],check=True)
 sheet=Image.new('RGB',(4*410,532),'#141419');draw=ImageDraw.Draw(sheet)
 with raw.open('rb') as f:
  for col,(frame,label) in enumerate([(25,'Idle'),(45,'Listening + pulling'),(105,'Held + listening'),(200,'Released')]):
   f.seek(frame*410*502*2);a=array('H',f.read(410*502*2));im=Image.frombytes('RGB',(410,502),bytes(c for v in a for c in ((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)))
   im.save(out/f'listening-{frame}.png');sheet.paste(im,(410*col,30));draw.text((410*col+15,9),label,fill='white')
   if frame==105:im.crop((0,325,410,430)).resize((1230,315),Image.Resampling.NEAREST).save(out/'glow-300-percent.png')
 sheet.save(out/'listening-review.png')
 subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),'1'],check=True,capture_output=True,text=True,env=env)
 other=hashlib.sha256(raw.read_bytes()).hexdigest();assert other==digest
 subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),'2','0'],check=True,capture_output=True,text=True,env=env)
 assert hashlib.sha256(raw.read_bytes()).hexdigest()==digest,'Background block skip changes visible glow pixels'

 (out/'listening-render-check.json').write_text(json.dumps({'frames':210,'worker_1_and_2_identical':True,'background_blocks_match_full_glow':True,'sha256':digest,'sanitizers':'ASan/UBSan','skin_check':result.stderr.strip(),'scope':'Native production renderer replay; not device timing.'},indent=2)+'\n')
 print('210 listening/pull/release frames match exactly with one and two workers; ASan/UBSan clean.')
check_budget(ROOT,out)
