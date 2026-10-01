#include "WirelessAudio.h"
#include "../../main/mic_adpcm.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace {
constexpr uint32_t Ring=16384,Block=320,Prime=640;
int16_t samples[Ring];std::atomic<uint32_t> readAt{0},writeAt{0},flushTo{0};
std::atomic<uint32_t> resetGeneration{0};uint32_t renderedGeneration=0;
std::atomic<uint64_t> accepted{0},lost{0},underflows{0},overflows{0},rendered{0};
AudioQueueRef queue=nullptr;AudioQueueBufferRef buffers[3]={};
char status[160]="Wireless microphone not started";
bool haveSequence=false;uint32_t sequence=0;bool primed=false;
void render(void*,AudioQueueRef q,AudioQueueBufferRef b){
 auto generation=resetGeneration.load(std::memory_order_acquire);
 if(generation!=renderedGeneration){renderedGeneration=generation;primed=false;}
 auto r=readAt.load(std::memory_order_relaxed),flush=flushTo.load(std::memory_order_acquire),w=writeAt.load(std::memory_order_acquire);
 if(int32_t(flush-r)>0){r=flush;primed=false;}
 if(w-r>=Prime)primed=true;
 auto* out=(int16_t*)b->mAudioData;
 if(primed && w-r>=Block){for(unsigned i=0;i<Block;i++)out[i]=samples[(r+i)%Ring];r+=Block;rendered+=Block;}
 else {std::memset(out,0,Block*2);if(primed){underflows++;primed=false;}}
 readAt.store(r,std::memory_order_release);b->mAudioDataByteSize=Block*2;AudioQueueEnqueueBuffer(q,b,0,nullptr);
}
bool push(const int16_t* pcm){
 auto w=writeAt.load(std::memory_order_relaxed),r=readAt.load(std::memory_order_acquire);
 // Bound accumulated latency after a stalled callback/clock drift. Only the
 // consumer advances readAt; a release/acquire handoff requests its resync.
 if(w-r>2560){flushTo.store(w-Prime,std::memory_order_release);overflows++;}
 if(w-r>Ring-Block)return false;
 for(unsigned i=0;i<Block;i++)samples[(w+i)%Ring]=pcm[i];writeAt.store(w+Block,std::memory_order_release);return true;
}
}
int ditto_audio_start(){
 if(queue)return 0;
 // Resolve the installed driver's persistent UID, never a default/speaker.
 AudioObjectPropertyAddress a{kAudioHardwarePropertyDeviceForUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
 CFStringRef uid=CFSTR("BlackHole2ch_UID");AudioDeviceID device=0;
 AudioValueTranslation tr{&uid,sizeof(uid),&device,sizeof(device)};UInt32 n=sizeof(tr);
 OSStatus e=AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,nullptr,&n,&tr);
 if(e||!device){snprintf(status,sizeof(status),"Install BlackHole 2ch to use Ditto's wireless microphone");return -1;}
 AudioStreamBasicDescription fmt{};fmt.mSampleRate=16000;fmt.mFormatID=kAudioFormatLinearPCM;
 fmt.mFormatFlags=kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked;fmt.mBytesPerPacket=2;fmt.mFramesPerPacket=1;fmt.mBytesPerFrame=2;fmt.mChannelsPerFrame=1;fmt.mBitsPerChannel=16;
 e=AudioQueueNewOutput(&fmt,render,nullptr,nullptr,nullptr,0,&queue);
 if(!e)e=AudioQueueSetProperty(queue,kAudioQueueProperty_CurrentDevice,&uid,sizeof(uid));
 if(!e){for(auto& b:buffers){e=AudioQueueAllocateBuffer(queue,Block*2,&b);if(e)break;memset(b->mAudioData,0,Block*2);b->mAudioDataByteSize=Block*2;e=AudioQueueEnqueueBuffer(queue,b,0,nullptr);if(e)break;}}
 if(!e)e=AudioQueueStart(queue,nullptr);
 if(e){if(queue)AudioQueueDispose(queue,true);queue=nullptr;snprintf(status,sizeof(status),"Wireless microphone output error %d",int(e));return int(e);}
 snprintf(status,sizeof(status),"Wireless microphone ready · BlackHole 2ch");return 0;
}
void ditto_audio_stop(){if(queue){AudioQueueStop(queue,true);AudioQueueDispose(queue,true);queue=nullptr;}readAt=0;writeAt=0;flushTo=0;primed=false;haveSequence=false;}
void ditto_audio_reset(){flushTo.store(writeAt.load(),std::memory_order_release);resetGeneration.fetch_add(1,std::memory_order_release);haveSequence=false;}
int ditto_audio_packet(const uint8_t* bytes,size_t count){
 int16_t pcm[MicADPCM::Samples];uint32_t q;
 if(!MicADPCM::decode(bytes,count,pcm,q))return 0;
 if(haveSequence){uint32_t gap=q-sequence;if(gap==0||gap>0x80000000u)return 0;if(gap>1){lost+=gap-1;int16_t silence[Block]={};for(unsigned i=1;i<std::min(gap,6u);i++)push(silence);}}
 sequence=q;haveSequence=true;accepted++;if(queue)push(pcm);return 1;
}
const char* ditto_audio_status(){return status;}
uint64_t ditto_audio_metric(int which){switch(which){case 0:return accepted;case 1:return lost;case 2:return underflows;case 3:return overflows;case 4:return rendered;case 5:return writeAt.load()-readAt.load();default:return 0;}}

int ditto_audio_decode(const uint8_t* bytes,size_t count,int16_t* pcm){uint32_t seq;return MicADPCM::decode(bytes,count,pcm,seq)?320:0;}
