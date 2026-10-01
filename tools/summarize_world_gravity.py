"""Summarize the retained current-device evidence without resetting counters."""
import hashlib,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results/ditto-world-gravity-0.8.1'
r=json.loads((OUT/'world-gravity-hardware.json').read_text())
summary={'version':'0.8.1','app_sha256':hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest(),'avatar_sha256':hashlib.sha256((ROOT/'assets/avatar.a3d').read_bytes()).hexdigest()}
summary['gravity_directions']={k:{'axial_scale':v['axial_scale'],'fps':v['final']['fps']} for k,v in r['directions'].items()}
summary['max_rotation_gravity_error_g']=max(math.sqrt(sum((x+y)**2 for x,y in zip(s['motion_gravity_g'],s['motion_specific_g']))) for s in r['rotation'])
summary['sleep_motion_wake_passed']='sleep_motion_wake' in r
summary['real_imu_restored']=all(s['imu_available'] and not s['motion_simulated'] and s['imu_sample_age_ms']<150 for s in r['live_samples'])
summary['max_material_volume_error']=max(abs(s['material_volume']-1) for v in r['directions'].values() for s in v['samples'])
summary['max_physics_ms']=max(s['physics_ms'] for v in r['directions'].values() for s in v['samples'])
for name in ('shake-hardware-check','sharp-tap-check','material-hardware-check'):
 p=OUT/(name+'.json')
 if p.exists():
  report=json.loads(p.read_text());summary[name]={'final_version':report['final']['version'],'display_errors':report['final']['display_errors'],'touch_errors':report['final']['touch_errors'],'touch_recoveries':report['final']['touch_recoveries']}
  if name=='shake-hardware-check':summary[name]['runs']=[{k:v for k,v in run.items() if k in ('axis','hz','fps','minimum','maximum','injection_hz','after_wobble_peak')} for run in report['runs']]
p=OUT/'soak.json'
if p.exists():
 report=json.loads(p.read_text());a,b=report['baseline'],report['measurement_end']
 summary['combined_load']={'fps':(b['frames']-a['frames'])/(b['uptime_s']-a['uptime_s']),'percent_within25ms':100*(b['frames_within25']-a['frames_within25'])/(b['frames']-a['frames']),'max_rolling_p95_ms':max(s['frame_p95_ms'] for s in report['samples']),'duration_s':report['workload_duration_s'],'errors':report['errors'],'unexpected_resets':report['unexpected_resets'],'max_command_latency_ms':report['command_latency_max_ms'],'audio':report['audio']}
(OUT/'validation-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
