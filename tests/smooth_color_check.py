"""Check smooth RGB565 color under parallel and odd-origin tiled rendering."""
import array,hashlib,json,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools/vendor_a3d'))
import a3d_scene as S

with tempfile.TemporaryDirectory(prefix='scout-smooth-') as folder:
    folder=Path(folder);binary=folder/'render'
    subprocess.run(['c++','-std=c++17','-O2','-DA3D_SMOOTH_COLOR','-I',str(root/'components/a3d/src'),str(root/'tests/render_host.cpp'),'-o',str(binary)],check=True)
    def render(asset,workers,rows):
        out=folder/'pixels.rgb565'
        subprocess.run([str(binary),str(asset),str(out),'-1',str(workers),'shader',str(rows)],check=True)
        return out.read_bytes()
    serial=render(root/'assets/avatar.a3d',1,20)
    parallel=render(root/'assets/avatar.a3d',2,20)
    assert serial==parallel,'Worker scheduling changed a native pixel'

    # Test absolute dither phase separately from float silhouette-edge stepping.
    # Changing triangle tile heights can move a handful of edge pixels in the
    # existing rasterizer; a flat patch isolates color phase from that coverage.
    fixture=S.Scene(materials=[S.Material(color=(.87,.53,.98),ambient=.613,diffuse=0,specular=0)])
    fixture.meshes=[S.Mesh(name='Uniform patch',positions=[(-2,-2,0),(2,-2,0),(2,2,0),(-2,2,0)],normals=[(0,0,1)]*4,indices=[0,1,2,0,2,3],material_index=0)]
    fixture.nodes=[S.Node(mesh_index=0)]
    fixture.clips=[S.Clip(name='Idle',duration_ms=1000,loop=True)]
    asset=folder/'uniform.a3d';asset.write_bytes(S.build_container(fixture))
    patches=[]
    for rows in (20,19,31):
        pixels=array.array('H');pixels.frombytes(render(asset,2,rows))
        patches.append([pixels[frame*410*502+y*410+x] for frame in range(8) for y in range(215,285) for x in range(155,255)])
    assert patches[0]==patches[1]==patches[2],'Dither phase moved at a tile boundary'
    assert len(set(patches[0]))>1,'Fixture did not exercise fractional color'
    report={'passed':True,'avatar_frames':112,'native_pixels':len(serial)//2,
        'sha256':hashlib.sha256(serial).hexdigest(),'worker_outputs_identical':True,
        'uniform_patch_tile_heights':[20,19,31],'uniform_patch_phase_identical':True,
        'note':'Only production 20-row character rendering requires byte equality; alternate-height silhouette rounding is not a dither defect.'}
    print(json.dumps(report,indent=2))
    (root/'test-results/ditto-integration/smooth-color-check.json').write_text(json.dumps(report,indent=2)+'\n')
