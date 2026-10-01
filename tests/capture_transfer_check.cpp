#include "cJSON.h"
#include "mbedtls/base64.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
constexpr int SCREEN_W=410,SCREEN_H=502,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
constexpr int pdTRUE=1,portMAX_DELAY=-1;
struct Sem{bool token;};static Sem ready{false},mutex{true};
static Sem* capture_done=&ready;static Sem* ui_mutex=&mutex;
int xSemaphoreTake(Sem* s,int){if(s==&mutex)return 1;bool old=s->token;s->token=false;return old;}
void xSemaphoreGive(Sem* s){s->token=true;}
static bool allocation_failure=false,encoding_failure=false;
void* heap_caps_malloc(size_t n,int){return allocation_failure?nullptr:std::malloc(n);}
void heap_caps_free(void* p){std::free(p);}
int64_t now=0;int64_t esp_timer_get_time(){return now;}
static std::atomic<bool> capture_pending{false};
static std::vector<uint16_t> pixels(SCREEN_W*SCREEN_H);static uint16_t* capture_pixels=pixels.data();
static std::vector<std::string> records;
void emit(cJSON* o){char* s=cJSON_PrintUnformatted(o);assert(s);records.emplace_back(s);cJSON_free(s);cJSON_Delete(o);}
int encode(unsigned char* out,size_t size,size_t* n,const unsigned char* src,size_t len){
  return encoding_failure?-1:mbedtls_base64_encode(out,size,n,src,len);
}
#define mbedtls_base64_encode encode
#include "capture.inc"
#undef mbedtls_base64_encode
void complete(){assert(capture_pending.exchange(false));ready.token=true;}
void rejected(const std::string& id) {
  cJSON* r=cJSON_Parse(records.back().c_str());assert(r);
  assert(std::string(cJSON_GetObjectItem(r,"id")->valuestring)==id);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
}
int main() {
  for(size_t i=0;i<pixels.size();i++)pixels[i]=uint16_t(i*397+123);
  assert(!ui_capture_busy() && !ui_capture_step());
  // Owned ID outlives the parsed input; waiting never blocks command dispatch.
  std::string id="owned \"ID\" \\ newline\n";std::string original=id;
  ui_capture(id.c_str());id.assign(id.size(),'X');assert(ui_capture_busy());
  for(int i=0;i<50;i++){now+=1000;assert(!ui_capture_step());}
  ui_capture("concurrent");rejected("concurrent");records.clear();complete();
  std::vector<uint8_t> captured(pixels.size()*2);size_t rows=0;
  while(ui_capture_busy()) {
    const size_t before=records.size();assert(ui_capture_step());
    assert(records.size()-before==(rows+2==SCREEN_H?2:1));
    cJSON* r=cJSON_Parse(records[before].c_str());assert(r);
    assert(std::string(cJSON_GetObjectItem(r,"id")->valuestring)==original);
    int y=cJSON_GetObjectItem(r,"y")->valueint,h=cJSON_GetObjectItem(r,"rows")->valueint;
    assert(y==int(rows) && h==2);
    auto s=cJSON_GetObjectItem(r,"pixels")->valuestring;size_t n=0;
    assert(!mbedtls_base64_decode(captured.data()+y*SCREEN_W*2,h*SCREEN_W*2,&n,reinterpret_cast<unsigned char*>(s),strlen(s)));
    assert(n==size_t(h*SCREEN_W*2));rows+=h;cJSON_Delete(r);
  }
  assert(rows==SCREEN_H && !memcmp(captured.data(),pixels.data(),captured.size()));
  cJSON* done=cJSON_Parse(records.back().c_str());assert(cJSON_IsTrue(cJSON_GetObjectItem(done,"ok")));cJSON_Delete(done);
  // A timeout replies once; late publication is discarded before reuse.
  records.clear();ui_capture("slow");now+=5000001;assert(ui_capture_step());rejected("slow");
  for(int i=0;i<50;i++)assert(!ui_capture_step());assert(records.size()==1);
  ui_capture("too early");rejected("too early");complete();assert(!ui_capture_step() && !ui_capture_busy());
  ui_capture("after timeout");assert(ui_capture_busy());complete();while(ui_capture_busy())assert(ui_capture_step());
  // Failed allocation/not-ready/encoding cannot wedge future requests.
  allocation_failure=true;ui_capture("allocation");rejected("allocation");assert(!ui_capture_busy());allocation_failure=false;
  capture_done=nullptr;ui_capture("not ready");rejected("not ready");capture_done=&ready;
  ui_capture("encode");complete();encoding_failure=true;assert(ui_capture_step());rejected("encode");assert(!ui_capture_busy());encoding_failure=false;
  std::cout<<"PASS: full 410x502 bytes, 251 incremental chunks, owned/escaped IDs, busy rejection, timeout/late-publication drain, allocation/encoding recovery; ASan/UBSan\n";
}
