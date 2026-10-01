"""Portable, hardware-free smoke checks for the public source tree."""
import os
from pathlib import Path
import subprocess
import sys
from storage_hygiene import scratch_directory

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ['usb_pcm', 'voice_capture', 'wireless_power', 'ble_control',
          'activity', 'brightness_transition', 'touch_history', 'motion_health',
          'ditto_rig', 'ditto_autoplay']
HOST = ['voice_interrupt', 'voice_session', 'voice_action', 'usb_ownership',
        'package_release']

def main():
    compiler = os.environ.get('CXX', 'c++')
    with scratch_directory('public-checks', reserve_mib=48) as temp:
        for name in NATIVE:
            binary = temp / name
            subprocess.run([compiler, '-std=c++17', '-O2', '-pthread',
                            '-I', str(ROOT / 'main'),
                            '-I', str(ROOT / 'components/a3d/src'),
                            str(ROOT / 'tests' / (name + '_check.cpp')),
                            '-o', str(binary)], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)
        for name in HOST:
            subprocess.run([sys.executable, str(ROOT / 'tests' / (name + '_check.py'))],
                           check=True, cwd=ROOT)
    print(f'PASS: {len(NATIVE)} native and {len(HOST)} host suites; no hardware used.')

if __name__ == '__main__':
    main()
