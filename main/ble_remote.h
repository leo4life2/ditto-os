#pragma once
#include <cstdint>
#include <cstddef>
void ble_remote_init();
void ble_remote_tick();
bool ble_remote_connected();
bool ble_remote_available();
size_t board_remote_packet(uint8_t* out,size_t size);

struct cJSON;
void ble_remote_boot();
void ble_remote_status(cJSON* out);

bool ble_remote_recording();
void ble_remote_microphone(const int16_t* pcm,size_t count,bool muted);
