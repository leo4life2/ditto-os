#include "../main/mic_adpcm.h"
#include "../main/power_journal.h"
#include "../main/ble_control.h"
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdio>
struct Flash:PowerJournal::Storage{
 std::vector<uint8_t> b=std::vector<uint8_t>(PowerJournal::Bytes,255);bool torn=false;unsigned erases=0;
 bool read(size_t o,void* p,size_t n)override{assert(o+n<=b.size());memcpy(p,b.data()+o,n);return true;}
 bool write(size_t o,const void* p,size_t n)override{assert(o+n<=b.size());bool fail=torn;torn=false;if(fail)n/=2;for(size_t i=0;i<n;i++){uint8_t v=((uint8_t*)p)[i];assert((b[o+i]&v)==v);b[o+i]=v;}return !fail;}
 bool erase(size_t o,size_t n)override{assert(o>=PowerJournal::Sector && o%4096==0 && n==4096 && o+n<=b.size());std::fill(b.begin()+o,b.begin()+o+n,255);erases++;return true;}
};
int main(){
 using namespace PowerJournal;std::vector<uint32_t> index(Capacity);Flash flash;Journal j(flash,index.data());assert(j.open());
 Record r{};r.d.percent=100;assert(j.append(r));flash.torn=true;assert(!j.append(r));
 Journal recovery(flash,index.data());assert(recovery.open() && recovery.latest==1 && recovery.bad==1);assert(recovery.append(r)&&r.d.sequence==2);
 for(unsigned i=0;i<Capacity+100;i++)assert(recovery.append(r));assert(recovery.count<=Capacity&&recovery.count>Capacity-32);
 uint32_t cursor=0,count=0;while(recovery.after(cursor,r)){assert(r.d.sequence>cursor);cursor=r.d.sequence;count++;}assert(count==recovery.count && cursor==recovery.latest);
 auto latest=recovery.latest;Journal reboot(flash,index.data());assert(reboot.open()&&reboot.latest==latest&&reboot.count==count);
 Flash unknown;unknown.b[98765]=0;Journal refused(unknown,index.data());assert(!refused.open() && unknown.erases==0 && unknown.b[0]==255);
 uint8_t packet[MicADPCM::Bytes];int16_t input[320],output[320];int step=0;double noise=0,signal=0;uint32_t seq;
 for(unsigned block=0;block<100;block++){
  for(int i=0;i<320;i++)input[i]=int16_t(12000*std::sin((block*320+i)*2*3.141592653589793*440/16000));
  MicADPCM::encode(input,packet,block,step);assert(MicADPCM::decode(packet,sizeof(packet),output,seq)&&seq==block);
  if(block>2)for(int i=0;i<320;i++){double d=input[i]-output[i];noise+=d*d;signal+=double(input[i])*input[i];}
 }
 double snr=10*log10(signal/noise);assert(snr>25);packet[10]=89;assert(!MicADPCM::decode(packet,sizeof(packet),output,seq));
 step=0;MicADPCM::encode(input,packet,100,step,true);assert(MicADPCM::decode(packet,sizeof(packet),output,seq));for(auto v:output)assert(v==0);
 uint8_t control[44]={2,3,0,0};memcpy(control+8,"12345678-1234-1234-1234-123456789ABC",36);BleControl::Command c;assert(BleControl::decode(control,44,c)&&c.audio&&c.dictating);control[1]=4;assert(!BleControl::decode(control,44,c));
 printf("PASS: power journal ownership, torn writes, ring wrap, reboot, ordered export; independent ADPCM blocks %.1f dB SNR; mute and BLE audio lease flags\n",snr);
}
