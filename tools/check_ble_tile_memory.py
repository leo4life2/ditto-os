"""Measure native-pixel differences from radio-budget tile size, with sanitizers."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
import numpy as np
from PIL import Image
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);results=[]
with scratch_directory('ble-tile-equivalence',reserve_mib=128) as tmp:
 exe=tmp/'render';raw=tmp/'frames.rgb565'
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(ROOT/'main'),'-I'+str(ROOT/'components/a3d/src'),str(ROOT/'tools/render_listening.cpp'),'-o',str(exe)],check=True)
 env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
 # Listening includes idle, touch, stretch, release, glow and moving facial ink.
 for clip in [1]:
  frames=[]
  for tile in (18,12):
   subprocess.run([str(exe),str(ROOT/'assets/avatar.a3d'),str(raw),'2','1',str(clip),str(tile)],env=env,check=True,capture_output=True)
   frames.append(np.fromfile(raw,dtype='<u2').reshape(210,502,410))
  a,b=frames;different=a!=b;delta=np.zeros(a.shape,dtype=np.int16)
  channel=[]
  for shift,mask in [(11,31),(5,63),(0,31)]:
   d=np.abs(((a>>shift)&mask).astype(np.int16)-((b>>shift)&mask).astype(np.int16));channel.append(int(d.max()));delta=np.maximum(delta,d)
  metrics={'clip':clip,'frames':210,'changed_pixels':int(different.sum()),'total_pixels':a.size,'max_channel_steps':channel,'over_one_step_pixels':int((delta>1).sum())};results.append(metrics);print(metrics,flush=True)
  i=int(different.sum(axis=(1,2)).argmax())
  for name,arr in [('18',a),('12',b)]:
   p=arr[i];rgb=np.stack([((p>>11)&31)*255//31,((p>>5)&63)*255//63,(p&31)*255//31],axis=-1).astype('uint8');Image.fromarray(rgb).save(out.parent/f'tile-{name}-worst.png')
 out.write_text(json.dumps({'ASan_UBSan':True,'clips':results},indent=2)+'\n')
