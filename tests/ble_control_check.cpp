#include "ble_control.h"
#include <cassert>
#include <cstdio>
int main(){
 uint8_t bytes[44]={1,1,2,0,0x78,0x56,0x34,0x12};memcpy(bytes+8,"12345678-1234-1234-1234-123456789ABC",36);BleControl::Command c;
 assert(BleControl::decode(bytes,44,c)&&c.dictating&&c.sequence==0x12345678&&!strcmp(c.kind,"enter"));
 for(size_t n=0;n<44;n++)assert(!BleControl::decode(bytes,n,c));
 for(int i=0;i<44;i++){auto old=bytes[i];bytes[i]=255;if(i<4||i>=8)assert(!BleControl::decode(bytes,44,c));bytes[i]=old;}
 for(int k=1;k<=3;k++){bytes[2]=k;assert(BleControl::decode(bytes,44,c));}
 bytes[2]=0;assert(!BleControl::decode(bytes,44,c));memset(bytes+4,0,4);assert(BleControl::decode(bytes,44,c)&&!c.kind);bytes[2]=1;assert(!BleControl::decode(bytes,44,c));
 puts("PASS: shared Swift golden frame, actions, UUID validation, all short lengths and invalid header fields");
}
