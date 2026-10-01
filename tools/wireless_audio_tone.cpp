// Bounded diagnostic: feed a known tone through the production ADPCM decoder,
// jitter buffer and BlackHole output. Never selects or writes to speakers.
#include "../host/mac-remote/WirelessAudio.h"
#include "../main/mic_adpcm.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
int main(){
 if(ditto_audio_start()!=0){fprintf(stderr,"%s\n",ditto_audio_status());return 2;}
 using Clock=std::chrono::steady_clock;
 auto start=Clock::now();int step=0;
 for(unsigned block=0;block<250;block++){
  int16_t pcm[320];uint8_t packet[MicADPCM::Bytes];
  // Silence at both ends separates the signal from driver-startup noise.
  double frequency=block<125?440:880;
  for(unsigned i=0;i<320;i++)pcm[i]=(block<25||block>=225)?0:int16_t(10000*sin(2*3.141592653589793*frequency*(block*320+i)/16000));
  MicADPCM::encode(pcm,packet,block+1,step);ditto_audio_packet(packet,sizeof(packet));
  std::this_thread::sleep_until(start+std::chrono::milliseconds((block+1)*20));
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(250));
 printf("{\"accepted\":%llu,\"lost\":%llu,\"underflows\":%llu,\"overflows\":%llu,\"rendered_samples\":%llu}\n",
  (unsigned long long)ditto_audio_metric(0),(unsigned long long)ditto_audio_metric(1),
  (unsigned long long)ditto_audio_metric(2),(unsigned long long)ditto_audio_metric(3),(unsigned long long)ditto_audio_metric(4));
 ditto_audio_stop();return 0;
}
