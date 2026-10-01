"""Run production ESP packet packing through the actual Swift BLE/gesture decoder."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from storage_hygiene import scratch_directory
s=(ROOT/'main/board.cpp').read_text()
body=s[s.index('size_t board_remote_packet('):s.index('static BlobMotion::Filter')]
prefix=r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "remote_input.h"
struct V{float x=0,y=0,z=0;};
struct {struct {bool down=false;int x=100,y=100;}touch;
 bool sleeping=false,usb_connected=true;int battery_percent=72;int64_t motion_sim_until_us=0;
 struct {bool valid=true;int64_t time_us=1000000;V linear{-1.25f,0.5f,0.125f};}motion;
}state;
struct Diagnostics{RemoteInput::History remote;}diagnostics,*touch_diagnostics=&diagnostics;
constexpr int state_mutex=0,portMAX_DELAY=0;
void xSemaphoreTake(int,int){} void xSemaphoreGive(int){}
int64_t now=1000000;int64_t esp_timer_get_time(){return now;}
'''
suffix=r'''
int main(int argc,char** argv){
 assert(argc==2);FILE* f=fopen(argv[1],"wb");assert(f);
 uint8_t data[92];assert(board_remote_packet(data,91)==0);
 auto emit=[&](){uint8_t n=board_remote_packet(data,sizeof(data));assert(n>=28);fwrite(&n,1,1,f);fwrite(data,1,n,f);};
 emit();
 auto& h=diagnostics.remote;
 h.push(1010000,100,100,true,false,false);h.push(1025000,100,100,false,false,false);
 h.push(1050000,100,100,true,false,false);h.push(1065000,100,100,false,false,false);
 now=1080000;state.motion.time_us=now;emit();emit();
 h.push(1100000,100,100,true,false,true);now=1110000;state.touch.down=true;emit();
 now=1380000;state.motion.time_us=now;emit(); // wake-only hold cannot dictate
 h.push(1390000,100,100,false,false,false);state.touch.down=false;now=1400000;emit();
 now=2000000;state.battery_percent=-1;emit();fclose(f);
}
'''
swift=r'''
import Foundation
gestureSelfTest()
let data=[UInt8](try Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1])))
var offset=0,packets=[[String:Any]](),machine=Gestures(),actions=[[Gesture]]()
while offset<data.count {
 let n=Int(data[offset]);offset+=1;let raw=Data(data[offset..<offset+n]);offset+=n
 guard let p=decodeRemotePacket(raw,after:machine.sequence) else{fatalError("Valid firmware frame rejected")}
 packets.append(p);actions.append(machine.feed(p))
 // Every truncated frame and an incorrect version are rejected.
 for cut in 0..<n{precondition(decodeRemotePacket(raw.prefix(cut),after:0)==nil)}
 var bad=raw;bad[15]=2;precondition(decodeRemotePacket(bad,after:0)==nil)
}
precondition(actions.count==7 && actions[1] == [.enter])
for i in [0,2,3,4,5,6]{precondition(actions[i].isEmpty)}
precondition(packets[0]["battery"] as? Int == 72 && packets[6]["battery"] as? Int == -1)
precondition(packets[0]["linear"] as? [Double] == [-1.25,0.5,0.125])
let edges=packets[1]["edges"] as! [[Double]]
precondition(edges[0]==[1,1010000,100,100,1,0,0])
precondition(edges[3]==[4,1065000,100,100,0,0,0])
print("PASS: production firmware binary -> Swift BLE decoder -> exactly one Return for buffered 15ms taps; wake-only hold suppressed; signed acceleration, battery, edge timing, truncation/version rejection")
'''
with scratch_directory('remote-wire',reserve_mib=48) as folder:
 p=Path(folder);(p/'pack.cpp').write_text(prefix+body+suffix);(p/'main.swift').write_text(swift)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I',str(ROOT/'main'),str(p/'pack.cpp'),'-o',str(p/'pack')],check=True)
 subprocess.run([str(p/'pack'),str(p/'wire.bin')],check=True)
 subprocess.run(['swiftc','-O','-framework','Cocoa','-framework','CoreBluetooth',str(ROOT/'host/mac-remote/Bluetooth.swift'),str(ROOT/'host/mac-remote/Gestures.swift'),str(p/'main.swift'),'-o',str(p/'decode')],check=True)
 subprocess.run([str(p/'decode'),str(p/'wire.bin')],check=True)
