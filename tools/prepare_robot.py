#!/usr/bin/env python3
"""Convert the CC0 robot into compact, smooth-shaded live geometry."""
from pathlib import Path
import json, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor_a3d'))
import a3d_gltf as G
import a3d_scene as S
from a3d_decimate import decimate

source = ROOT / 'assets/source/RobotExpressive.glb'
scene = G.convert(source, verbose=False)
original=G.Gltf(source)
morphs=[]
report = []
for mi, mesh in enumerate(scene.meshes):
    skins = [s for s in scene.skins if s.mesh_index == mi]
    # Preserve joint attachment boundaries, but weld duplicate hard-normal vertices.
    groups, remap, used = {}, [], []
    for vi, p in enumerate(mesh.positions):
        bindings = tuple((s.bindings[vi].bone0,s.bindings[vi].bone1,s.bindings[vi].weight0) for s in skins)
        key = (tuple(round(v,6) for v in p), bindings)
        if key not in groups:
            groups[key] = len(used); used.append(vi)
        remap.append(groups[key])
    before = len(mesh.positions)
    mesh.positions = [mesh.positions[i] for i in used]
    mesh.indices = [remap[i] for i in mesh.indices]
    mesh.normals = S.generate_normals(mesh.positions, mesh.indices)
    for skin in skins:
        skin.bindings = [skin.bindings[i] for i in used]
    target=max(12,int(len(mesh.indices)//3*(1.0 if mesh.name=='Head.2' else .25)))
    mesh.positions,mesh.normals,_,mesh.indices,extras,stats=decimate(mesh.positions,mesh.normals,None,mesh.indices,target,extra_per_vertex=[s.bindings for s in skins]+[used])
    for skin,bindings in zip(skins,extras[:-1]):skin.bindings=bindings
    mesh.normals=S.generate_normals(mesh.positions,mesh.indices)
    if mesh.name.startswith('Head.'):
        primitive=original.js['meshes'][2]['primitives'][int(mesh.name.split('.')[-1])]
        deltas=[[original.accessor(t['POSITION'])[i] for i in extras[-1]] for t in primitive['targets']]
        morphs.append((mi,mesh.positions,deltas))
    report.append({'mesh':mesh.name,'vertices_before':before,'vertices_after':len(mesh.positions),'triangles':len(mesh.indices)//3,'reduction':stats})
for material in scene.materials:
    material.color=tuple(12.92*x if x<=.0031308 else 1.055*x**(1/2.4)-.055 for x in material.color)
    if material.name=='Main': material.color=(1.,.78,.18)
    elif material.name=='Grey': material.color=(.95,.91,.76)
    elif material.name=='Black': material.color=(.10,.16,.18)
    material.ambient = .72
    material.diffuse = .40
    material.specular = .16
    material.specular_exponent = 12
data = S.build_container(scene)
(ROOT/'assets/robot.a3d').write_bytes(data)
header=['#pragma once','struct MorphMesh {int mesh,count;const float* base;const float* delta[3];};']
for mi,base,deltas in morphs:
    for name,values in [('base',base)]+[(str(i),d) for i,d in enumerate(deltas)]:
        header.append('inline const float morph_%d_%s[]={%s};'%(mi,name,','.join(format(v,'.9g')+'f' if '.' in format(v,'.9g') or 'e' in format(v,'.9g') else format(v,'.9g')+'.0f' for p in values for v in p)))
header.append('inline const MorphMesh morph_meshes[]={'+','.join('{%d,%d,morph_%d_base,{morph_%d_0,morph_%d_1,morph_%d_2}}'%(mi,len(base),mi,mi,mi,mi) for mi,base,deltas in morphs)+'};')
(ROOT/'main/robot_morph.h').write_text('\n'.join(header)+'\n')
(ROOT/'assets/model-report.json').write_text(json.dumps({'source':str(source),'bytes':len(data),'meshes':report,'morph_animation':'Three original head morph targets exported separately; runtime blends expressions','clips':[c.name for c in scene.clips]},indent=2)+'\n')
print('robot:',len(data),'bytes;',sum(x['vertices_after'] for x in report),'vertices;',sum(x['triangles'] for x in report),'triangles')
