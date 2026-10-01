"""Pixel-exact native renderer regression, with 300% before/after crops."""
import argparse,hashlib,json
from pathlib import Path
from array import array
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('candidate',type=Path);p.add_argument('out',type=Path);p.add_argument('--max-changed-ppm',type=float,default=0);p.add_argument('--max-channel-step',type=int,default=0);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
frame_bytes=410*502*2
ha=hashlib.sha256();hb=hashlib.sha256();frames=0;different=0;max_step=0;first=[];selected={45:'rest',98:'drag',112:'release',279:'stretch',335:'settled'};images={}
with a.reference.open('rb') as fa,a.candidate.open('rb') as fb:
 while True:
  x=fa.read(frame_bytes);y=fb.read(frame_bytes)
  if not x and not y:break
  assert len(x)==len(y)==frame_bytes
  ha.update(x);hb.update(y)
  if x!=y:
   ax=array('H',x);ay=array('H',y)
   for i,(u,v) in enumerate(zip(ax,ay)):
    if u!=v:
     different+=1
     max_step=max(max_step,abs((u>>11)-(v>>11)),abs(((u>>5)&63)-((v>>5)&63)),abs((u&31)-(v&31)))
     if len(first)<12:first.append([frames,i%410,i//410,u,v])
  if frames in selected:
   imgs=[]
   for raw in [x,y]:
    vals=array('H',raw);im=Image.new('RGB',(410,502));im.putdata([((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in vals]);imgs.append(im)
   images[selected[frames]]=imgs
  frames+=1
report={'frames':frames,'resolution':[410,502],'compared_pixels':frames*410*502,'different_pixels':different,'changed_ppm':different/(frames*410*502)*1e6,'max_rgb565_channel_step':max_step,'pixel_identical':different==0,'reference_sha256':ha.hexdigest(),'candidate_sha256':hb.hexdigest(),'first_differences':first}
(a.out/'pixel-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
# Keep crisp pixel boundaries at 300%; both images use the same nearest zoom.
rows=[]
for name,ims in images.items():
 for label,box in [('face',(150,180,270,270)),('left-surface',(45,205,165,295))]:
  row=Image.new('RGB',(760,304),'#19191e');draw=ImageDraw.Draw(row)
  for i,im in enumerate(ims):
   draw.text((10+i*380,9),f'{name} / {label} / '+('0.9.0 reference' if i==0 else 'optimized'),fill='white')
   row.paste(im.crop(box).resize((360,270),Image.Resampling.NEAREST),(10+i*380,29))
  rows.append(row)
for i in range(0,len(rows),4):
 group=rows[i:i+4];sheet=Image.new('RGB',(760,304*len(group)),'#19191e')
 for j,r in enumerate(group):sheet.paste(r,(0,j*304))
 sheet.save(a.out/f'quality-300-percent-{i//4+1}.png')
print(json.dumps(report));assert report['changed_ppm']<=a.max_changed_ppm and max_step<=a.max_channel_step,first
