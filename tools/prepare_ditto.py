"""Build a smooth, genuinely three-dimensional Ditto for the local prototype.

No sprites, baked animation frames, or printing mesh. The closed implicit
surface shares smooth gradient normals; the face follows the same surface.
Run with ordinary Python. The resulting asset uses the OS's semantic clips.
"""
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor_a3d'))
import a3d_scene as S

PI = math.pi
CLIPS = ['Idle', 'Listening', 'Thinking', 'Speaking', 'Wave', 'Yes', 'No',
         'Happy', 'Curious', 'Sleep', 'Surprise', 'Point', 'Celebrate', 'Sad', 'Wink', 'Giggle']
# Smooth implicit union. Its analytic gradient supplies one continuous normal
# field, including the shoulder joins. No intersecting balls or hard seams.
LOBES = [(0,-.17,0,.90,.67,.58,0),
         (-.22,.45,0,.43,.39,.48,0),(.26,.45,0,.40,.39,.48,0),
         (-.75,.32,-.02,.31,.29,.30,.05),(.83,.02,-.01,.30,.26,.31,-.20),
         (-.60,-.66,.025,.43,.20,.38,0),(.59,-.66,.025,.42,.20,.38,0)]
def field(x,y,z,gradient=False):
    value=0;gx=gy=gz=0
    for cx,cy,cz,rx,ry,rz,angle in LOBES:
        c,s=math.cos(angle),math.sin(angle)
        dx,dy,dz=x-cx,y-cy,z-cz
        u=c*dx+s*dy;v=-s*dx+c*dy
        e=math.exp(1-(u/rx)**2-(v/ry)**2-(dz/rz)**2)
        value+=e
        gx+=2*e*(u*c/(rx*rx)-v*s/(ry*ry))
        gy+=2*e*(u*s/(rx*rx)+v*c/(ry*ry))
        gz+=2*e*dz/(rz*rz)
    return (gx,gy,gz) if gradient else value

def surface(t, p):
    d=(math.sin(2*PI*t)*math.sin(p),math.cos(2*PI*t)*math.sin(p),math.cos(p))
    return surface_direction(d)

def surface_direction(d):
    lo,hi=0,2.5
    for _ in range(30):
        r=(lo+hi)*.5
        if field(*(r*v for v in d))>1:lo=r
        else:hi=r
    return tuple((lo+hi)*.5*v for v in d)

def normalized(v):
    n = math.sqrt(sum(x*x for x in v))
    return tuple(x/n for x in v)

def normal(t,p):
    return normalized(field(*surface(t,p),gradient=True))

# Used only by the exporter to seat the face on the curved skin.
def front_z(x,y):
    lo,hi=0,2
    for _ in range(30):
        z=(lo+hi)*.5
        if field(x,y,z)>1:lo=z
        else:hi=z
    return (lo+hi)*.5

scene = S.Scene()
scene.materials = [S.Material(name='Soft lilac', color=(.87,.53,.98),
    ambient=.32,diffuse=.53,specular=.14,specular_exponent=10),
    S.Material(name='Ink',color=(.07,.025,.09),ambient=1,diffuse=0,specular=0)]
# Ground-anchored squash and stretch moves the face with the body, without a
# separate mouth floating in front or joints opening seams in the blob.
scene.nodes = [S.Node(name='Motion'), S.Node(name='Soft body',parent=0,translation=(0,-.82,0))]
def mesh(name,p,n,idx,mat,parent=1,translation=(0,.82,0)):
    mi=len(scene.meshes)
    scene.meshes.append(S.Mesh(name=name,positions=p,normals=n,indices=idx,material_index=mat))
    ni=len(scene.nodes)
    scene.nodes.append(S.Node(name=name,parent=parent,mesh_index=mi,translation=translation))
    return ni

g=(1+math.sqrt(5))/2
directions=[normalized(v) for v in [(-1,g,0),(1,g,0),(-1,-g,0),(1,-g,0),(0,-1,g),(0,1,g),(0,-1,-g),(0,1,-g),(g,0,-1),(g,0,1),(-g,0,-1),(-g,0,1)]]
faces=[(0,11,5),(0,5,1),(0,1,7),(0,7,10),(0,10,11),(1,5,9),(5,11,4),(11,10,2),(10,7,6),(7,1,8),(3,9,4),(3,4,2),(3,2,6),(3,6,8),(3,8,9),(4,9,5),(2,4,11),(6,2,10),(8,6,7),(9,8,1)]
p=[surface_direction(d) for d in directions]
n=[normalized(field(*v,gradient=True)) for v in p]
# Spend vertices on contour/curvature, not thousands of near-identical points
# in the flat cheeks. Split shared edges together, keeping a closed manifold.
for iteration in range(7):
    edges={tuple(sorted((f[i],f[(i+1)%3]))) for f in faces for i in range(3)}
    split={}
    for a,b in sorted(edges):
        d=normalized(tuple(directions[a][k]+directions[b][k] for k in range(3)))
        v=surface_direction(d);nv=normalized(field(*v,gradient=True))
        error=math.sqrt(sum((v[k]-(p[a][k]+p[b][k])*.5)**2 for k in range(3)))
        front=d[2]>-.25
        tolerance=(.005 if abs(nv[2])<.45 else .014) if front else .05
        cosine=math.cos(math.radians(22 if front else 45))
        ndot=sum(n[a][k]*n[b][k] for k in range(3))
        # Gloss exposes bends missed by endpoint-normal tests. Bound the
        # midpoint interpolation error too, especially at the foot/body join.
        average=normalized(tuple(n[a][k]+n[b][k] for k in range(3)))
        midpoint_error=sum(nv[k]*average[k] for k in range(3))
        if iteration<1 or error>tolerance or ndot<cosine or (front and midpoint_error<math.cos(math.radians(2.5))):
            split[a,b]=len(p);p.append(v);n.append(nv);directions.append(d)
    if not split:break
    refined=[]
    for a,b,c in faces:
        ab=split.get(tuple(sorted((a,b))));bc=split.get(tuple(sorted((b,c))));ca=split.get(tuple(sorted((c,a))))
        count=sum(v is not None for v in (ab,bc,ca))
        if count==0:refined.append((a,b,c))
        elif count==3:refined.extend(((a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)))
        elif count==1:
            if bc is not None:a,b,c,ab=b,c,a,bc
            elif ca is not None:a,b,c,ab=c,a,b,ca
            refined.extend(((a,ab,c),(ab,b,c)))
        else:
            if ca is None:a,b,c,ab,ca=b,c,a,bc,ab
            elif ab is None:a,b,c,ab,ca=c,a,b,ca,bc
            refined.extend(((a,ab,ca),(ab,b,c),(ab,c,ca)))
    faces=refined
# Every undirected body edge must have exactly two incident faces.
from collections import Counter
assert set(Counter(tuple(sorted((f[i],f[(i+1)%3]))) for f in faces for i in range(3)).values())=={2}
idx=[j for f in faces for j in f]
# Draw front to back; the depth buffer still handles every animated orientation.
tri=[idx[i:i+3] for i in range(0,len(idx),3)]
tri.sort(key=lambda t:sum(p[j][2] for j in t),reverse=True)
body=mesh('Continuous blob',p,n,[j for t in tri for j in t],0)

eyes=[]
for side,x in [('Left',-.27),('Right',.27)]:
    y=.40;z=front_z(x,y)+.009
    ep=[(0,0,0)];en=[(0,0,1)];ei=[]
    for i in range(20):
        a=2*PI*i/20;dx=.026*math.cos(a);dy=.032*math.sin(a)
        ep.append((dx,dy,front_z(x+dx,y+dy)+.012-z));en.append((0,0,1))
    for i in range(20):ei += [0,i+1,(i+1)%20+1]
    eyes.append(mesh(side+' eye',ep,en,ei,1,translation=(x,.82+y,z)))

# A narrow smile conforming to the surface, with round ends; enough segments
# for a clean curve at 410 x 502 without spending geometry on tiny black dots.
mp=[];mn=[];mi=[]
my=.17;mz=front_z(0,my)+.017
steps=24
for j in range(steps+1):
    x=-.18+.36*j/steps
    y=my+.050*(x/.18)**2
    slope=2*.050*x/(.18*.18)
    norm=math.sqrt(1+slope*slope)
    for sign in (-1,1):
        xx=x-sign*.009*slope/norm;yy=y+sign*.009/norm
        mp.append((xx,yy-my,front_z(xx,yy)+.017-mz));mn.append((0,0,1))
for j in range(steps):
    a=j*2;mi += [a,a+2,a+1,a+1,a+2,a+3]
mouth=mesh('Little smile',mp,mn,mi,1,translation=(0,.82+my,mz))

def quat(rx=0,ry=0,rz=0):
    # Euler XYZ -> quaternion xyzw.
    sx,cx=math.sin(rx/2),math.cos(rx/2);sy,cy=math.sin(ry/2),math.cos(ry/2);sz,cz=math.sin(rz/2),math.cos(rz/2)
    return (sx*cy*cz+cx*sy*sz,cx*sy*cz-sx*cy*sz,cx*cy*sz+sx*sy*cz,cx*cy*cz-sx*sy*sz)

for name in CLIPS:
    duration=4
    times=[i/30 for i in range(duration*30+1)]
    channels={(node,path):[] for node in (0,1,*eyes,mouth) for path in (0,1,2)}
    for t in times:
        w=2*PI*t/duration
        sy=1+.023*math.sin(w);sx=1/math.sqrt(sy);sz=sx
        rx=ry=0;rz=.018*math.sin(w);tx=ty=0;blink=1;mouth_y=1
        # One blink with eased lids, over a breathing loop.
        blink=1-.94*max(0,1-abs(t-2.65)/.11)**2
        envelope=math.sin(PI*t/duration)**2
        if name=='Listening':
            sy+=.035;sx-=.015;rz=.065+.015*math.sin(w);rx=-.025
        elif name=='Thinking':
            ry=.13*math.sin(w);rz=.075*math.sin(w);sy+=.018
        elif name=='Speaking':
            sy+=.035*math.sin(4*w);sx-=.014*math.sin(4*w);mouth_y=1+.32*math.sin(4*w)**2
        elif name=='Wave':
            rz+=.16*math.sin(3*w)*envelope;sy+=.055*envelope
        elif name=='Yes':rx=.14*math.sin(3*w)*envelope;sy-=.08*math.sin(3*w)*envelope
        elif name=='No':ry=.25*math.sin(3*w)*envelope
        elif name in ('Happy','Celebrate'):
            b=max(0,math.sin((3 if name=='Happy' else 4)*w))
            ty=.18*b*envelope;sy+=.075*b*envelope;rz=.08*math.sin(2*w)*envelope
        elif name=='Curious':rz=.12*envelope;ry=.10*envelope;sy+=.04*envelope
        elif name=='Sleep':sy=.99+.006*math.sin(w);sx=1/math.sqrt(sy);sz=sx;rz=0;blink=.10
        elif name=='Surprise':sy+=.22*envelope;sx-=.09*envelope;ty=.035*envelope;mouth_y=1+2*envelope
        elif name=='Point':rz=-.17*envelope;tx=.06*envelope;sy+=.06*envelope
        elif name=='Sad':sy=.90+.018*math.sin(w);sx=1.06;rz=.025;blink=.85
        elif name=='Wink':rz=.06*envelope;sy+=.025*envelope
        elif name=='Giggle':
            sy+=.05*math.sin(6*w)*envelope;rz=.04*math.sin(5*w)*envelope;ty=.045*abs(math.sin(3*w))*envelope
        for node in (0,1,*eyes,mouth):
            base=scene.nodes[node]
            tr=base.translation;rot=(0,0,0,1);sc=(1,1,1)
            if node==0:tr=(tx,ty,0);rot=quat(rx,ry,rz)
            if node==1:sc=(sx,sy,sz)
            if node in eyes:sc=(1,blink,1)
            if node==mouth:sc=(1,mouth_y,1)
            for path,value in ((0,tr),(1,rot),(2,sc)):channels[node,path].append(value)
    clip=S.Clip(name=name,duration_ms=duration*1000,loop=True)
    for (node,path),values in channels.items():
        constant=all(v==values[0] for v in values)
        clip.channels.append(S.Channel(node,path,S.ANIM_LINEAR,[0,duration] if constant else times,[values[0]]*2 if constant else values))
    scene.clips.append(clip)

out=ROOT/'assets/avatar.a3d';out.write_bytes(S.build_container(scene))
report={'id':'ditto-prototype','source':'Procedural fan model of Pokémon Ditto; local prototyping',
    'generator':'tools/prepare_ditto.py','bytes':out.stat().st_size,
    'triangles':sum(len(m.indices)//3 for m in scene.meshes),
    'vertices':sum(len(m.positions) for m in scene.meshes),'meshes':len(scene.meshes),
    'nodes':len(scene.nodes),'clips':CLIPS,'mouth_animation':True,
    'geometry':'Closed implicit blob with smooth analytic gradient normals',
    'animation':'Live node transforms with interruptible squash/stretch, gaze and blinking',
    'status':'Procedural asset and live face morphs; see test-results/ditto-emotes-0.6.0 for device evidence'}
(ROOT/'assets/avatar-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
header=['#pragma once','// Generated by tools/prepare_ditto.py.','namespace Avatar {',
    'inline constexpr const char* Id="ditto-prototype";',
    'enum Clip { '+', '.join(CLIPS)+', ClipCount };',
    'inline constexpr const char* clips[]={'+','.join(json.dumps(c) for c in CLIPS)+'};']
for key,value in [('HeadNode',1),('EarLeft',-1),('EarRight',-1),('EyeLeft',eyes[0]),('EyeRight',eyes[1]),('MouthNode',mouth),('Triangles',report['triangles'])]:
    header.append(f'inline constexpr int {key}={value};')
header.append('}')
(ROOT/'main/avatar_asset.h').write_text('\n'.join(header)+'\n')
(ROOT/'host/avatar_catalog.py').write_text('# Generated avatar action contract\nCLIPS = '+repr(CLIPS)+'\n')
print(json.dumps(report,indent=2))

# Small deterministic morph targets seated on the same implicit skin. Their
# topology matches the live face meshes; no sprites or runtime text textures.
def v3(v):
    def f(x):
        s=format(float(x),'.9g')
        return s+('f' if '.' in s or 'e' in s else '.0f')
    return '{'+','.join(f(x) for x in v)+'}'
face_header=['#pragma once','// Generated by tools/prepare_ditto.py.','namespace DittoFace {','struct V { float x,y,z; };']
def emit(name,vs):face_header.append('inline constexpr V '+name+'[] = {'+','.join(map(v3,vs))+'};')
cross=[]
for eye in eyes:
    node=scene.nodes[eye];x,y,z=node.translation;y-=.82
    for i in range(21):
        if i==0:cx=cy=0
        else:
            a=2*PI*(i-1)/20;c=math.cos(a);s=math.sin(a)
            u=abs((c+s)/math.sqrt(2));v=abs((c-s)/math.sqrt(2))
            radius=max(min(.076/max(u,1e-6),.018/max(v,1e-6)),min(.018/max(u,1e-6),.076/max(v,1e-6)))
            cx=radius*c;cy=radius*s
        cross.append((cx,cy,front_z(x+cx,y+cy)+.018-z))
emit('Cross',cross)
flat=[];round_mouth=[];smirk=[]
for j in range(steps+1):
    for sign in (-1,1):
        x=-.16+.32*j/steps;y=my+.007*math.sin(3*PI*j/steps)+sign*.020
        flat.append((x,y-my,front_z(x,y)+.020-mz))
        # Keep vertices in the smile strip's left-to-right order throughout
        # the morph. A ring target crossed/collapsed triangles halfway open.
        u=-1+2*j/steps;x=.056*u;y=my+sign*.068*math.sqrt(max(0,1-u*u))
        round_mouth.append((x,y-my,front_z(x,y)+.020-mz))
        x=-.18+.36*j/steps;y=my+.050*(x/.18)**2+.035*x/.18+sign*.009
        smirk.append((x,y-my,front_z(x,y)+.020-mz))
emit('FlatMouth',flat);emit('RoundMouth',round_mouth);emit('Smirk',smirk)
# Re-seat dot eyes and mouth after their final transform on the actual skin.
bm=scene.meshes[0];triangles=[bm.indices[i:i+3] for i in range(0,len(bm.indices),3)]
def skin_z(x,y):
    depth=-100
    for ia,ib,ic in triangles:
        a,b,c=(bm.positions[i] for i in (ia,ib,ic))
        if not min(a[0],b[0],c[0])<=x<=max(a[0],b[0],c[0]) or not min(a[1],b[1],c[1])<=y<=max(a[1],b[1],c[1]):continue
        den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-9:continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den
        v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den
        if u>=0 and v>=0 and u+v<=1:depth=max(depth,u*a[2]+v*b[2]+(1-u-v)*c[2])
    return depth if depth>-99 else front_z(x,y)
values=[skin_z(-.7+1.4*x/24,-.10+.85*y/24) for y in range(25) for x in range(25)]
face_header.append('inline constexpr float Surface[]={'+','.join(format(v,'.9f')+'f' for v in values)+'};')
face_header.append("""inline float surface(float x,float y) {
  float u=std::clamp((x+.7f)*(24/1.4f),0.f,23.999f),v=std::clamp((y+.10f)*(24/.85f),0.f,23.999f);
  int ix=int(u),iy=int(v);u-=ix;v-=iy;int k=iy*25+ix;
  float a=Surface[k]+(Surface[k+1]-Surface[k])*u;
  float b=Surface[k+25]+(Surface[k+26]-Surface[k+25])*u;
  return a+(b-a)*v;
}""")
face_header.append('}')
(ROOT/'main/ditto_face.h').write_text('\n'.join(face_header)+'\n')
