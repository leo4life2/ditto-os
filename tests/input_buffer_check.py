"""Production command framing: capacity, fragmentation, overflow and recovery."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from storage_hygiene import scratch_directory
source=(root/'main/main.cpp').read_text()
body=source[source.index('  char *const line=input_buffer;'):].rsplit('}',1)[0]
cjson=root.parent/'esp-idf/components/json/cJSON'
prefix=r'''
#include "cJSON.h"
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
constexpr int portMAX_DELAY=0;
bool ui_capture_busy(){return false;} bool ui_capture_step(){return false;}
int pdMS_TO_TICKS(int n){return n;} void vTaskDelay(int){}
constexpr size_t INPUT_BYTES=4096;
std::array<char,INPUT_BYTES+16> memory;
char* input_buffer=memory.data()+8;
std::string stream;size_t offset=0;int fragment=1;
std::vector<std::string> ids,errors;
struct End{};
int scout_usb_read(char* dst,size_t n,int){
 if(offset==stream.size())throw End{};
 n=std::min({n,size_t(fragment),stream.size()-offset});
 memcpy(dst,stream.data()+offset,n);offset+=n;return int(n);
}
void event_message(const char*,const char* s){errors.emplace_back(s);}
void dispatch(cJSON* r){auto id=cJSON_GetObjectItemCaseSensitive(r,"id");assert(cJSON_IsString(id));ids.emplace_back(id->valuestring);}
void run(){
'''
suffix=r'''
}
int main(){
 const std::string first="{\"id\":\"largest\"}",last="{\"id\":\"recovered\"}";
 // Exactly 4,095 data bytes is valid; 4,096 is overflow. A discarded command
 // cannot leak a suffix into the next one, including across USB boundaries.
 stream="\r\n"+first+std::string(4095-first.size(),' ')+"\r\n"+
        first+std::string(4096-first.size(),' ')+"\n{broken}\n[1,2]\n"+last+"\n";
 for(fragment=1;fragment<=256;fragment++) {
  memory.fill(char(0xa7));offset=0;ids.clear();errors.clear();
  try{run();}catch(End&){}
  assert((ids==std::vector<std::string>{"largest","recovered"}));
  assert((errors==std::vector<std::string>{"Command too long","Invalid JSON command","Invalid JSON command"}));
  for(int i=0;i<8;i++)assert(uint8_t(memory[i])==0xa7 && uint8_t(memory[memory.size()-1-i])==0xa7);
  for(size_t i=0;i<INPUT_BYTES-1;i++)assert(input_buffer[i]==0);
 }
 puts("256 USB fragment sizes: 4,095-byte commands accepted, 4,096 rejected, malformed input discarded, next command recovered, buffers wiped and canaries intact.");
}
'''
with scratch_directory('usb-input',reserve_mib=32) as td:
 out=Path(td);(out/'test.cpp').write_text(prefix+body+suffix)
 flags=['-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cjson)]
 subprocess.run(['cc',*flags,'-c',str(cjson/'cJSON.c'),'-o',str(out/'cjson.o')],check=True)
 subprocess.run(['c++','-std=c++17',*flags,str(out/'test.cpp'),str(out/'cjson.o'),'-o',str(out/'test')],check=True)
 subprocess.run([str(out/'test')],check=True)
