#include "scout.h"
#include "surface_span_reference.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
extern "C" void scout_surface_span_s3(uint16_t*,uint16_t*,const uint16_t*,int,int32_t,int32_t,int32_t,int32_t,int32_t,unsigned);
using Ref=SpanReference::Shader;
struct SpanBuffers { uint16_t reference[416],reference_z[416],actual[416],actual_z[416]; };
// Hardware-only corpus. Timing blocks may be constant-propagated: use frame_profile.py for speed.
extern "C" void scout_span_diagnostic() {
    auto* b=static_cast<SpanBuffers*>(heap_caps_malloc(sizeof(SpanBuffers),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    auto* palette=static_cast<uint16_t*>(heap_caps_malloc((2048+2)*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    cJSON* o=cJSON_CreateObject();cJSON_AddStringToObject(o,"event","debug.span.result");
    if(!b||!palette){cJSON_AddStringToObject(o,"error","Diagnostic buffers unavailable");heap_caps_free(b);heap_caps_free(palette);emit(o);return;}
    uint32_t rng=0x41a3289f;
    auto random=[&](){rng=rng*1664525u+1013904223u;return rng;};
    for(int i=0;i<2050;i++)palette[i]=uint16_t(random());
    uint32_t cases=0,pixels=0,mismatches=0;
    auto compare=[&](int count,int offset,int palette_offset,int32_t z,int32_t dz,int32_t light,int32_t dl,int32_t ddl,unsigned phase){
        for(int i=0;i<416;i++){b->reference[i]=b->actual[i]=uint16_t(random());b->reference_z[i]=b->actual_z[i]=uint16_t(random());}
        Ref::_surfaceSpan(b->reference+offset,b->reference_z+offset,palette+palette_offset,count,z,dz,light,dl,ddl,phase,32);
        scout_surface_span_s3(b->actual+offset,b->actual_z+offset,palette+palette_offset,count,z,dz,light,dl,ddl,phase);
        cases++;pixels+=std::max(0,count);
        mismatches+=memcmp(b->reference,b->actual,sizeof(b->reference))!=0;
        mismatches+=memcmp(b->reference_z,b->actual_z,sizeof(b->reference_z))!=0;
    };
    const int32_t limits[]={INT32_MIN,INT32_MAX,-268435457,-4096,-1,0,1,4095,4096,268431360,268435455,268435456};
    const int32_t lights[]={INT32_MIN,INT32_MAX,-131073,-257,-1,0,1,130815,130816,131071,131072};
    for(auto z:limits)for(auto l:lights)for(unsigned ph=0;ph<4;ph++)compare(1,1,ph&1,z,0,l,0,0,ph);
    for(int n:{-4,-1,0})compare(n,1,0,0,0,0,0,0,0);
    for(int test=0;test<12000;test++){
        int count=random()%411,offset=1+random()%3;
        int32_t z=int32_t(random()%360000001)-40000000,dz=int32_t(random()%40001)-20000;
        int32_t light=int32_t(random()%600001)-200000,dl=int32_t(random()%4001)-2000,ddl=int32_t(random()%41)-20;
        compare(count,offset,test&1,z,dz,light,dl,ddl,random()%4);
        if(test%64==0)vTaskDelay(1);
    }
    cJSON_AddNumberToObject(o,"cases",cases);cJSON_AddNumberToObject(o,"pixels",pixels);cJSON_AddNumberToObject(o,"mismatches",mismatches);
    auto timed=[&](bool candidate){
        int64_t start=esp_timer_get_time();
        for(int j=0;j<64;j++){
            std::fill_n(b->actual_z,410,0);
            if(candidate)scout_surface_span_s3(b->actual,b->actual_z,palette,410,100000000,200,23000,95,1,j&3);
            else Ref::_surfaceSpan(b->actual,b->actual_z,palette,410,100000000,200,23000,95,1,j&3,32);
        }
        return esp_timer_get_time()-start;
    };
    cJSON* times=cJSON_AddArrayToObject(o,"timings_us");
    for(int i=0;i<20;i++){
        int64_t a,btime;
        if(i&1){btime=timed(true);a=timed(false);}else{a=timed(false);btime=timed(true);}
        auto* item=cJSON_CreateObject();cJSON_AddNumberToObject(item,"reference",a);cJSON_AddNumberToObject(item,"candidate",btime);cJSON_AddItemToArray(times,item);vTaskDelay(1);
    }
    heap_caps_free(b);heap_caps_free(palette);emit(o);
}
