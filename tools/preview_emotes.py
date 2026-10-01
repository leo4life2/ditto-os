"""Render the current soft rig's semantic clips and export native visual review."""
import argparse,json,subprocess,sys,shutil
from storage_hygiene import scratch_directory, check_budget
from pathlib import Path
from array import array
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from avatar_catalog import CLIPS
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
check_budget(ROOT,out)
report={'version':(ROOT/'VERSION').read_text().strip(),'clips':{},'native_size':[410,502],'frames_per_clip':150,'scope':'Production-renderer replay on the host, not a device video.'}
with scratch_directory('rig',reserve_mib=96) as tmp:
    exe=tmp/'render';raw=tmp/'clip.rgb565';combined=tmp/'clips.txt'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_rig_emote.cpp'),'-o',str(exe)],check=True)
    sheet=Image.new('RGB',(410*4,527*((len(CLIPS)+3)//4)),(18,17,23));draw=ImageDraw.Draw(sheet)
    with combined.open('w') as collection:
        for i,name in enumerate(CLIPS):
            result=subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(i)],capture_output=True,text=True,check=True)
            report['clips'][name]=result.stderr.strip()
            with raw.open('rb') as f:
                f.seek((25 if name=='Wink' else 48)*410*502*2);values=array('H',f.read(410*502*2))
                im=Image.frombytes('RGB',(410,502),bytes(c for v in values for c in ((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)))
            im.save(out/(name.lower()+'-emote.png'));x=i%4*410;y=i//4*527;sheet.paste(im,(x,y+25));draw.text((x+16,y+5),name,fill='white')
            encoded=tmp/f'{i:02}.mp4'
            subprocess.run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb565le','-video_size','410x502','-framerate','30','-i',str(raw),'-c:v','libx264','-crf','12','-pix_fmt','yuv420p','-movflags','+faststart',str(encoded)],check=True)
            shutil.copyfile(encoded,out/(name.lower()+'.mp4'))
            collection.write(f"file '{encoded.name}'\n")
            raw.unlink()
    sheet.save(out/'emotes-contact-sheet.png')
    subprocess.run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(combined),'-c','copy','-movflags','+faststart',str(out/'emotes-preview.mp4')],check=True)
(out/'emotes-render-check.json').write_text(json.dumps(report,indent=2)+'\n')
check_budget(ROOT,out)
print(out)
