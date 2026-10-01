"""Preserve measurements even when a completed soak fails a strict gate."""
import hashlib,json
from pathlib import Path
from validation_context import evidence_directory
ROOT=Path(__file__).resolve().parents[1];OUT=evidence_directory(ROOT)
r=json.loads((OUT/'soak.json').read_text());identity=json.loads((OUT/'build-identity.json').read_text())
assert hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest()==identity['firmware_sha256']
assert hashlib.sha256((ROOT/'assets/avatar.a3d').read_bytes()).hexdigest()==identity['asset_sha256']
assert r['completed_requested_duration'], 'Do not summarize an interrupted run as completed'
b=r['baseline'];e=r['measurement_end'];n=e['frames']-b['frames'];duration=e['uptime_s']-b['uptime_s']
m={**identity,'duration_s':duration,'frames':n,'average_fps':n/duration,'within25_percent':100*(e['frames_within25']-b['frames_within25'])/n,
   'over33ms':e['missed_frames']-b['missed_frames'],'errors':r['errors'],'unexpected_resets':r['unexpected_resets'],
   'physics_max_sample_ms':max(s['physics_ms'] for s in r['samples']),'audio':r['audio'],
   'touch_errors':e['touch_errors'],'touch_retries':e.get('touch_retries'),'touch_recoveries':e.get('touch_recoveries'),'touch_available_at_end':r['final']['touch_available'],
   'two_hour_test':False,'physical_gestures_verified':False}
m['performance_passed']=m['average_fps']>=29.7 and m['within25_percent']>=95 and m['over33ms']/n<=.01
m['zero_error_gate_passed']=not r['errors'] and not r['unexpected_resets'] and all(e[k]==0 for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','touch_errors'))
if 'imu_samples' in e:
    m['imu_samples']=e['imu_samples']-b['imu_samples'];m['imu_rejected_samples']=e['imu_errors']-b['imu_errors'];m['imu_retries']=e['imu_retries']-b['imu_retries']
    m['imu_available_at_end']=r['final']['imu_available']
    m['zero_error_gate_passed']=m['zero_error_gate_passed'] and m['imu_rejected_samples']==0 and m['imu_available_at_end']
m['all_gates_passed']=m['performance_passed'] and m['zero_error_gate_passed']
(OUT/'final-metrics.json').write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps(m,indent=2))
