#include "wire_output.h"
#include "cJSON.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::mutex output_mutex;
static constexpr size_t OUTPUT_BYTES=24*1024;
static char scratch[OUTPUT_BYTES],*output_buffer=scratch;
static std::string wire;
static size_t calls=0,short_limit=1024,fail_call=0;
static std::atomic<size_t> allocations=0;
void* allocate(size_t n){allocations++;return std::malloc(n);}
namespace JsonStorage {struct External {};}
constexpr int portMAX_DELAY=0;
int pdMS_TO_TICKS(int n){return n;}
void xSemaphoreTake(std::mutex& m,int){m.lock();}
void xSemaphoreGive(std::mutex& m){m.unlock();}
int usb_serial_jtag_write_bytes(const char* p,size_t n,int) {
  assert(n>0 && n<=1024);calls++;
  if(calls==fail_call)return 0;
  n=std::min(n,short_limit);wire.append(p,n);return int(n);
}
#include "emit.inc"

void reset(){wire.clear();calls=0;short_limit=1024;fail_call=0;output_buffer=scratch;}
cJSON* parse(const std::string& s) {
  assert(s.rfind("SCOUT ",0)==0 && s.back()=='\n');
  cJSON* j=cJSON_Parse(s.c_str()+6);assert(j);return j;
}
void reply(const std::string& text) {
  cJSON* j=cJSON_CreateObject();assert(j);
  cJSON_AddStringToObject(j,"id","test");
  cJSON_AddStringToObject(j,"text",text.c_str());emit(j);
}
int main() {
  cJSON_Hooks hooks{allocate,std::free};cJSON_InitHooks(&hooks);
  uint8_t pcm[640];for(size_t i=0;i<640;i++)pcm[i]=uint8_t(i*73+11);
  for(size_t n=0;n<=640;n++) {
    reset();size_t before=allocations;emit_audio_input(pcm,n);
    assert(allocations==before && calls==1);
    cJSON* j=parse(wire);assert(std::string(cJSON_GetObjectItem(j,"event")->valuestring)=="audio.input");
    const char* text=cJSON_GetObjectItem(j,"pcm")->valuestring;
    uint8_t decoded[640];size_t len=0;
    assert(PcmBase64::decode(decoded,sizeof(decoded),len,text,strlen(text)) && len==n);
    assert(!memcmp(decoded,pcm,n));cJSON_Delete(j);
    char packet[ScoutWire::AudioBytes+2];memset(packet,0x55,sizeof(packet));
    const size_t bytes=wire.size();
    assert(!ScoutWire::audio_frame(packet+1,bytes,pcm,n));
    assert(uint8_t(packet[0])==0x55 && uint8_t(packet[bytes+1])==0x55);
    assert(ScoutWire::audio_frame(packet+1,bytes+1,pcm,n)==bytes);
    assert(packet[bytes+1]==0 && uint8_t(packet[bytes+2])==0x55);
    assert(std::string(packet+1,bytes)==wire);
  }
  reset();emit_audio_input(pcm,641);emit_audio_input(nullptr,640);assert(wire.empty() && calls==0);
  for(size_t n:{0,2,900,1010,1017,1024,4096,24500,24576,33000,64000}) {
    std::string text(n,'x');if(n>=2){text[0]='"';text[1]='\n';}
    for(size_t partial:{1024,7})for(bool no_scratch:{false,true}) {
      reset();short_limit=partial;if(no_scratch)output_buffer=nullptr;reply(text);
      cJSON* j=parse(wire);assert(std::string(cJSON_GetObjectItem(j,"text")->valuestring)==text);cJSON_Delete(j);
      if(n<900 && partial==1024 && !no_scratch)assert(calls==1);
    }
  }
  // A failed large record is delimited, allowing the following reply to parse.
  reset();fail_call=2;reply(std::string(5000,'z'));assert(wire.back()=='\n');
  size_t end=wire.size();reply("after timeout");cJSON_Delete(parse(wire.substr(end)));
  reset();short_limit=13;
  std::vector<std::thread> threads;
  for(int t=0;t<6;t++)threads.emplace_back([&,t]{for(int i=0;i<100;i++) {
    if(t%2)emit_audio_input(pcm,sizeof(pcm));else reply(std::to_string(t)+":"+std::to_string(i));
  }});
  for(auto& t:threads)t.join();
  size_t start=0,records=0,audio=0;
  while(start<wire.size()) {
    size_t end=wire.find('\n',start);assert(end!=std::string::npos);
    cJSON* j=parse(wire.substr(start,end-start+1));
    if(cJSON_GetObjectItem(j,"pcm"))audio++;
    cJSON_Delete(j);records++;start=end+1;
  }
  assert(records==600 && audio==300);
  std::cout<<"PASS: 641 PCM lengths, zero audio JSON allocations, one-call normal packets, large/fallback/short writes, timeout recovery, 600 concurrent records; ASan/UBSan\n";
}
