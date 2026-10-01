"""Bound fixed-point depth differences against the original float rasterizer."""
import array,json,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from validation_context import evidence_directory
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='scout-depth-') as folder:
    folder=Path(folder);images=[]
    for name,flags in [('float',['-DA3D_FLOAT_DEPTH_REFERENCE']),('fixed',[])]:
        binary=folder/name;out=folder/(name+'.rgb565')
        subprocess.run(['c++','-std=c++17','-O2',*flags,'-I',str(root/'components/a3d/src'),str(root/'tests/render_host.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary),str(root/'assets/avatar.a3d'),str(out),'-1','2'],check=True)
        pixels=array.array('H');pixels.frombytes(out.read_bytes());images.append(pixels)
    assert len(images[0])==len(images[1])
    frame_size=410*502;differences=[]
    for start in range(0,len(images[0]),frame_size):
        differences.append(sum(a!=b for a,b in zip(images[0][start:start+frame_size],images[1][start:start+frame_size])))
    report={'frames':len(differences),'pixels':len(images[0]),'changed_pixels':sum(differences),'max_changed_pixels_per_frame':max(differences),'fraction':sum(differences)/len(images[0])}
    print(json.dumps(report,indent=2))
    (evidence_directory(root)/'depth-check.json').write_text(json.dumps(report,indent=2)+'\n')
    # At intersecting print surfaces, sub-depth-unit rounding can select the
    # neighboring face. It must remain sparse, never a missing silhouette.
    assert max(differences)<frame_size*.001,report
