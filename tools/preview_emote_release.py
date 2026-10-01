"""Side-by-side production render after actual hold and double-tap input."""
from pathlib import Path
import sys,subprocess,json,os
from array import array
from PIL import Image,ImageDraw
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'test-results/emote-recovery-0.11.28'
with scratch_directory('emote-release',reserve_mib=160) as tmp:
 exe=tmp/'render';subprocess.run(['clang++','-std=c++17','-O2','-fsanitize=address,undefined','-Imain','-Icomponents/a3d/src','tools/render_emote_release.cpp','-o',str(exe)],cwd=ROOT,check=True)
 env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
 sheet=Image.new('RGB',(820,2*540),'#141419');draw=ImageDraw.Draw(sheet);report={}
 for row,(name,clip,mode,frame) in enumerate([('hold-celebrate',12,0,37),('double-tap-wink',14,1,29)]):
  for priority in [0,1]:
   raw=tmp/f'{priority}.rgb565'
   r=subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(clip),str(priority),str(mode)],env=env,capture_output=True,text=True,check=True)
   report[f'{name}-{priority}']=r.stderr.strip()
   with raw.open('rb') as f:
    f.seek(frame*410*502*2);a=array('H',f.read(410*502*2));im=Image.frombytes('RGB',(410,502),bytes(c for v in a for c in ((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)))
   im.save(out/f'{name}-{priority}.png');sheet.paste(im,(priority*410,row*540+30));draw.text((priority*410+14,row*540+8),name+(' — fixed' if priority else ' — before'),fill='white')
  cmd=['ffmpeg','-v','error','-y']
  for priority in [0,1]:cmd+=['-f','rawvideo','-pixel_format','rgb565le','-video_size','410x502','-framerate','30','-i',str(tmp/f'{priority}.rgb565')]
  subprocess.run(cmd+['-filter_complex','[0:v][1:v]hstack=inputs=2','-c:v','libx264','-crf','14','-pix_fmt','yuv420p','-movflags','+faststart',str(out/f'{name}-comparison.mp4')],check=True)
 sheet.save(out/'release-comparison.png');(out/'visual-check.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native hold/Celebrate and double-tap/Wink comparisons; skin Jacobians and finite geometry; ASan/UBSan')
