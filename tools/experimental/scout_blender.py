"""Mecha Scout art master. Blender geometry/materials, not a firmware asset yet."""
import bpy, math, os, sys
from mathutils import Vector
from pathlib import Path
OUT=Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
M={}
def material(name,color,rough=.32,coat=.18,metal=0):
 m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF')
 p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
 p.inputs['Coat Weight'].default_value=coat;p.inputs['Coat Roughness'].default_value=.22
 M[name]=m;return m
material('Porcelain',(.87,.82,.70),.30,.24)
material('Ivory edge',(.97,.92,.81),.30,.22)
material('Peach',(.72,.32,.12),.46,.10)
material('Inner ear',(.58,.19,.07),.55,.04)
material('Blue',(.025,.095,.34),.26,.32)
material('Red',(.62,.035,.027),.28,.25)
material('Gold',(.83,.39,.045),.31,.16)
material('Eyes',(.006,.009,.012),.16,.38)
material('Rubber',(.009,.013,.018),.55,.04)
material('Eye white',(.96,.93,.82),.24,.18)
material('Mouth',(.22,.070,.027),.55,0)
material('Vent',(.065,.070,.075),.60,0)

def pt(v):return (v[0],-v[2],v[1])
def newmesh(name,verts,faces,mat,smooth=True,bevel=0):
 mesh=bpy.data.meshes.new(name);mesh.from_pydata([pt(v) for v in verts],[],faces);mesh.update()
 o=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(o);o.data.materials.append(M[mat])
 for p in mesh.polygons:p.use_smooth=smooth
 if bevel:
  b=o.modifiers.new('Soft manufactured edges','BEVEL');b.width=bevel;b.segments=3
  w=o.modifiers.new('Weighted surface normals','WEIGHTED_NORMAL');w.keep_sharp=True
 return o

def sphere(name,mat,c,r,segments=48,rings=24,power=1,angle=0):
 v=[];f=[];a=math.radians(angle);ca,sa=math.cos(a),math.sin(a)
 def sp(x):return math.copysign(abs(x)**power,x)
 for iy in range(rings+1):
  lat=-math.pi/2+math.pi*iy/rings
  for ix in range(segments):
   lon=2*math.pi*ix/segments;x=r[0]*sp(math.cos(lat)*math.cos(lon));y=r[1]*sp(math.sin(lat));z=r[2]*sp(math.cos(lat)*math.sin(lon))
   v.append((c[0]+x*ca-y*sa,c[1]+x*sa+y*ca,c[2]+z))
 for iy in range(rings):
  for ix in range(segments):
   p=iy*segments+ix;q=iy*segments+(ix+1)%segments;rr=(iy+1)*segments+(ix+1)%segments;s=(iy+1)*segments+ix
   f.append((p,s,rr,q))
 return newmesh(name,v,f,mat)

def box(name,mat,c,size,bevel=.04,angle=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=pt(c));o=bpy.context.object;o.name=name;o.dimensions=(size[0],size[2],size[1]);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 o.rotation_euler[1]=math.radians(angle);o.data.materials.append(M[mat]);b=o.modifiers.new('Rounded edges','BEVEL');b.width=bevel;b.segments=5;o.modifiers.new('Surface normals','WEIGHTED_NORMAL');return o

def polygon(name,mat,outline,z,depth,bevel=.02):
 if sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(outline,outline[1:]+outline[:1]))<0:outline=list(reversed(outline))
 n=len(outline);v=[(x,y,z) for x,y in outline]+[(x,y,z-depth) for x,y in outline]
 f=[tuple(range(n)),tuple(reversed(range(n,2*n)))]+[(i,i+n,(i+1)%n+n,(i+1)%n) for i in range(n)]
 return newmesh(name,v,f,mat,False,bevel)

def line(name,mat,points,radius=.012):
 curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.resolution_u=12;curve.bevel_depth=radius;curve.bevel_resolution=4
 s=curve.splines.new('BEZIER');s.bezier_points.add(len(points)-1)
 for p,co in zip(s.bezier_points,points):p.co=pt(co);p.handle_left_type='AUTO';p.handle_right_type='AUTO'
 o=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(o);o.data.materials.append(M[mat]);return o

# A single open-face helmet shell. Its silhouette and visor opening are modeled,
# rather than built from overlapping earpad-shaped primitives.
head_y=2.02
verts=[];faces=[];segments=96;rings=24
for j in range(rings+1):
 t=j/rings
 for i in range(segments):
  th=2*math.pi*i/segments;distance=abs((th+math.pi)%(2*math.pi)-math.pi)
  if distance<.70:lo=.16+.05*math.cos(distance/.70*math.pi/2)
  elif distance<1.1:
   u=(distance-.70)/.40;u=u*u*(3-2*u);lo=.16*(1-u)-.74*u
  else:lo=-.74
  phi=lo+(math.pi/2-lo)*t
  x=.89*math.cos(phi)*math.sin(th);y=.69*math.sin(phi);z=.55*math.cos(phi)*math.cos(th)
  verts.append((x,head_y+y,z-.01))
for j in range(rings):
 for i in range(segments):
  a=j*segments+i;b=j*segments+(i+1)%segments;c=(j+1)*segments+(i+1)%segments;d=(j+1)*segments+i
  faces.append((a,b,c,d))
helmet=newmesh('Helmet shell',verts,faces,'Porcelain')
solid=helmet.modifiers.new('Armor thickness','SOLIDIFY');solid.thickness=.065;solid.offset=-1
bev=helmet.modifiers.new('Soft visor rim','BEVEL');bev.width=.016;bev.segments=3

sphere('Face','Peach',(0,head_y-.10,.255),(.70,.435,.37),64,32,.70)
# Ears with a tapered rounded outline and a sculpted inset.
for side in [-1,1]:
 sphere('Ear','Peach',(side*.48,head_y+.87,-.095),(.19,.56,.14),48,24,.95,-side*18)
 sphere('Ear inset','Inner ear',(side*.50,head_y+.91,.023),(.106,.390,.026),40,24,.95,-side*18)
 # Angular cheek armor and side vent cassette.
 x=side*.73
 polygon('Cheek guard','Ivory edge',[(x-side*.11,head_y-.07),(x+side*.08,head_y+.04),(x+side*.17,head_y-.03),(x+side*.13,head_y-.44),(x-side*.02,head_y-.49),(x-side*.12,head_y-.40)],.435,.27,.048)
 box('Vent housing','Porcelain',(side*.88,head_y+.005,-.015),(.235,.54,.35),.062)
 for h in [-.14,0,.14]:box('Side vent','Rubber',(side*.92,head_y+h,.174),(.070,.082,.025),.012)
 box('Brow vent surround','Rubber',(side*.50,head_y+.445,.361),(.19,.115,.043),.018,-side*14)
 box('Brow vent inset','Vent',(side*.50,head_y+.446,.389),(.115,.05,.012),.008,-side*14)
# Crest: defined center armor rather than a rounded pill.
polygon('Crest frame','Ivory edge',[(-.20,head_y+.38),(-.17,head_y+.72),(.17,head_y+.72),(.20,head_y+.38)],.410,.24,.035)
polygon('Coral crest','Red',[(-.15,head_y+.45),(-.125,head_y+.29),(0,head_y+.18),(.125,head_y+.29),(.15,head_y+.45),(.115,head_y+.57),(-.115,head_y+.57)],.526,.125,.032)
box('Antenna socket','Porcelain',(.79,head_y+.37,-.09),(.12,.18,.12),.025,-10)
sphere('Antenna','Blue',(.84,head_y+.61,-.10),(.033,.26,.033),24,16,1,-11)
# Facial features follow the curve of the muzzle.
for side in [-1,1]:
 sphere('Eye white','Eye white',(side*.29,head_y-.035,.589),(.18,.235,.067),48,28,1,-side*6)
 sphere('Pupil','Eyes',(side*.29+.022,head_y+.00,.654),(.101,.166,.039),48,28)
 # Small authored catchlight survives a lighting bake and small-screen viewing.
 sphere('Catchlight','Eye white',(side*.29+.001,head_y+.090,.690),(.019,.030,.009),24,12)
polygon('Nose','Eyes',[(-.070,head_y-.195),(-.051,head_y-.255),(0,head_y-.287),(.051,head_y-.255),(.070,head_y-.195)],.655,.052,.025)
smile=[(-.36,head_y-.265,.567),(-.23,head_y-.325,.598),(0,head_y-.344,.613),(.23,head_y-.325,.598),(.36,head_y-.265,.567)]
line('Smile crease','Mouth',smile,.012)
for x in [-.24,-.08,.08,.24]:
 y=head_y-.313-.024*(1-(x/.3)**2);z=.606-.036*(abs(x)/.3)
 polygon('Tooth','Eye white',[(x-.058,y+.01),(x-.016,y-.050),(x+.015,y-.057),(x+.068,y+.003)],z,.025,.012)

# Compact articulated armor below the large expressive head.
box('Torso','Blue',(0,1.18,0),(.77,.49,.41),.095)
polygon('Neck collar','Gold',[(-.245,1.455),(-.20,1.34),(.20,1.34),(.245,1.455)],.215,.26,.035)
box('Waist','Red',(0,.91,0),(.59,.23,.35),.055)
polygon('Chest shield','Blue',[(-.155,1.38),(-.14,1.08),(-.09,.99),(.09,.99),(.14,1.08),(.155,1.38)],.273,.065,.03)
for side in [-1,1]:
 box('Chest vent frame','Gold',(side*.258,1.20,.236),(.182,.188,.065),.018)
 box('Chest vent recess','Rubber',(side*.258,1.20,.273),(.138,.137,.012),.008)
 for h in [-.048,0,.048]:box('Chest vent blade','Gold',(side*.258,1.20+h,.284),(.144,.023,.020),.006)
 box('Hip skirt','Ivory edge',(side*.262,.73,.12),(.265,.34,.28),.043,-side*10)
 box('Skirt vent','Gold',(side*.266,.75,.272),(.156,.165,.028),.016,-side*10)
 for h in [-.05,0,.05]:box('Skirt vent rib','Gold',(side*.266,.75+h,.293),(.138,.017,.016),.005,-side*10)
box('Belt center','Ivory edge',(0,.74,.17),(.245,.31,.27),.035)
box('Belt red badge','Red',(0,.76,.315),(.146,.175,.025),.020)
line('Belt chevron','Gold',[(-.048,.806,.336),(0,.771,.342),(.048,.806,.336)],.012)
# Defined shoulder plates, forearms, cuffs, and four small fingers per hand.
for side in [-1,1]:
 sphere('Shoulder joint','Rubber',(side*.52,1.34,0),(.135,.145,.15),32,20)
 box('Shoulder armor','Porcelain',(side*.54,1.40,.015),(.34,.29,.40),.077,-side*13)
 polygon('Shoulder inset','Ivory edge',[(side*.41,1.47),(side*.67,1.45),(side*.63,1.30),(side*.48,1.33)],.228,.025,.025)
 sphere('Upper arm','Porcelain',(side*.61,1.15,.02),(.115,.21,.125),32,20,1,side*12)
 sphere('Elbow joint','Rubber',(side*.665,.982,.034),(.112,.11,.112),32,20)
 box('Forearm','Ivory edge',(side*.70,.887,.078),(.27,.28,.285),.065,side*15)
 box('Forearm panel','Porcelain',(side*.705,.904,.230),(.21,.19,.028),.031,side*15)
 sphere('Palm','Rubber',(side*.74,.685,.109),(.132,.122,.13),36,22,.73)
 for k in range(3):
  sphere('Finger','Rubber',(side*(.664+k*.068),.638,.195),(.036,.078,.047),24,16,.70,side*9)
 sphere('Thumb','Rubber',(side*.63,.721,.180),(.044,.073,.050),24,16,.75,-side*23)
 # Splayed stance and chamfered boots.
 sphere('Thigh','Porcelain',(side*.255,.485,.012),(.146,.235,.16),32,22,.8,side*9)
 box('Shin armor','Porcelain',(side*.285,.304,.076),(.34,.30,.32),.059,side*8)
 sphere('Ankle side','Ivory edge',(side*.436,.274,.044),(.05,.11,.11),28,18)
 box('Boot upper','Ivory edge',(side*.293,.166,.142),(.345,.175,.455),.05,-side*4)
 box('Boot toe','Red',(side*.302,.095,.263),(.405,.18,.42),.044,-side*4)
 box('Boot red inset','Red',(side*.295,.229,.232),(.19,.025,.115),.01,-side*4)

# Studio light setup for visual review of the actual source model.
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=48;scene.cycles.use_denoising=True
scene.world.color=(.06,.06,.06)
world=scene.world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs['Color'].default_value=(.11,.12,.15,1);world.node_tree.nodes['Background'].inputs['Strength'].default_value=.3

def area(name,location,power,size,color):
 bpy.ops.object.light_add(type='AREA',location=pt(location));o=bpy.context.object;o.name=name;o.data.energy=power;o.data.shape='DISK';o.data.size=size;o.data.color=color;o.rotation_euler=(Vector(pt((0,1.8,0)))-o.location).to_track_quat('-Z','Y').to_euler()
area('Large warm key',(-3,5,5),550,4.0,(1,.91,.78));area('Cool soft fill',(3,2.8,3),200,3.0,(.70,.84,1));area('Ivory rim',(-2,4,-2),700,2.3,(1,.9,.7))
bpy.ops.object.camera_add(location=pt((4,2.8,10)));camera=bpy.context.object;camera.name='Review camera';target=Vector(pt((0,1.66,0)));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=3.9;scene.camera=camera
scene.render.resolution_x=820;scene.render.resolution_y=1004;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
wn=world.node_tree;bg=wn.nodes['Background'];black=wn.nodes.new('ShaderNodeBackground');black.inputs['Strength'].default_value=0;lp=wn.nodes.new('ShaderNodeLightPath');mix=wn.nodes.new('ShaderNodeMixShader');wn.links.new(lp.outputs['Is Camera Ray'],mix.inputs[0]);wn.links.new(bg.outputs[0],mix.inputs[1]);wn.links.new(black.outputs[0],mix.inputs[2]);wn.links.new(mix.outputs[0],wn.nodes['World Output'].inputs['Surface'])
scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast';scene.render.filepath=str(OUT/'scout-master.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'scout-master.blend'))
bpy.ops.render.render(write_still=True)
print('ART_MASTER',OUT/'scout-master.blend')
