from pathlib import Path
import sys,subprocess
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'));from storage_hygiene import scratch_directory
with scratch_directory('wireless-checks',reserve_mib=16) as p:
 for source in ['wireless_power_check.cpp','ble_control_check.cpp','wireless_audio_buffer_check.mm']:
  exe=p/Path(source).stem
  framework=['-framework','AudioToolbox','-framework','CoreAudio','-framework','CoreFoundation'] if source.endswith('.mm') else []
  subprocess.run(['clang++','-std=c++17','-I'+str(ROOT/'main'),'-fsanitize=address,undefined','-g',str(ROOT/'tests'/source),*framework,'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 subprocess.run([sys.executable,str(ROOT/'tests/battery_archive_check.py')],check=True)
