"""Short actual-device renderer profile; stop the persistent USB bridge first."""
import argparse,json,sys,time,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'host'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--seconds',type=float,default=15);args=p.parse_args()
with Device() as d:
 time.sleep(3);d.sync_clock();d.call('display.wake');d.call('app.home');d.call('avatar.emote',{'name':'Idle'})
 time.sleep(2);a=d.call('system.status');samples=[];start=time.monotonic()
 while time.monotonic()-start<args.seconds:
  samples.append(d.call('system.status'));time.sleep(.5)
 b=d.call('system.status');stages=[]
 for line in d.logs:
  if line.startswith('PROFILE '):stages.append([float(v) for v in line.split()[1:]])
 result={'average_render_fps':(b['frames']-a['frames'])/(b['uptime_s']-a['uptime_s']),
  'average_presented_fps':(b['presented_frames']-a['presented_frames'])/(b['uptime_s']-a['uptime_s']),
  'stage_names':['lighting','bounds','gradients','rows','pixels'],
  'mean_stage_ms':[statistics.mean(row[i+1] for row in stages) for i in range(5)] if stages else [],
  'samples':samples,'baseline':a,'end':b,'device_logs':list(d.logs)}
 valid=b['frames']>a['frames'] and b['presented_frames']>a['presented_frames'] and all(y['uptime_s']>x['uptime_s'] for x,y in zip([a]+samples,samples+[b]))
 result['valid_continuous_run']=valid
 args.output.write_text(json.dumps(result,indent=2)+'\n')
 assert valid,'Renderer stalled or reset; timing is invalid (see saved evidence)' 
 print(json.dumps({k:v for k,v in result.items() if k not in ['samples','baseline','end','device_logs']},indent=2))
 print({k:statistics.mean(s[k] for s in samples) for k in ['anim_ms','bin_ms','draw_ms','copy_ms','dma_wait_ms','raster0_ms','raster1_ms','frame_ms']})
