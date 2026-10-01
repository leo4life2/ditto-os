"""Verify fresh real IMU output and recovery after stopping the actual sensor."""
import json, os, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={}
with Device() as d:
 time.sleep(2);d.sync_clock();d.call('app.home');d.call('debug.motion',{'enabled':False})
 def read_live(seconds):
  rows=[];end=time.monotonic()+seconds
  while time.monotonic()<end:
   rows.append(d.call('system.status'));time.sleep(.025)
  assert all(r['imu_available'] and not r['motion_simulated'] and r['imu_sample_age_ms']<150 for r in rows)
  assert len({r['imu_timestamp'] for r in rows})>len(rows)*.8
  assert len({tuple(r['motion_specific_g']) for r in rows})>5
  assert len({tuple(r['motion_gyro_rads']) for r in rows})>5
  return rows
 try:
  report['before']=read_live(1.5);base=report['before'][-1]
  assert base['version']==os.environ.get('SCOUT_EXPECTED_VERSION','0.8.1')
  # Disable both real sensors. The normal freshness watchdog must recover.
  d.call('debug.motion.stall');rows=[];start=time.monotonic()
  while time.monotonic()-start<4:
   s=d.call('system.status');rows.append(s)
   if s['imu_recoveries']>base['imu_recoveries'] and s['imu_available'] and s['imu_timestamp']>5:break
   time.sleep(.02)
  report['fault']=rows;report['recovery_seconds']=time.monotonic()-start
  assert any(not r['imu_available'] and r['imu_sample_age_ms']>150 for r in rows),rows
  assert s['imu_recoveries']==base['imu_recoveries']+1 and s['imu_stalls']==base['imu_stalls']+1,s
  assert s['imu_fault_ctrl7']==0 and s['imu_configured'] and s['imu_available'],s
  assert s['imu_stale_reads']>base['imu_stale_reads'],s
  assert s['frames']>base['frames'] and s['display_errors']==base['display_errors'],s
  report['after']=read_live(2)
  assert report['after'][-1]['imu_recoveries']==base['imu_recoveries']+1
  print('Actual sensor stop, stale-sample detection, automatic recovery and fresh six-axis output passed in %.3f s'%report['recovery_seconds'])
 finally:
  d.call('debug.motion',{'enabled':False});d.call('app.home');d.call('display.wake')
  report['final']=d.call('system.status');report['logs']=list(d.logs)
  (OUT/'motion-recovery-check.json').write_text(json.dumps(report,indent=2)+'\n')
