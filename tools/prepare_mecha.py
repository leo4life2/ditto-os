#!/usr/bin/env python3
"""Author an original rigid-joint Mecha Scout adaptation as live 3D geometry.

This is a review asset, separate from the installed performance-test robot.
Smooth analytic normals provide rounded toy surfaces without texture images.
"""
import json, math, sys
from pathlib import Path
from collections import defaultdict
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/vendor_a3d'))
import a3d_scene as S
scene=S.Scene()
materials={}
for name,color in [('ivory',(1,.91,.76)),('white',(1,.985,.92)),('face',(1,.60,.32)),('ear',(.86,.36,.19)),('blue',(.17,.40,.87)),('red',(.96,.25,.22)),('gold',(1,.66,.14)),('ink',(.055,.070,.09)),('vent',(.11,.14,.17))]:
 materials[name]=len(scene.materials)
 scene.materials.append(S.Material(name=name,color=color,ambient=.46,diffuse=.60,specular=.18,specular_exponent=18))

def node(name,parent=S.NONE16,t=(0,0,0)):
 i=len(scene.nodes);scene.nodes.append(S.Node(name=name,parent=parent,translation=t));return i
root=node('Scout')
body=node('Body',root,(0,.98,0))
head=node('Head',body,(0,1.07,0))
ear_l=node('Ear.L',head,(-.48,.59,-.10));ear_r=node('Ear.R',head,(.48,.59,-.10))
eyes=[node('Eye.L',head,(-.295,.02,.57)),node('Eye.R',head,(.295,.02,.57))]
mouth=node('Mouth',head,(0,-.27,.61))
arms=[];hands=[];legs=[]
for side in [-1,1]:
 a=node('Shoulder.'+str(side),body,(side*.52,.37,0));arms.append(a)
 h=node('Elbow.'+str(side),a,(side*.13,-.29,.02));hands.append(h)
 legs.append(node('Hip.'+str(side),root,(side*.24,.77,0)))
parts=defaultdict(lambda:[[],[],[]])
def add(parent,mat,positions,normals,indices):
 p,n,idx=parts[(parent,mat)];base=len(p);p.extend(positions);n.extend(normals);idx.extend(base+i for i in indices)
def qz(deg):
 a=math.radians(deg)/2;return (0,0,math.sin(a),math.cos(a))
def norm(v):
 d=math.sqrt(sum(x*x for x in v));return tuple(x/d for x in v) if d>1e-9 else (0,1,0)
def turn(v,deg):
 a=math.radians(deg);c,s=math.cos(a),math.sin(a);return (c*v[0]-s*v[1],s*v[0]+c*v[1],v[2])
def spow(x,e):return math.copysign(abs(x)**e,x)

def ell(parent,mat,c,r,segments=12,rings=6,angle=0,power=1):
 # latitude/longitude superellipsoid; analytic gradient keeps low tessellation smooth.
 p=[];n=[];idx=[]
 for iy in range(rings+1):
  lat=-math.pi/2+math.pi*iy/rings
  for ix in range(segments):
   lon=2*math.pi*ix/segments
   sphere=(math.cos(lat)*math.cos(lon),math.sin(lat),math.cos(lat)*math.sin(lon))
   v=tuple(spow(s,power) for s in sphere)
   pos=turn(tuple(v[j]*r[j] for j in range(3)),angle)
   normal=turn(norm(tuple(spow(s,2-power)/r[j] for j,s in enumerate(sphere))),angle)
   p.append(tuple(c[j]+pos[j] for j in range(3)));n.append(normal)
 for iy in range(rings):
  for ix in range(segments):
   a=iy*segments+ix;b=iy*segments+(ix+1)%segments;d=(iy+1)*segments+ix;e=(iy+1)*segments+(ix+1)%segments
   if iy:idx.extend((a,d,b))
   if iy<rings-1:idx.extend((b,d,e))
 add(parent,mat,p,n,idx)

def plate(parent,mat,outline,z,thickness=.02):
 # Rounded planar motifs with a shallow bevel, no alpha/material texture.
 p=[(x,y,z) for x,y in outline];cx=sum(x for x,y in outline)/len(outline);cy=sum(y for x,y in outline)/len(outline)
 N=len(p);p.append((cx,cy,z+thickness));n=[norm((x-cx,y-cy,1.8)) for x,y in outline]+[(0,0,1)];idx=[]
 for i in range(N):idx.extend((N,i,(i+1)%N))
 add(parent,mat,p,n,idx)

# Body and armor: compact color blocks with rounded edges.
ell(body,'blue',(0,.23,0),(.44,.34,.27),12,6,power=.60)
ell(body,'red',(0,-.12,0),(.34,.14,.22),10,4,power=.60)
ell(body,'gold',(0,.52,.10),(.22,.07,.20),10,4,power=.55)
ell(body,'ivory',(0,-.27,.04),(.44,.17,.25),12,4,power=.60)
for side in [-1,1]:
 ell(body,'gold',(side*.255,.22,.277),(.105,.115,.025),8,4,power=.45)
 for h in [-.048,.005,.06]:
  ell(body,'vent',(side*.255,.22+h,.307),(.080,.010,.012),6,2,power=.5)
ell(body,'blue',(0,.19,.27),(.14,.22,.07),8,4,power=.55)
ell(body,'red',(0,-.26,.301),(.09,.10,.035),8,4,power=.6)
plate(body,'gold',[(-.065,-.215),(0,-.26),(.065,-.215),(0,-.285)],.341)

# Oversized friendly head, helmet, ears and a face that projects through the shell.
ell(head,'ivory',(0,.095,-.02),(.90,.64,.52),20,10,power=.88)
ell(head,'face',(0,-.095,.26),(.71,.455,.37),20,10,power=.78)
for side in [-1,1]:
 ell(head,'ivory',(side*.755,-.125,.30),(.17,.405,.25),10,6,angle=-side*9,power=.62)
 ell(head,'ivory',(side*.925,.025,-.025),(.17,.36,.22),10,6,power=.65)
 for yy in [-.14,.005,.15]:
  ell(head,'vent',(side*.91,yy,.195),(.05,.048,.025),6,2,power=.50)
 # brow vents sit on the forward dome.
 ell(head,'ink',(side*.52,.455,.35),(.115,.060,.025),8,4,angle=-side*12,power=.60)
 ell(head,'vent',(side*.52,.455,.38),(.07,.026,.009),8,2,angle=-side*12,power=.5)
# Helmet central crest and raised frame.
ell(head,'ivory',(0,.57,.19),(.21,.22,.29),8,4,power=.55)
ell(head,'red',(0,.405,.474),(.14,.21,.11),8,4,power=.6)
ell(head,'blue',(.84,.56,-.02),(.035,.31,.035),8,4,angle=-12)
ell(head,'ivory',(.80,.33,-.02),(.07,.09,.065),8,4)
for side,en in zip([-1,1],[ear_l,ear_r]):
 ell(en,'face',(side*.05,.32,0),(.20,.56,.13),12,6,angle=-side*15)
 ell(en,'ear',(side*.058,.34,.09),(.11,.40,.045),10,6,angle=-side*15)
# Eyes are independent rigid nodes, allowing squints/blinks without frame assets.
for en in eyes:
 ell(en,'white',(0,0,0),(.205,.27,.065),16,8)
 ell(en,'ink',(.025,.025,.066),(.115,.195,.036),14,8)
 ell(en,'white',(-.008,.11,.10),(.027,.043,.012),8,4)
ell(head,'ink',(0,-.21,.653),(.087,.052,.045),10,4,power=.85)
# A curved smile with four small triangular teeth; expressions can scale/tilt it.
smile=[]
for i in range(15):
 x=-.37+.74*i/14;y=-.015-.083*(1-(x/.37)**2);smile.append((x,y))
for i in reversed(range(15)):
 x=-.37+.74*i/14;y=-.040-.16*(1-(x/.37)**2);smile.append((x,y))
plate(mouth,'ear',smile,.0,.01)
for x in [-.255,-.085,.085,.255]:
 y=-.025-.083*(1-(x/.37)**2)
 plate(mouth,'white',[(x-.060,y),(x,y-.061),(x+.065,y-.007)],.025,.005)

for side,a,h,l in zip([-1,1],arms,hands,legs):
 ell(a,'ivory',(side*.02,0,0),(.215,.20,.24),10,6,power=.67)
 ell(a,'ivory',(side*.065,-.20,0),(.115,.19,.12),10,4,angle=side*14,power=.7)
 ell(h,'ivory',(side*.035,-.115,.035),(.16,.20,.16),10,4,power=.65)
 ell(h,'ink',(side*.075,-.34,.06),(.14,.13,.135),10,4,power=.8)
 ell(h,'ivory',(side*.01,-.12,.19),(.115,.12,.025),8,4,power=.5)
 ell(l,'ivory',(0,-.17,0),(.165,.23,.17),10,4,power=.7)
 ell(l,'ivory',(0,-.40,.035),(.205,.20,.21),10,4,power=.6)
 ell(l,'red',(0,-.61,.13),(.235,.12,.31),12,4,power=.55)
 ell(l,'ivory',(0,-.51,.15),(.20,.10,.235),10,4,power=.6)

# Merge stationary details by joint/material, retaining an economical rigid hierarchy.
for (joint,mat),(p,n,idx) in parts.items():
 mi=len(scene.meshes);name=scene.nodes[joint].name+'.'+mat
 scene.meshes.append(S.Mesh(name=name,positions=p,normals=n,indices=idx,material_index=materials[mat]))
 ni=node(name,joint);scene.nodes[ni].mesh_index=mi

# Smooth key poses: local interpolation stays fully live on the ESP32.
def rotation(axis,degrees):
 a=math.radians(degrees)/2;return tuple((math.sin(a) if j==axis else 0) for j in range(3))+(math.cos(a),)
def clip(name,duration,loop=True):
 c=S.Clip(name=name,duration_ms=int(duration*1000),loop=loop);scene.clips.append(c);return c
def channel(c,n,path,times,values):c.channels.append(S.Channel(n,path,S.ANIM_LINEAR,times,values))
def rot(c,n,axis,vals):channel(c,n,S.ANIM_ROTATION,[t for t,v in vals],[rotation(axis,v) for t,v in vals])
def breathe(c):
 d=c.duration_ms/1000;t=[0,d*.25,d*.5,d*.75,d]
 channel(c,body,S.ANIM_TRANSLATION,t,[(0,.98+y,0) for y in [0,.014,.028,.014,0]])
 rot(c,head,2,[(0,-1.5),(d/2,1.5),(d,-1.5)])
def blink(c,start=.7):
 for en in eyes:channel(c,en,S.ANIM_SCALE,[0,start,start+.07,start+.13,start+.21,c.duration_ms/1000],[(1,1,1),(1,1,1),(1,.06,1),(1,.06,1),(1,1,1),(1,1,1)])
for name in ['Idle','Listening','Thinking','Speaking','Wave','Yes','No','Happy','Curious','Sleep','Surprise','Point','Celebrate','Sad']:
 c=clip(name,3.2 if name not in ['Wave','Yes','No'] else 2.0)
 breathe(c)
 if name not in ['Surprise','Sleep']:blink(c)
 d=c.duration_ms/1000
 if name=='Listening':rot(c,head,2,[(0,-7),(d/2,-5),(d,-7)])
 if name=='Thinking':rot(c,head,1,[(0,-9),(d/2,9),(d,-9)]);rot(c,head,2,[(0,7),(d,7)])
 if name=='Speaking':
  channel(c,mouth,S.ANIM_SCALE,[i*d/16 for i in range(17)],[(1,.8+(i%3)*.22,1) for i in range(17)])
 if name=='Wave':
  rot(c,arms[1],2,[(0,0),(.35,78),(1.65,78),(2,0)])
  rot(c,hands[1],2,[(0,0),(.35,20),(.65,48),(.95,20),(1.25,48),(1.65,20),(2,0)])
  channel(c,arms[1],S.ANIM_TRANSLATION,[0,.35,1.65,2],[(.52,.37,0),(.52,.37,.40),(.52,.37,.40),(.52,.37,0)])
 if name=='Yes':rot(c,head,0,[(0,0),(.35,12),(.65,-7),(1,12),(1.4,-5),(2,0)])
 if name=='No':rot(c,head,1,[(0,0),(.35,-15),(.7,15),(1.05,-15),(1.4,15),(2,0)])
 if name in ['Happy','Celebrate']:
  for a,side in zip(arms,[-1,1]):rot(c,a,2,[(0,side*55),(d/2,side*80),(d,side*55)])
  rot(c,ear_l,2,[(0,-6),(d/2,8),(d,-6)]);rot(c,ear_r,2,[(0,6),(d/2,-8),(d,6)])
 if name=='Curious':rot(c,head,2,[(0,12),(d/2,9),(d,12)])
 if name=='Sleep':
  for en in eyes:channel(c,en,S.ANIM_SCALE,[0,d],[(1,.08,1),(1,.08,1)])
  rot(c,head,0,[(0,9),(d/2,12),(d,9)])
 if name=='Surprise':
  channel(c,mouth,S.ANIM_SCALE,[0,d],[(.48,1.5,1),(.48,1.5,1)])
  for en in eyes:channel(c,en,S.ANIM_SCALE,[0,d],[(1.1,1.1,1),(1.1,1.1,1)])
 if name=='Point':rot(c,arms[1],2,[(0,65),(d,65)])
 if name=='Sad':rot(c,head,0,[(0,13),(d,13)]);rot(c,ear_l,2,[(0,24),(d,24)]);rot(c,ear_r,2,[(0,-24),(d,-24)])

out=ROOT/'assets/mecha-scout.a3d';out.write_bytes(S.build_container(scene))
report={'status':'review asset; not installed or performance-qualified','source':'Original procedural adaptation of approved Mecha Scout concept; tools/prepare_mecha.py','bytes':out.stat().st_size,'vertices':sum(len(m.positions) for m in scene.meshes),'triangles':sum(len(m.indices)//3 for m in scene.meshes),'meshes':len(scene.meshes),'nodes':len(scene.nodes),'rig':'hierarchical rigid joints, no skinning; independent eyes, mouth and ears','clips':[c.name for c in scene.clips]}
(ROOT/'assets/mecha-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
# The firmware uses generated node/clip IDs rather than assumptions about a downloaded rig.
header=['#pragma once','namespace Mecha {','enum Clip { '+', '.join(c.name for c in scene.clips)+', ClipCount };',
        'inline constexpr const char* clips[]={'+','.join(json.dumps(c.name) for c in scene.clips)+'};']
for name,number in [('HeadNode',head),('EarLeft',ear_l),('EarRight',ear_r),('EyeLeft',eyes[0]),('EyeRight',eyes[1]),('MouthNode',mouth)]:header.append(f'inline constexpr int {name}={number};')
header.append(f'inline constexpr int Triangles={report["triangles"]};')
header.append('}')
(ROOT/'main/mecha_rig.h').write_text('\n'.join(header)+'\n')
(ROOT/'host/avatar_catalog.py').write_text('# Generated by tools/prepare_mecha.py\nCLIPS = '+repr(report['clips'])+'\n')
