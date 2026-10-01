"""Native software-renderer review; this is not a physical-screen recording."""
import argparse,math,shutil,struct,subprocess
from storage_hygiene import scratch_directory, check_budget
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',type=Path,default=ROOT/'test-results/ditto-integration',help='Directory for PNG views, contact sheet and motion.mp4')
args=parser.parse_args();OUT=args.output_dir.resolve();OUT.mkdir(parents=True,exist_ok=True)
check_budget(ROOT,OUT)
ffmpeg=shutil.which('ffmpeg')
if not ffmpeg:parser.error('ffmpeg is required on PATH to encode the motion preview')
renderer=None
def run(out,clip=0,ms=0,yaw=.12,frames=1,pitch=.05):
 subprocess.run([str(renderer),str(ROOT/'assets/avatar.a3d'),str(out),str(clip),str(ms),str(yaw),'3.9',str(frames),str(pitch)],check=True)
def png(raw):
 rgb=bytes(c for (v,) in struct.iter_unpack('<H',raw) for c in (((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
 return Image.frombytes('RGB',(410,502),rgb)
with scratch_directory('ditto',reserve_mib=272) as tmp:
 tmp=Path(tmp);renderer=tmp/'render_asset'
 subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_asset.cpp'),'-o',str(renderer)],check=True)
 sheet=Image.new('RGB',(410*4,532*2),'#131318');draw=ImageDraw.Draw(sheet)
 for i,(name,yaw,pitch,clip,ms) in enumerate([('Front',0,.05,0,0),('Left',-.8,.05,0,0),('Right',.8,.05,0,0),('Back',math.pi,.05,0,0),('Top',0,.9,0,0),('Sleep',.12,.05,9,0),('Surprise',.12,.05,10,2000),('Wave',.12,.05,4,900)]):
  p=tmp/'one.rgb565';run(p,clip,ms,yaw,pitch=pitch);im=png(p.read_bytes());im.save(OUT/(name.lower()+'.png'))
  x=i%4*410;y=i//4*532;sheet.paste(im,(x,y+30));draw.text((x+20,y+8),name,fill='#fff1e4')
 sheet.save(OUT/'native-views.png')
 full=tmp/'motion.rgb565'
 with full.open('wb') as f:
  for clip in [0,1,4,7]:
   p=tmp/'clip.rgb565';run(p,clip,frames=120);f.write(p.read_bytes())
 subprocess.run([ffmpeg,'-loglevel','error','-y','-f','rawvideo','-pixel_format','rgb565le','-video_size','410x502','-framerate','30','-i',str(full),'-c:v','libx264','-preset','slow','-crf','16','-pix_fmt','yuv420p','-movflags','+faststart',str(OUT/'motion.mp4')],check=True)
print(OUT/'motion.mp4')
check_budget(ROOT,OUT)
