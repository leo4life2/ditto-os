"""Check PMIC status decoding against the documented independent status fields."""
from pathlib import Path
import os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
source=r'''
#include "battery_status.h"
#include <cassert>
#include <cstdio>
int main(){
 using M=Battery::Mode;
 assert(Battery::decode(false,0xff,0xff).mode==M::Unknown);
 // Every charging stage reports active charge, including a momentarily zero
 // current direction; a discharging battery on weak external power does not.
 for(int phase=0;phase<4;phase++)for(int reserved:{0,128}){
  assert(Battery::decode(true,0x28,0x20|phase|reserved).mode==M::Charging);
  assert(Battery::decode(true,0x28,phase|reserved).mode==M::Charging);
  assert(Battery::decode(true,0x28,0x40|phase|reserved).mode==M::Paused);
 }
 assert(Battery::decode(true,0x28,0x14).mode==M::Full);
 assert(Battery::decode(true,0x28,0x15).mode==M::Paused);
 assert(Battery::decode(true,0x28,0x35).mode==M::Paused);
 assert(Battery::decode(true,0x28,0x16).mode==M::Unknown);
 assert(Battery::decode(true,0x28,0x72).mode==M::Unknown);
 assert(Battery::decode(true,0x28,0x54).mode==M::Paused);
 assert(Battery::decode(true,0x08,0x55).mode==M::Discharging);
 assert(Battery::decode(true,0x20,0x15).mode==M::Absent);
 for(int a=0;a<256;a++)for(int b=0;b<256;b++){
  auto s=Battery::decode(true,a,b);
  assert(s.external==bool(a&0x20));assert(s.present==bool(a&8));
  assert(s.phase==(b&7));assert(s.direction==((b>>5)&3));
  assert(Battery::decode(true,a,b^0x80).mode==s.mode);
  if(s.mode==M::Charging||s.mode==M::Full||s.mode==M::Paused)assert(s.external&&s.present);
  if(!s.present)assert(s.mode==M::Absent);
  else if(!s.external)assert(s.mode==M::Discharging);
 }
 puts("65,536 register combinations: presence, external power, phase and direction independent; no false charge from USB or reserved bit.");
}
'''.replace('#include <cstdio>','#include <cstdio>\n#include <initializer_list>')
with scratch_directory('battery-status',reserve_mib=16) as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I',str(ROOT/'main'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1','ASAN_OPTIONS':'halt_on_error=1'})
