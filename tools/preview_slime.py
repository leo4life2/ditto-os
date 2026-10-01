"""Render the shared live slime code and assemble native-size visual evidence."""
import argparse,json,struct,subprocess,hashlib
from storage_hygiene import scratch_directory, check_budget
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=ROOT/'test-results/ditto-emotes-0.6.0');a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
check_budget(ROOT,out)
with scratch_directory('slime',reserve_mib=352) as tmp:
 tmp=Path(tmp);exe=tmp/'render';raw=tmp/'parallel.rgb565';serial=tmp/'serial.rgb565'
 subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_blob_physics.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw)],check=True)
 subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(serial),'single'],check=True)
 data=raw.read_bytes();assert data==serial.read_bytes(),'Parallel deformation differs'
 (out/'deformation-render-check.json').write_text(json.dumps({'frames':400,'width':410,'height':502,'identical':True,'sha256':hashlib.sha256(data).hexdigest()},indent=2)+'\n')
 subprocess.run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb565le','-video_size','410x502','-framerate','30','-i',str(raw),'-c:v','libx264','-crf','18','-pix_fmt','yuv420p',str(out/'slime-preview.mp4')],check=True)
 w,h=410,502;sheet=Image.new('RGB',(w*3,(h+25)*2),(18,17,23));draw=ImageDraw.Draw(sheet)
 for i,(name,frame) in enumerate([('Poke',20),('Squish',80),('Stretch',160),('Circling',250),('Recovered',320),('Sleep',380)]):
  values=struct.unpack('<'+str(w*h)+'H',data[frame*w*h*2:(frame+1)*w*h*2])
  im=Image.new('RGB',(w,h));im.putdata([(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in values]);im.save(out/(name.lower()+'-preview.png'))
  x=i%3*w;y=i//3*(h+25);sheet.paste(im,(x,y+25));draw.text((x+16,y+5),name,fill='white')
 sheet.save(out/'slime-contact-sheet.png')
print(out)
check_budget(ROOT,out)
