"""Check absolute scan-time safety with early/late wakes and timer failure."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/board.cpp').read_text()
body=s[s.index('static void wait_until(int64_t target) {'):s.index('static void presentation_task(void*) {')]
prefix=r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
constexpr int pdTRUE=1,portMAX_DELAY=0xffffffff,ESP_OK=0;
int scan_timer=1,starts=0,yields=0,fail=0;
int64_t now=0,wake=0,jitter=0;
int64_t esp_timer_get_time(){return now;}
void esp_rom_delay_us(unsigned n){now+=n;}
int pdMS_TO_TICKS(int n){return n;}
void vTaskDelay(int n){now+=n*1000;yields++;}
void ulTaskNotifyTake(int,int delay){if(delay){now=wake+jitter;yields++;}}
int esp_timer_start_once(int,uint64_t duration){starts++;wake=now+duration;return fail;}
'''
suffix=r'''
int main(){int count=0;
for(int available=0;available<2;available++)for(int failure=0;failure<2;failure++)
for(int skew : {-30,0,20,100,1000})for(int remain : {-5,0,1,40,180,181,500,1500,1501,15000}) {
 scan_timer=available;fail=failure;jitter=skew;now=100000;starts=yields=0;
 const auto target=now+remain;wait_until(target);assert(now>=target);
 if(available && !failure && remain>180)assert(starts==1 && yields==1);
 if(remain<=0)assert(now==100000);count++;
}
printf("%d scan waits: never earlier than the target; timer unavailable/failure and wake jitter covered.\n",count);
}
'''
with tempfile.TemporaryDirectory(prefix='scout-scan-wait-') as d:
 p=Path(d);(p/'test.cpp').write_text('#include <initializer_list>\n'+prefix+body+suffix)
 subprocess.run(['c++','-std=c++17','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
