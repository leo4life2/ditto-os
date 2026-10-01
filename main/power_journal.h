#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

namespace PowerJournal {
constexpr uint32_t Magic=0x50545444,HeaderMagic=0x48545444,Version=1;
constexpr size_t Sector=4096,Bytes=1024*1024,RecordBytes=256;
constexpr size_t Capacity=(Bytes-Sector)/RecordBytes;
enum Flag:uint32_t {External=1,Present=2,ValidBattery=4,Sleeping=8,Parked=16,
  USB=32,BLE=64,Charging=128,Start=256,ClockValid=512,VoltageValid=1024,SourceChange=2048};
struct Data {
 uint32_t magic=Magic,version=Version,sequence=0,boot=0;
 int64_t epoch=0;uint64_t uptime_ms=0;
 uint32_t flags=0;int16_t percent=-1;uint16_t millivolts=0;
 uint32_t span_ms=0,awake_ms=0,sleep_ms=0,battery_ms=0,unknown_power_ms=0;
 uint32_t usb_ms=0,ble_ms=0,ble_available_ms=0,wifi_connected_ms=0;
 uint32_t mic_awake_ms=0,audio_ms=0,dictating_ms=0,brightness_pct_ms=0,fast_cpu_ms=0;
 uint32_t cpu_elapsed_ms=0,idle0_us=0,idle1_us=0,render_us=0,services_us=0,audio_us=0,radio_us=0;
 uint32_t frames=0,physics_steps=0,contacts=0,min_internal=0,reset_reason=0;
 uint32_t dropped_records=0,display_errors=0,audio_errors=0;
 char firmware[16]={};
};
struct Record {Data d;uint8_t reserved[RecordBytes-sizeof(Data)-sizeof(uint32_t)]={};uint32_t crc=0;};
static_assert(sizeof(Record)==RecordBytes);
struct Header {uint32_t magic=HeaderMagic,version=Version,bytes=Bytes,record_bytes=RecordBytes;uint32_t crc=0;};
inline uint32_t crc(const void* bytes,size_t n){uint32_t c=~0u;auto* p=(const uint8_t*)bytes;while(n--){c^=*p++;for(int k=0;k<8;k++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
inline bool valid(const Record& r){return r.d.magic==Magic && r.d.version==Version && r.d.sequence && r.crc==crc(&r,sizeof(r)-4);}
inline bool blank(const void* p,size_t n){auto* b=(const uint8_t*)p;while(n--)if(*b++!=255)return false;return true;}
struct Storage {virtual bool read(size_t offset,void* out,size_t n)=0;virtual bool write(size_t offset,const void* p,size_t n)=0;virtual bool erase(size_t offset,size_t n)=0;virtual ~Storage()=default;};
// The first MiB of the otherwise unused storage partition is claimed only if
// completely erased. Once its checked header exists, torn records are ignored.
// Erase only our own oldest sector, never firmware, NVS, or unknown contents.
struct Journal {
 Storage& io;uint32_t* index;uint32_t latest=0,count=0,bad=0;size_t next=0;bool ready=false;
 explicit Journal(Storage& s,uint32_t* i):io(s),index(i){}
 bool open(){
  Header h;Record r;
  if(!io.read(0,&h,sizeof(h)))return false;
  if(blank(&h,sizeof(h))){
   for(size_t p=0;p<Bytes;p+=sizeof(r))if(!io.read(p,&r,sizeof(r))||!blank(&r,sizeof(r)))return false;
   h=Header{};h.crc=crc(&h,sizeof(h)-4);
   if(!io.write(0,&h,sizeof(h)))return false;
  }
  if(h.magic!=HeaderMagic || h.version!=Version || h.bytes!=Bytes || h.record_bytes!=RecordBytes || h.crc!=crc(&h,sizeof(h)-4))return false;
  latest=count=bad=0;next=0;std::memset(index,0,Capacity*sizeof(*index));
  for(size_t i=0;i<Capacity;i++){
   if(!io.read(Sector+i*sizeof(r),&r,sizeof(r)))return false;
   if(valid(r)){index[i]=r.d.sequence;count++;if(r.d.sequence>latest){latest=r.d.sequence;next=(i+1)%Capacity;}}
   else if(!blank(&r,sizeof(r)))bad++;
  }
  return ready=true;
 }
 bool append(Record& r){
  if(!ready || latest==UINT32_MAX)return false;
  Record old;
  if(!io.read(Sector+next*sizeof(r),&old,sizeof(old)))return false;
  if(!blank(&old,sizeof(old))){
   constexpr size_t per=Sector/sizeof(Record);
   if(next%per)next=(next+per-next%per)%Capacity;
   if(!io.erase(Sector+next*sizeof(r),Sector))return false;
   for(size_t i=next;i<next+per;i++){if(index[i])count--;index[i]=0;}
  }
  r.d.sequence=latest+1;r.crc=crc(&r,sizeof(r)-4);
  if(!io.write(Sector+next*sizeof(r),&r,sizeof(r)))return false;
  Record verify;if(!io.read(Sector+next*sizeof(r),&verify,sizeof(verify))||!valid(verify))return false;
  index[next]=r.d.sequence;count++;latest=r.d.sequence;next=(next+1)%Capacity;return true;
 }
 bool after(uint32_t sequence,Record& r){
  uint32_t best=UINT32_MAX;size_t slot=Capacity;
  for(size_t i=0;i<Capacity;i++)if(index[i]>sequence && index[i]<best){best=index[i];slot=i;}
  return slot<Capacity && io.read(Sector+slot*sizeof(r),&r,sizeof(r)) && valid(r);
 }
};
}
