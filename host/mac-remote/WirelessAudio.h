#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int ditto_audio_start(void);
void ditto_audio_stop(void);
void ditto_audio_reset(void);
int ditto_audio_decode(const uint8_t* bytes,size_t count,int16_t* pcm);
int ditto_audio_packet(const uint8_t* bytes,size_t count);
const char* ditto_audio_status(void);
uint64_t ditto_audio_metric(int which);
#ifdef __cplusplus
}
#endif
