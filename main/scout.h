#pragma once
#include "cJSON.h"
#include "avatar_asset.h"
#include "blob_physics.h"
#include "touch_history.h"
#include "battery_status.h"
#include "scene_damage.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstddef>
#include <cstdint>

constexpr int SCREEN_W = 410, SCREEN_H = 502, SAFE = 50;
// Slightly shorter DMA/render tiles retain internal-RAM headroom for power
// management and Wi-Fi control tasks; native pixel resolution is unchanged.
constexpr int SCENE_TILE_ROWS = 12;
enum class Screen { Home, Volume, Brightness, Wifi, Microphone, Timer, System };
struct State {
  int volume = 30, brightness = 55;
  int battery_percent=-1;
  bool battery_charging=false,usb_connected=false,bluetooth_connected=false;
  Battery::Mode battery_mode=Battery::Mode::Unknown;
  bool battery_external=false,battery_present=false,battery_status_valid=false;
  bool battery_voltage_valid=false;
  uint8_t battery_status0=0,battery_status1=0;
  bool muted = false, wifi_connected = false, wifi_connecting = false,
       bridge = false;
  bool audio_stream = false, voice_active = false, sleeping = false;
  bool wifi_scanning = false, wifi_scan_done = false, wifi_scan_failed = false;
  char ssid[33] = {}, caption[128] = "Ready for you", conversation[16] = "idle";
  char wifi_draft_ssid[33] = {}, wifi_password[65] = {};
  char wifi_error[64] = {};
  int wifi_edit_index = -1, wifi_edit_count = 0, wifi_edit_removed = 0;
  char networks[4][33] = {};
  int network_count = 0;
  Screen screen = Screen::Home;
  int clip = Avatar::Idle, render_clip = Avatar::Idle;
  uint32_t clip_serial = 0, revision = 0;
  // Host action acknowledgement and bounded dictation lease. USB audio may
  // remain open briefly after Flow releases its recording keys.
  bool remote_dictating=false;
  int64_t remote_dictation_until_us=0,remote_audio_tail_until_us=0;
  uint32_t remote_reaction_serial=0,auto_hello_count=0,auto_reaction_count=0;
  int64_t auto_hello_at_us=0;
  bool emote_rig_active=false,emote_reaction_priority=false;
  float emote_joint_motion=0,emote_seconds=0;
  int expression = 0, effective_brightness = 55;
  Blob::Touch touch;
  BlobMotion::Sample motion;
  bool imu_available=false,imu_configured=false;
  uint32_t imu_samples=0,imu_errors=0,imu_retries=0;
  uint32_t imu_timestamp=0,imu_stale_reads=0,imu_stalls=0,imu_recoveries=0;
  int64_t imu_last_sample_us=0;
  uint8_t imu_fault_ctrl1=0,imu_fault_ctrl7=0;
  int64_t motion_sim_until_us=0;
  bool motion_active=false;
  int64_t last_motion_activity_us=0;
  uint32_t motion_wakes=0,touch_wakes=0;
  bool audio_sleeping=false,render_parked=false;
  bool sleep_overlay_active=false;
  uint32_t sleep_overlay_frames=0;
  float sleep_overlay_ms=0;
  uint32_t audio_power_errors=0,audio_input_blocks=0,audio_output_blocks=0,physics_steps=0;
  float motion_x=0,motion_y=0,motion_z=0,motion_bend_x=0,motion_bend_y=0,motion_bend_z=0;
  float motion_spin_x=0,motion_spin_y=0,motion_spin_z=0,motion_depth=0;
  BlobMotion::V motion_wave;
  BlobMotion::V material_scale{1,1,1},material_memory;
  BlobMotion::V material_shear,material_support,material_gravity;
  float material_volume=1;
  bool touch_available = false;
  uint32_t touch_samples = 0, touch_errors = 0, touch_contacts = 0, touch_retries = 0, touch_recoveries = 0;
  uint64_t touch_invalid_report = 0;
  int64_t touch_updated_us = 0, touch_sim_until_us = 0;
  float physics_ms = 0, physics_activity = 0, annoyance = 0;
  float glow_mask_ms=0,glow_blur_ms=0,glow_palette_ms=0;
  uint32_t tap_count=0,tap_seen_press=0;
  bool tap_hit=false;
  float tap_local_x=0,tap_local_y=0,tap_bounce_x=0,tap_bounce_y=0,tap_depth=0,body_squash=0;
  float tap_bounce_z=0,tap_ripple=0;
  float slime_stretch=0, slime_press=0, face_x_eyes=0, face_side_eye=0;

  BlobMotion::V face_gaze;
  float face_oval=0,face_melon=0,face_sleep=0,face_eye_open=1,face_eye_width=1,shake_damping=1,face_dizzy=0;
  int behavior_mode=0;uint32_t behavior_contacts=0;
  bool shake_active=false,shake_recovering=false;

  int64_t last_activity_us = 0, screen_until_us = 0, timer_end_us = 0;
  uint32_t timer_id = 0;
  float tilt_x = 0, tilt_y = 0, mic_rms = 0, speaker_rms = 0, battery_v = 0;
  float mic_raw_rms=0,mic_right_rms=0;
  int mic_raw_peak=0,mic_peak=0;bool mic_settling=false;uint32_t mic_dsp_max_us=0;
  uint32_t audio_rx_errors = 0, audio_overflows = 0, audio_tx_errors = 0;
  uint32_t frames = 0, missed_frames = 0, display_errors = 0;
  uint32_t frames_within25 = 0;
  uint32_t presented_frames=0,te_timeouts=0,scan_deadline_misses=0;
  uint32_t present_strips=0,present_unmerged_strips=0,present_pixels=0,present_unmerged_pixels=0;
  float te_hz=0,present_ms=0,te_wait_ms=0,scan_margin_ms=0;
  float frame_ms = 0, frame_p95_ms = 0, fps = 0;
  float anim_ms = 0, skin_ms = 0, bin_ms = 0, draw_ms = 0;
  float raster0_ms = 0, raster1_ms = 0;
  float compose_ms = 0, text_ms = 0, upload_ms = 0;
  float diff_ms = 0, copy_ms = 0, dma_wait_ms = 0, submit_ms = 0;
  uint32_t ui_stack_free = 0, render_stack_free = 0;
};
extern State state;
extern SemaphoreHandle_t state_mutex, ui_mutex;
State snapshot();
void notify_state();
void emit(cJSON *event);
void emit_audio_input(const uint8_t* pcm,size_t size);
void event_message(const char *type, const char *message);
void services_init();
void dispatch(cJSON *request);
void ui_task(void *);
void ui_reserve_memory();
void ui_capture(const char *request_id);
bool ui_capture_busy();
bool ui_capture_step();
void board_init();
void board_blit(int x, int y, int w, int h, const uint16_t *native_pixels,
                int source_stride = 0);
void board_clear();
void board_present_begin(bool capture = false,bool low_power_patch=false);
bool board_scene_rows_needed(int y,int h,const SceneDamageBounds& damage);
uint16_t *board_tile_buffer(int index);
void board_present_tile(int y, int h, uint16_t *pixels,
                        bool continuous = false, uint16_t *target = nullptr,
                        const SceneDamageBounds* damage = nullptr);
void board_present_end();
void board_present_patch(int x,int y,int width,int height,const uint16_t* pixels);
void board_brightness(int percent);
uint16_t *board_framebuffer();
extern i2c_master_bus_handle_t board_i2c;
esp_err_t board_read(uint8_t addr, uint8_t reg, void *out, size_t len);
esp_err_t board_write(uint8_t addr, uint8_t reg, uint8_t value);
void board_audio_init();
void board_audio_sleep(bool sleeping);
void power_init();
esp_pm_lock_handle_t power_render_lock();
cJSON *power_task_stats();
void board_volume(int percent);
bool board_audio_enqueue(const int16_t *mono, size_t samples);
void board_audio_clear();
void board_tone();
void board_sensor_tick();
void board_motion_stall_request();
void board_clock_write(int64_t epoch);

void board_touch_tick();
void board_touch_start();
bool board_touch_pop(int64_t cutoff,TouchHistory::Event& event);
cJSON *board_touch_stats();
cJSON *board_remote_input(uint32_t after);
void board_touch_reset_request();
void input_touch(bool down, int x, int y, bool simulated = false);

void input_motion(BlobMotion::V specific,BlobMotion::V gyro,bool simulated=false);

void remote_feedback(bool has_recording,bool active,const char* client,uint32_t seq,const char* kind);
