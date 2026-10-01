"""Compare glossy native frames across workers and tile boundaries."""
import subprocess,tempfile,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0,str(ROOT/'tools'))
from validation_context import evidence_directory
out=evidence_directory(ROOT)
with tempfile.TemporaryDirectory(prefix='scout-liquid-') as td:
 td=Path(td);runs=[]
 for name,workers,tile in [('serial',1,18),('parallel',2,18),('different-tiles',2,14)]:
  exe=td/name;raw=td/(name+'.rgb565');body=td/(name+'.body')
  subprocess.run(['c++','-std=c++17','-O2',f'-DSCOUT_RENDER_WORKERS={workers}',f'-DSCOUT_RENDER_TILE_ROWS={tile}','-I',str(ROOT/'main'),'-I',str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_eye_interactions.cpp'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),str(body)],check=True)
  runs.append({'name':name,'pixels':hashlib.sha256(raw.read_bytes()).hexdigest(),'body':hashlib.sha256(body.read_bytes()).hexdigest()})
 assert runs[0]['pixels']==runs[1]['pixels'],'worker count changes rendered pixels'
 # Fixed-point scanline rounding may change a quantization boundary with a
 # different tile start. Count and bound any difference rather than hide it.
 from array import array
 a=array('H');a.frombytes((td/'parallel.rgb565').read_bytes());b=array('H');b.frombytes((td/'different-tiles.rgb565').read_bytes())
 changed=0;max_step=0;edge_changes=0;bad=[]
 for pos,(x,y) in enumerate(zip(a,b)):
  if x!=y:
   changed+=1
   delta=max(abs((x>>11)-(y>>11)),abs(((x>>5)&63)-((y>>5)&63)),abs((x&31)-(y&31)));max_step=max(max_step,delta)
   if delta>4:
    # A fixed-point coverage/depth tie can choose either side of a 1-pixel
    # silhouette/ink boundary. Interior lighting must remain within 4 RGB565 steps across a shared triangle edge.
    dark=lambda c: max(c>>11,(c>>5)&63,c&31)<8
    frame_pos=pos%(410*502);xx=frame_pos%410;yy=frame_pos//410
    neighbor=[a[pos+dy*410+dx] for dy in [-1,0,1] for dx in [-1,0,1] if 0<=xx+dx<410 and 0<=yy+dy<502]
    if x==0 or y==0 or (dark(x)!=dark(y) and any(dark(q) for q in neighbor) and any(not dark(q) for q in neighbor)):edge_changes+=1
    else:bad.append([pos,x,y,delta])
 assert changed/len(a)<.00001 and not bad,(changed,len(a),max_step,bad[:12])
 assert len({r['body'] for r in runs})==1,'worker/tile config changes deformation'
 report={'frames_per_run':480,'runs':runs,'parallel_pixels_identical':True,'changed_tile_pixels':changed,'total_pixels':len(a),'max_rgb565_channel_step':max_step,'single_pixel_coverage_differences':edge_changes}
 (out/'liquid-render-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
