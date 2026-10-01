#pragma once
#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
void scout_usb_init();
int scout_usb_write(const char*,size_t);
int scout_usb_read(char*,size_t,TickType_t);
void scout_usb_microphone(int16_t*,size_t,bool muted);
bool scout_usb_connected();
bool scout_usb_recording();
uint32_t scout_usb_audio_drops();
uint32_t scout_usb_audio_packets();
void scout_usb_bootloader();

float scout_usb_first_audio_ms();
uint32_t scout_usb_audio_starts();

void scout_usb_test_pattern(uint32_t duration_ms);
uint32_t scout_usb_metric(unsigned index);
