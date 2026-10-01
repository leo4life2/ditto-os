"""Run inside Blender. Convert the assembled printing model into rigid live 3D.

The copied 3MF and high-resolution master remain unchanged. The exported scene
uses interchangeable semantic clip/node IDs, not model-specific firmware code.
"""
import bpy,bmesh,math,json,sys
from pathlib import Path
from mathutils import Matrix,Vector
from collections import defaultdict
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT.parent/'modeling/imported-labubu'
sys.path.insert(0,str(ROOT/'tools/vendor_a3d'))
import a3d_scene as S
# Engineering LOD: keep native raster resolution while bounding geometry cost.
# The printing master stays intact; future avatars use the same asset contract.
BUDGET_SCALE = .30
bpy.ops.wm.open_mainfile(filepath=str(SRC/'labubu-rigged.blend'))
rig=bpy.data.objects['Labubu rig'];rig.animation_data.action=None
for pb in rig.pose.bones:pb.matrix_basis=Matrix.Identity(4)
bpy.context.view_layer.update()
scene=S.Scene();C=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
bone_ids={};rest={b.name:Matrix.Translation(b.head_local) for b in rig.data.bones}
for b in rig.data.bones:
 parent=bone_ids[b.parent.name] if b.parent else S.NONE16
 local=(rest[b.parent.name].inverted()@rest[b.name]) if b.parent else rest[b.name]
 t=C@local.translation
 bone_ids[b.name]=len(scene.nodes);scene.nodes.append(S.Node(name=b.name,parent=parent,translation=tuple(t)))
# Mouth is a fixed printed grin; the avatar advertises that instead of falsely
# treating the teeth as a speech-ready jaw. Conversation uses body/ear motion.
materials={};part_report=[];parts=defaultdict(lambda:[[],[],[],{}])
def srgb(v):return 12.92*v if v<=.0031308 else 1.055*v**(1/2.4)-.055
def material(m):
 if m.name not in materials:
  p=m.node_tree.nodes.get('Principled BSDF');rgb=p.inputs['Base Color'].default_value[:3]
  materials[m.name]=len(scene.materials);scene.materials.append(S.Material(name=m.name,color=tuple(srgb(v) for v in rgb),ambient=.42,diffuse=.64,specular=.12,specular_exponent=20))
 return materials[m.name]
budgets={4:400,8:220,65:320,77:96,81:96,71:80,73:80,38:64,42:64,52:32,62:140,33:160,19:40,56:48,58:56,60:56,14:80,21:80,23:32,27:32,46:24,50:24,48:48,54:48,2:20,29:16,44:16,87:48,6:40,10:40}
objects=[o for o in bpy.data.objects if o.type=='MESH' and not o.hide_render]
for original in objects:
 sid=original.get('source_object_id',-1);bone=original.vertex_groups[0].name
 if sid==71:bone='gaze.R'
 if sid==73:bone='gaze.L'
 target=budgets.get(sid,70 if 'armor' in original.name.lower() else 24)
 target=max(12,round(target*BUDGET_SCALE))
 low=bpy.data.objects.new('Device '+original.name,original.data.copy());bpy.context.collection.objects.link(low)
 low.matrix_world=original.matrix_world.copy();bpy.context.view_layer.objects.active=low
 for o in bpy.context.selected_objects:o.select_set(False)
 low.select_set(True)
 bm=bmesh.new();bm.from_mesh(low.data)
 bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.001)
 bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.00001)
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
 bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(2),use_dissolve_boundaries=False,verts=list(bm.verts),edges=list(bm.edges),delimit={'NORMAL'})
 bm.to_mesh(low.data);bm.free();low.data.update()
 low.data.calc_loop_triangles();before=len(low.data.loop_triangles)
 if before>target:
  dec=low.modifiers.new('Geometry budget','DECIMATE');dec.ratio=target/before;dec.use_collapse_triangulate=True
  bpy.ops.object.modifier_apply(modifier=dec.name)
 low.data.calc_loop_triangles()
 if len(low.data.loop_triangles)>max(target*1.5,target+20):
  # Some printing meshes contain overlapping/non-manifold internal triangles.
  # Repair only the local device copy before trying the bounded reduction again.
  low.data=original.data.copy()
  repair=low.modifiers.new('Repair print topology','REMESH');repair.mode='VOXEL';repair.voxel_size=.004;repair.use_smooth_shade=True
  bpy.ops.object.modifier_apply(modifier=repair.name)
  low.data.calc_loop_triangles();count=len(low.data.loop_triangles)
  dec=low.modifiers.new('Repaired geometry budget','DECIMATE');dec.ratio=min(1,target/count);dec.use_collapse_triangulate=True
  bpy.ops.object.modifier_apply(modifier=dec.name)
 # Preserve the high-resolution source's smooth lighting at the remaining corners.
 transfer=low.modifiers.new('Source shading','DATA_TRANSFER');transfer.object=original
 transfer.use_loop_data=True;transfer.data_types_loops={'CUSTOM_NORMAL'};transfer.loop_mapping='POLYINTERP_NEAREST'
 bpy.ops.object.modifier_apply(modifier=transfer.name)
 low.data.calc_loop_triangles();me=low.data
 mat=material(original.data.materials[0]);P,N,I,lookup=parts[(bone,mat)]
 pivot=rig.data.bones[bone].head_local
 for tri in me.loop_triangles:
  ids=[]
  for loopid in tri.loops:
   vi=me.loops[loopid].vertex_index;p=C@(me.vertices[vi].co-pivot);n=(C.to_3x3()@me.corner_normals[loopid].vector).normalized()
   key=tuple(round(v,6) for v in (*p,*n))
   if key not in lookup:lookup[key]=len(P);P.append(tuple(p));N.append(tuple(n))
   ids.append(lookup[key])
  if len(set(ids))==3:I.extend(ids)
 part_report.append({'name':original.name,'source_id':sid,'joint':bone,'before':before,'after':len(me.loop_triangles)})
 print('REDUCED',original.name,before,len(me.loop_triangles),flush=True)
 bpy.data.objects.remove(low,do_unlink=True)
# Opaque front-to-back submission reduces work hidden by the face/helmet.
# Depth testing still determines visibility at every animated pose.
def front_depth(item):
 (bone,_),(p,_,_,_)=item
 return sum(v[2] for v in p)/len(p)+(C@rig.data.bones[bone].head_local).z
for (bone,mat),(p,n,indices,_) in sorted(parts.items(),key=front_depth,reverse=True):
 triangles=[indices[i:i+3] for i in range(0,len(indices),3)]
 triangles.sort(key=lambda tri:sum(p[i][2] for i in tri),reverse=True)
 indices=[i for tri in triangles for i in tri]
 meshid=len(scene.meshes);scene.meshes.append(S.Mesh(name=bone+'.'+scene.materials[mat].name,positions=p,normals=n,indices=indices,material_index=mat))
 scene.nodes.append(S.Node(name=scene.meshes[-1].name,parent=bone_ids[bone],mesh_index=meshid))
# The same semantic actions work with any compatible replacement character.
clips=['Idle','Listening','Thinking','Speaking','Wave','Yes','No','Happy','Curious','Sleep','Surprise','Point','Celebrate','Sad']
mapping={'Idle':'Idle','Listening':'Idle','Thinking':'Look around','Speaking':'Nod','Wave':'Wave','Yes':'Nod','No':'Look around','Happy':'Wave','Curious':'Idle','Sleep':'Idle','Surprise':'Idle','Point':'Wave','Celebrate':'Wave','Sad':'Idle'}
for name in clips:
 clip=S.Clip(name=name,duration_ms=4000,loop=True);scene.clips.append(clip)
 values={bn:{0:[],1:[],2:[]} for bn in bone_ids};times=[i/10 for i in range(41)]
 for t in times:
  rig.animation_data.action=bpy.data.actions[mapping[name]]
  frame=1+t*30
  if name=='Point':frame=46
  bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame))
  if name in ('Listening','Curious'):rig.pose.bones['head'].rotation_euler[2]+=math.radians(4 if name=='Listening' else 8)
  if name=='Speaking':rig.pose.bones['head'].rotation_euler[0]*=.35
  if name in ('Sleep','Sad'):rig.pose.bones['head'].rotation_euler[0]=math.radians(5)
  if name=='Surprise':rig.pose.bones['head'].rotation_euler[0]=math.radians(-5)
  if name=='Happy':
   for bn in ('upper_arm.L','forearm.L'):
    for axis in range(3):rig.pose.bones[bn].rotation_euler[axis]*=.4
  bpy.context.view_layer.update()
  world={}
  for b in rig.data.bones:
   delta=rig.pose.bones[b.name].matrix@b.matrix_local.inverted()
   world[b.name]=delta@rest[b.name]
   local=world[b.parent.name].inverted()@world[b.name] if b.parent else world[b.name]
   tr,rot,sc=(C@local@C.inverted()).decompose()
   # Independent eyeballs blink around their own pivots, without scaling the face.
   if b.name.startswith('gaze.'):
    if name=='Sleep':sc.y*=.10
    elif .90<t<1.14:sc.y*=max(.10,abs(t-1.02)/.12)
   values[b.name][0].append(tuple(tr));values[b.name][1].append((rot.x,rot.y,rot.z,rot.w));values[b.name][2].append(tuple(sc))
 for bn,channels in values.items():
  for path,data in channels.items():
   # Constant channels still reset state when switching between gestures.
   constant=all(max(abs(a-b) for a,b in zip(v,data[0]))<1e-6 for v in data)
   clip.channels.append(S.Channel(bone_ids[bn],path,S.ANIM_LINEAR,[0,4] if constant else times,[data[0],data[0]] if constant else data))
out=ROOT/'assets/avatar.a3d';out.write_bytes(S.build_container(scene))
report={'id':'labubu-engineering','source':'Copied Gundam Labubu 3MF, Chocobo; local prototype only','master':str(SRC/'labubu-rigged.blend'),'bytes':out.stat().st_size,'triangles':sum(len(m.indices)//3 for m in scene.meshes),'vertices':sum(len(m.positions) for m in scene.meshes),'meshes':len(scene.meshes),'nodes':len(scene.nodes),'bones':len(bone_ids),'clips':clips,'mouth_animation':False,'parts':part_report,'status':'engineering asset; hardware validation pending'}
(ROOT/'assets/avatar-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
header=['#pragma once','// Generated by prepare_labubu.py; renderer consumes this model-independent contract.','namespace Avatar {','inline constexpr const char* Id="labubu-engineering";','enum Clip { '+', '.join(clips)+', ClipCount };','inline constexpr const char* clips[]={'+','.join(json.dumps(c) for c in clips)+'};']
for key,bn in [('HeadNode','head'),('EarLeft','ear.L'),('EarRight','ear.R'),('EyeLeft','gaze.L'),('EyeRight','gaze.R')]:header.append(f'inline constexpr int {key}={bone_ids[bn]};')
header+=['inline constexpr int MouthNode=-1;',f'inline constexpr int Triangles={report["triangles"]};','}']
(ROOT/'main/avatar_asset.h').write_text('\n'.join(header)+'\n')
(ROOT/'host/avatar_catalog.py').write_text('# Generated avatar action contract\nCLIPS = '+repr(clips)+'\n')
print('EXPORT_READY',json.dumps({k:v for k,v in report.items() if k!='parts'}),flush=True)
