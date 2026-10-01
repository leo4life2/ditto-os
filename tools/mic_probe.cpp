// Direct Core Audio capture: request an explicit format and timestamp the first
// callback. Avoids AVFoundation's format-changing buffers during other clients.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <cstring>
#include <algorithm>
using Clock=std::chrono::steady_clock;
struct Capture {FILE* out; Clock::time_point start; double first=-1; uint64_t frames=0,clipped=0;double sum=0;int peak=0;};
static void check(OSStatus e,const char* s){if(e){fprintf(stderr,"%s: %d\n",s,int(e));exit(2);}}
static void callback(void* ctx,AudioQueueRef q,AudioQueueBufferRef b,const AudioTimeStamp* stamp,UInt32,const AudioStreamPacketDescription*){
 auto& c=*(Capture*)ctx;double t=std::chrono::duration<double>(Clock::now()-c.start).count();if(c.first<0)c.first=t;
 auto a=(const int16_t*)b->mAudioData;size_t n=b->mAudioDataByteSize/2;double sum=0;int peak=0;
 for(size_t i=0;i<n;i++){sum+=double(a[i])*a[i];peak=std::max(peak,std::abs(int(a[i])));if(std::abs(int(a[i]))>=32760)c.clipped++;}
 fprintf(stderr,"block %.6f %zu %.3f %d sample_time %.0f\n",t,n,sqrt(sum/std::max(size_t(1),n)),peak,stamp->mSampleTime);
 c.frames+=n;c.sum+=sum;c.peak=std::max(c.peak,peak);fwrite(a,2,n,c.out);AudioQueueEnqueueBuffer(q,b,0,nullptr);
}
int main(int argc,char**argv){
 AudioObjectPropertyAddress addr{kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};UInt32 size=0;
 check(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&addr,0,nullptr,&size),"devices size");std::vector<AudioDeviceID>d(size/sizeof(AudioDeviceID));check(AudioObjectGetPropertyData(kAudioObjectSystemObject,&addr,0,nullptr,&size,d.data()),"devices");CFStringRef uid=nullptr;
 for(auto id:d){addr.mSelector=kAudioObjectPropertyName;CFStringRef name=nullptr;size=sizeof(name);check(AudioObjectGetPropertyData(id,&addr,0,nullptr,&size,&name),"name");char text[256];CFStringGetCString(name,text,sizeof(text),kCFStringEncodingUTF8);CFRelease(name);
  if(strstr(text,"Ditto")){addr.mSelector=kAudioDevicePropertyDeviceUID;size=sizeof(uid);check(AudioObjectGetPropertyData(id,&addr,0,nullptr,&size,&uid),"uid");fprintf(stderr,"device %s\n",text);
   for(auto scope:{kAudioObjectPropertyScopeInput,kAudioObjectPropertyScopeOutput})for(auto elem:{0u,1u})for(auto sel:{kAudioDevicePropertyVolumeScalar,kAudioDevicePropertyVolumeDecibels,kAudioDevicePropertyMute}){AudioObjectPropertyAddress a{sel,scope,elem};if(!AudioObjectHasProperty(id,&a))continue;size=4;float v;UInt32 b;void* p=sel==kAudioDevicePropertyMute?(void*)&b:(void*)&v;auto e=AudioObjectGetPropertyData(id,&a,0,nullptr,&size,p);fprintf(stderr,"property %u %u %u = %.3f (err %d)\n",scope,elem,sel,sel==kAudioDevicePropertyMute?double(b):v,int(e));}
   break;
  }
 }
 if(!uid){fprintf(stderr,"Ditto not found\n");return 3;}
 if(argc<2){CFRelease(uid);return 0;}
 Capture c{};c.out=fopen(argv[1],"wb");if(!c.out)return 4;
 AudioStreamBasicDescription f{};f.mSampleRate=16000;f.mFormatID=kAudioFormatLinearPCM;f.mFormatFlags=kLinearPCMFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked;f.mBytesPerPacket=f.mBytesPerFrame=2;f.mFramesPerPacket=1;f.mChannelsPerFrame=1;f.mBitsPerChannel=16;
 AudioQueueRef q;check(AudioQueueNewInput(&f,callback,&c,CFRunLoopGetCurrent(),kCFRunLoopCommonModes,0,&q),"new input");check(AudioQueueSetProperty(q,kAudioQueueProperty_CurrentDevice,&uid,sizeof(uid)),"select input");CFRelease(uid);
 for(int i=0;i<3;i++){AudioQueueBufferRef b;check(AudioQueueAllocateBuffer(q,2048,&b),"allocate");check(AudioQueueEnqueueBuffer(q,b,0,nullptr),"enqueue");}
 c.start=Clock::now();check(AudioQueueStart(q,nullptr),"start");fprintf(stderr,"started %.6f\n",std::chrono::duration<double>(Clock::now()-c.start).count());CFRunLoopRunInMode(kCFRunLoopDefaultMode,argc>2?atof(argv[2]):5,false);check(AudioQueueStop(q,true),"stop");check(AudioQueueDispose(q,true),"dispose");fclose(c.out);fprintf(stderr,"result first=%.6f samples=%llu rms=%.3f peak=%d clipped=%llu\n",c.first,c.frames,sqrt(c.sum/std::max(uint64_t(1),c.frames)),c.peak,c.clipped);
}
