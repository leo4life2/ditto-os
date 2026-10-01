// Exercise the production decoder/jitter buffer without opening audio hardware.
#define AudioQueueEnqueueBuffer mockedEnqueue
#include "../host/mac-remote/WirelessAudio.mm"
#undef AudioQueueEnqueueBuffer
#include <cassert>
extern "C" OSStatus mockedEnqueue(AudioQueueRef,AudioQueueBufferRef,UInt32,const AudioStreamPacketDescription*){return 0;}
int main(){
 int16_t pcm[320],decoded[320],out[320];for(unsigned i=0;i<320;i++)pcm[i]=int16_t(i*80-10000);
 uint8_t packet[172];int index=0;AudioStreamPacketDescription desc{};AudioQueueBuffer buffer(out,sizeof(out),&desc,0);
 // An inert non-null token enables the production ring path; never start/stop hardware.
 queue=reinterpret_cast<AudioQueueRef>(uintptr_t(1));
 MicADPCM::encode(pcm,packet,1,index);assert(ditto_audio_packet(packet,172));assert(!ditto_audio_packet(packet,172));
 render(nullptr,nullptr,&buffer);for(auto v:out)assert(v==0); // short burst waits for priming
 MicADPCM::encode(pcm,packet,2,index);assert(ditto_audio_packet(packet,172));render(nullptr,nullptr,&buffer);assert(rendered==320);
 MicADPCM::encode(pcm,packet,4,index);assert(ditto_audio_packet(packet,172)&&lost==1); // one loss, independent recovery
 render(nullptr,nullptr,&buffer);render(nullptr,nullptr,&buffer);for(auto v:out)assert(v==0); // concealed block
 render(nullptr,nullptr,&buffer);assert(ditto_audio_decode(packet,172,decoded)==320);for(unsigned i=0;i<320;i++)assert(out[i]==decoded[i]);
 // Fresh utterance flushes preceding queued audio; no replay across reconnect.
 ditto_audio_reset();MicADPCM::encode(pcm,packet,1,index);assert(ditto_audio_packet(packet,172));render(nullptr,nullptr,&buffer);for(auto v:out)assert(v==0);
 for(unsigned i=2;i<30;i++){MicADPCM::encode(pcm,packet,i,index);assert(ditto_audio_packet(packet,172));}
 render(nullptr,nullptr,&buffer);assert(ditto_audio_metric(5)<=2560);assert(overflows>0);
 queue=nullptr;puts("PASS: production buffer priming, duplicate rejection, one-block loss concealment, independent decode, reconnect reset and bounded latency resync");
}
