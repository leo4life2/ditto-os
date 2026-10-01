"""Run production JSON emission with partial USB writes and competing senders."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/main.cpp').read_text();body=s[s.index('static SemaphoreHandle_t output_mutex;'):s.index('void event_message(')]
cjson=ROOT.parent/'esp-idf/components/json/cJSON'
prefix=r'''
#include "cJSON.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdio>
using SemaphoreHandle_t=std::mutex*;
constexpr int portMAX_DELAY=0,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4;
std::atomic<int> allocations{0},live{0};bool fail_internal=false;
void* heap_caps_malloc(size_t n,int caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) || caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(fail_internal && (caps&MALLOC_CAP_INTERNAL))return nullptr;auto p=malloc(n);if(p){allocations++;live++;}return p;}
void heap_caps_free(void* p){if(p){live--;free(p);}}
void xSemaphoreTake(std::mutex*m,int){m->lock();}void xSemaphoreGive(std::mutex*m){m->unlock();}
int pdMS_TO_TICKS(int n){return n;}
std::string wire;
int usb_serial_jtag_write_bytes(const char*s,size_t n,int){n=std::min(n,size_t(37));wire.append(s,n);return n;}
'''
suffix=r'''
int main(){std::mutex lock;output_mutex=&lock;cJSON_Hooks hooks={JsonStorage::allocate,heap_caps_free};cJSON_InitHooks(&hooks);
output_buffer=(char*)heap_caps_malloc(OUTPUT_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);int maxbuf=live;fail_internal=true;auto fallback=JsonStorage::allocate(96);assert(fallback);heap_caps_free(fallback);fail_internal=false;
auto send=[](int id,int size){auto v=cJSON_CreateObject();cJSON_AddNumberToObject(v,"id",id);
std::string payload(size,'a');if(size>12)payload.replace(0,12,"quote\" \\ \n\t");cJSON_AddStringToObject(v,"payload",payload.c_str());emit(v);};
int before=allocations;auto v=cJSON_CreateObject();cJSON_AddStringToObject(v,"event","audio.input");cJSON_AddStringToObject(v,"pcm","AA==");int built=allocations;emit(v);assert(allocations==built && live==maxbuf);
std::thread a([&]{assert(!JsonStorage::external);for(int i=0;i<400;i++){send(i,856);assert(!JsonStorage::external);}});
std::thread b([&]{JsonStorage::External bulk;for(int i=400;i<800;i++){send(i,6500);assert(JsonStorage::external);}});a.join();b.join();
send(800,30000);assert(live==maxbuf);heap_caps_free(output_buffer);output_buffer=nullptr;send(801,16);assert(live==0);
size_t offset=0;int count=0;bool seen[802]={};while(offset<wire.size()){
assert(wire.compare(offset,6,"SCOUT ")==0);auto end=wire.find('\n',offset);assert(end!=std::string::npos);
auto r=cJSON_Parse(wire.substr(offset+6,end-offset-6).c_str());assert(r);
auto id=cJSON_GetObjectItemCaseSensitive(r,"id");if(id){assert(id->valueint>=0 && id->valueint<802 && !seen[id->valueint]);seen[id->valueint]=true;auto p=cJSON_GetObjectItemCaseSensitive(r,"payload");assert(cJSON_IsString(p));int size=id->valueint<400?856:id->valueint<800?6500:id->valueint==800?30000:16;std::string expected(size,'a');expected.replace(0,12,"quote\" \\ \n\t");assert(expected==p->valuestring);}
cJSON_Delete(r);count++;offset=end+1;}
assert(count==803 && live==0);for(bool s:seen)assert(s);
printf("803 exact JSON records, concurrent status/audio-sized replies, 37-byte USB writes, escaping, oversized and missing-buffer/low-internal-memory fallback: no leaks; normal formatting allocates nothing.\n");}
'''
with tempfile.TemporaryDirectory(prefix='scout-json-output-') as d:
 p=Path(d);(p/'esp_heap_caps.h').write_text('#pragma once\n')
 (p/'test.cpp').write_text(prefix+'#include \"json_storage.h\"\n'+body+suffix)
 subprocess.run(['cc','-O2','-I',str(cjson),'-c',str(cjson/'cJSON.c'),'-o',str(p/'cjson.o')],check=True)
 subprocess.run(['c++','-std=c++17','-O2','-pthread','-I',str(p),'-I',str(ROOT/'main'),'-I',str(cjson),str(p/'test.cpp'),str(p/'cjson.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
