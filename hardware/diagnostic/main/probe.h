#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/i2c_master.h"
extern i2c_master_bus_handle_t probe_bus;
extern bool probe_found[128];
void probe_heaps(const char *stage);
void probe_sensors(void);
void probe_sd(void);
void probe_display(void);
void probe_display_load(unsigned frames, const char *condition);
void probe_display_staged(unsigned frames, const char *condition);
void probe_audio(void);
void probe_audio_stop(void);
void probe_radios(void);
void probe_status_screen(void);
void probe_calibration_screen(int page);
esp_err_t probe_read(uint8_t addr, uint8_t reg, void *buf, size_t len);
esp_err_t probe_write(uint8_t addr, uint8_t reg, uint8_t value);
#define RESULT(...) do {printf("PROBE "); printf(__VA_ARGS__); printf("\n"); fflush(stdout);} while(0)
#define TRY(x) do {esp_err_t probe_try_error_=(x); if(probe_try_error_!=ESP_OK){RESULT("{\"test\":\"error\",\"operation\":\"%s\",\"error\":\"%s\"}",#x,esp_err_to_name(probe_try_error_)); return;}} while(0)
