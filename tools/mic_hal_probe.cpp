// Capture the device's native Core Audio stream without AudioQueue conversion.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <ctime>
using Clock=std::chrono::steady_clock;
struct Probe{FILE* out;Clock::time_point begin;double first=-1;uint64_t frames=0;AudioStreamBasicDescription format;};
static void check(OSStatus s,const char* what){if(s){fprintf(stderr,"%s: %d\n",what,int(s));exit(2);}}
static OSStatus capture(AudioDeviceID,const AudioTimeStamp*,const AudioBufferList* in,const AudioTimeStamp* stamp,AudioBufferList*,const AudioTimeStamp*,void* ctx){
 auto& p=*(Probe*)ctx;if(!in||!in->mNumberBuffers)return noErr;auto& b=in->mBuffers[0];if(!b.mData||!b.mDataByteSize)return noErr;
 double now=std::chrono::duration<double>(Clock::now()-p.begin).count();if(p.first<0)p.first=now;
 size_t n=b.mDataByteSize/p.format.mBytesPerFrame;double sum=0;int peak=0;
 int16_t pcm[8192];if(n>8192)return noErr;
 for(size_t i=0;i<n;i++){
  float v=(p.format.mFormatFlags&kAudioFormatFlagIsFloat)?((float*)b.mData)[i*b.mNumberChannels]:((int16_t*)b.mData)[i*b.mNumberChannels]/32768.f;
  pcm[i]=int16_t(std::clamp(v,-1.f,.999969f)*32768);sum+=double(pcm[i])*pcm[i];peak=std::max(peak,abs(int(pcm[i])));
 }
 fwrite(pcm,2,n,p.out);p.frames+=n;fprintf(stderr,"block %.6f %zu %.3f %d sample %.0f\n",now,n,sqrt(sum/n),peak,stamp->mSampleTime);return noErr;
}
int main(int argc,char**argv){
 if(argc<2)return 1;AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};UInt32 n=0;check(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,nullptr,&n),"device list size");std::vector<AudioDeviceID>d(n/4);check(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,nullptr,&n,d.data()),"devices");AudioDeviceID target=0;
 // Optional persistent UID selects an exact virtual input without changing
 // the Mac's default microphone or speaker. The original USB probe is default.
 for(auto id:d){a.mSelector=argc>3?kAudioDevicePropertyDeviceUID:kAudioObjectPropertyName;CFStringRef name;n=sizeof(name);check(AudioObjectGetPropertyData(id,&a,0,nullptr,&n,&name),"name/UID");char s[256];CFStringGetCString(name,s,256,kCFStringEncodingUTF8);CFRelease(name);if(argc>3?strcmp(s,argv[3])==0:strstr(s,"Ditto")!=nullptr){target=id;break;}}
 if(!target)return 3;
 Probe p{};p.out=fopen(argv[1],"wb");if(!p.out)return 4;
 a={kAudioDevicePropertyStreamFormat,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain};n=sizeof(p.format);check(AudioObjectGetPropertyData(target,&a,0,nullptr,&n,&p.format),"input format");fprintf(stderr,"format %.0f Hz flags %u bits %u channels %u bytes_frame %u\n",p.format.mSampleRate,p.format.mFormatFlags,p.format.mBitsPerChannel,p.format.mChannelsPerFrame,p.format.mBytesPerFrame);
 const bool sampleOK=(p.format.mFormatFlags&kAudioFormatFlagIsFloat)?p.format.mBitsPerChannel==32:p.format.mBitsPerChannel==16;
 if(p.format.mFormatID!=kAudioFormatLinearPCM||!sampleOK||p.format.mSampleRate<8000||p.format.mSampleRate>192000||p.format.mChannelsPerFrame<1||p.format.mChannelsPerFrame>8)return 5;
 AudioDeviceIOProcID id;check(AudioDeviceCreateIOProcID(target,capture,&p,&id),"create IO");p.begin=Clock::now();timespec mono{};clock_gettime(CLOCK_MONOTONIC,&mono);fprintf(stderr,"origin_mono %.9f\n",mono.tv_sec+mono.tv_nsec*1e-9);check(AudioDeviceStart(target,id),"start IO");std::this_thread::sleep_for(std::chrono::milliseconds(int((argc>2?atof(argv[2]):12)*1000)));check(AudioDeviceStop(target,id),"stop IO");check(AudioDeviceDestroyIOProcID(target,id),"destroy IO");fclose(p.out);fprintf(stderr,"result first %.6f samples %llu\n",p.first,p.frames);
}
