#include "ble_remote.h"
#include "crash_note.h"
#include "usb_device.h"
#include "ditto_behavior.h"
#include "esp_app_desc.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "json_storage.h"
#include "power_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "scout.h"
#include "activity.h"
#include "copy_probe.h"
#include "cpu_probe.h"
#include "pcm_base64.h"
#include "esp_private/esp_clk.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <sys/time.h>

State state;
SemaphoreHandle_t state_mutex, ui_mutex;
static nvs_handle_t settings;
static std::atomic<bool> settings_pending{false};
static char pending_timezone[96]{}; // guarded by state_mutex
static std::atomic<bool> wifi_desired{false}, scanning{false};
static std::atomic<int64_t> reconnect_at{0};
static std::atomic<int> reconnect_delay{1};
static std::atomic<int64_t> connection_deadline{0};
static std::atomic<int> auth_failures{0};
static std::atomic<bool> intentional_disconnect{false};
static std::atomic<int64_t> voice_contact_us{0};
using Avatar::clips;
static const char *apps[] = {"home",       "volume", "brightness", "wifi",
                             "microphone", "timer",  "system"};
State snapshot() {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  State s = state;
  xSemaphoreGive(state_mutex);
  return s;
}
void notify_state() {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.revision++;
  state.last_activity_us = esp_timer_get_time();
  state.sleeping = false;
  xSemaphoreGive(state_mutex);
}
// Both transports share the same identity and deduplication under the state lock.
void remote_feedback(bool has_recording,bool active,const char* client,uint32_t seq,const char* kind) {
  static char last_client[41]={};static uint32_t last_seq=0;
  const int64_t now=esp_timer_get_time();
  xSemaphoreTake(state_mutex,portMAX_DELAY);
  if(has_recording) {
    if(state.remote_dictating && !active)state.remote_audio_tail_until_us=now+6000000;
    state.remote_dictating=active;state.remote_dictation_until_us=active?now+600000:0;
    if(active)state.remote_audio_tail_until_us=0;
  }
  if(client && kind && seq) {
    if(strcmp(last_client,client)) {snprintf(last_client,sizeof(last_client),"%s",client);last_seq=0;}
    if(seq>last_seq) {
      last_seq=seq;
      if(!strcmp(kind,"end")) {state.remote_dictating=false;state.remote_dictation_until_us=0;state.remote_audio_tail_until_us=now+6000000;}
      if(!state.sleeping && !state.touch.down && !state.remote_dictating)state.remote_reaction_serial++;
    }
  }
  xSemaphoreGive(state_mutex);
}
static cJSON *status() {
  JsonStorage::External storage;
  const size_t internal_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  const size_t internal_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  State s = snapshot();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
  cJSON_AddNumberToObject(o, "reset_reason", esp_reset_reason());
  cJSON_AddStringToObject(o, "app", apps[(int)s.screen]);
  cJSON_AddNumberToObject(o, "raster0_ms", s.raster0_ms);
  cJSON_AddNumberToObject(o, "raster1_ms", s.raster1_ms);
  cJSON_AddNumberToObject(o, "volume", s.volume);
  cJSON_AddNumberToObject(o, "brightness", s.brightness);
  cJSON_AddBoolToObject(o, "muted", s.muted);
  cJSON_AddBoolToObject(o, "wifi_connected", s.wifi_connected);
  cJSON_AddBoolToObject(o, "wifi_connecting", s.wifi_connecting);
  cJSON_AddBoolToObject(o, "wifi_scanning", s.wifi_scanning);
  cJSON_AddBoolToObject(o, "wifi_scan_done", s.wifi_scan_done);
  cJSON_AddBoolToObject(o, "wifi_scan_failed", s.wifi_scan_failed);
  cJSON_AddStringToObject(o, "ssid", s.ssid);
  cJSON_AddStringToObject(o, "wifi_error", s.wifi_error);
  cJSON_AddBoolToObject(o, "voice_active", s.voice_active);
  cJSON_AddBoolToObject(o, "audio_stream", s.audio_stream);
  cJSON_AddStringToObject(o, "conversation", s.conversation);
  cJSON_AddStringToObject(o, "caption", s.caption);
  cJSON_AddStringToObject(o, "clip", clips[std::clamp(s.render_clip, 0, int(Avatar::ClipCount)-1)]);
  cJSON_AddNumberToObject(o,"remote_reaction_serial",s.remote_reaction_serial);
  cJSON_AddNumberToObject(o,"auto_hello_count",s.auto_hello_count);
  cJSON_AddNumberToObject(o,"auto_reaction_count",s.auto_reaction_count);
  cJSON_AddNumberToObject(o,"auto_hello_at_us",s.auto_hello_at_us);
  cJSON_AddBoolToObject(o,"emote_rig_active",s.emote_rig_active);
  cJSON_AddBoolToObject(o,"emote_reaction_priority",s.emote_reaction_priority);
  cJSON_AddNumberToObject(o,"emote_joint_motion",s.emote_joint_motion);
  cJSON_AddNumberToObject(o,"emote_seconds",s.emote_seconds);
  cJSON_AddBoolToObject(o,"remote_dictating",s.remote_dictating && esp_timer_get_time()<s.remote_dictation_until_us);
  cJSON_AddStringToObject(o, "requested_emote", clips[std::clamp(s.clip, 0, int(Avatar::ClipCount)-1)]);
  cJSON_AddStringToObject(o, "avatar", Avatar::Id);
  cJSON_AddNumberToObject(o, "avatar_triangles", Avatar::Triangles);
  cJSON_AddNumberToObject(o, "expression", s.expression);
  cJSON_AddNumberToObject(o, "effective_brightness", s.effective_brightness);
  cJSON_AddBoolToObject(o, "touch_available", s.touch_available);
  cJSON_AddBoolToObject(o, "touch_down", s.touch.down);
  cJSON_AddNumberToObject(o, "touch_x", s.touch.x);
  cJSON_AddNumberToObject(o, "touch_y", s.touch.y);
  cJSON_AddNumberToObject(o, "touch_presses", s.touch.press);
  cJSON_AddNumberToObject(o, "touch_contacts", s.touch_contacts);
  cJSON_AddNumberToObject(o, "touch_samples", s.touch_samples);
  cJSON_AddNumberToObject(o, "touch_errors", s.touch_errors);
  cJSON_AddNumberToObject(o, "touch_recoveries", s.touch_recoveries);
  cJSON_AddNumberToObject(o, "touch_retries", s.touch_retries);
  char touch_raw[16]; snprintf(touch_raw,sizeof(touch_raw),"%010llx",(unsigned long long)s.touch_invalid_report);
  cJSON_AddStringToObject(o,"touch_last_invalid_report",touch_raw);
  cJSON_AddBoolToObject(o,"imu_available",s.imu_available);
  cJSON_AddBoolToObject(o,"imu_configured",s.imu_configured);
  cJSON_AddNumberToObject(o,"imu_samples",s.imu_samples);
  cJSON_AddNumberToObject(o,"imu_errors",s.imu_errors);
  cJSON_AddNumberToObject(o,"imu_retries",s.imu_retries);
  cJSON_AddNumberToObject(o,"imu_timestamp",s.imu_timestamp);
  cJSON_AddNumberToObject(o,"imu_sample_age_ms",s.imu_last_sample_us?(esp_timer_get_time()-s.imu_last_sample_us)/1000.f:-1);
  cJSON_AddNumberToObject(o,"imu_stale_reads",s.imu_stale_reads);
  cJSON_AddNumberToObject(o,"imu_stalls",s.imu_stalls);
  cJSON_AddNumberToObject(o,"imu_recoveries",s.imu_recoveries);
  cJSON_AddNumberToObject(o,"imu_fault_ctrl1",s.imu_fault_ctrl1);
  cJSON_AddNumberToObject(o,"imu_fault_ctrl7",s.imu_fault_ctrl7);
  cJSON_AddBoolToObject(o,"motion_simulated",esp_timer_get_time()<s.motion_sim_until_us);
  cJSON_AddBoolToObject(o,"motion_active",s.motion_active);
  cJSON_AddNumberToObject(o,"idle_timeout_s",Activity::idle_timeout_us/1000000);
  cJSON_AddNumberToObject(o,"idle_for_s",(esp_timer_get_time()-s.last_activity_us)/1e6);
  cJSON_AddNumberToObject(o,"motion_wakes",s.motion_wakes);
  cJSON_AddNumberToObject(o,"touch_wakes",s.touch_wakes);
  cJSON_AddBoolToObject(o,"audio_sleeping",s.audio_sleeping);
  cJSON_AddBoolToObject(o,"render_parked",s.render_parked);
  cJSON_AddBoolToObject(o,"sleep_overlay_active",s.sleep_overlay_active);
  cJSON_AddNumberToObject(o,"sleep_overlay_frames",s.sleep_overlay_frames);
  cJSON_AddNumberToObject(o,"sleep_overlay_ms",s.sleep_overlay_ms);
  cJSON_AddBoolToObject(o,"power_saving",s.sleeping && s.audio_sleeping && s.render_parked);
  cJSON_AddNumberToObject(o,"cpu_mhz",esp_clk_cpu_freq()/1000000);
  cJSON_AddNumberToObject(o,"audio_power_errors",s.audio_power_errors);
  cJSON_AddNumberToObject(o,"audio_input_blocks",s.audio_input_blocks);
  cJSON_AddNumberToObject(o,"audio_output_blocks",s.audio_output_blocks);
  cJSON_AddNumberToObject(o,"physics_steps",s.physics_steps);
  cJSON_AddNumberToObject(o,"motion_age_ms",s.motion.time_us?(esp_timer_get_time()-s.motion.time_us)/1000.f:-1);
  auto vector=[&](const char *key,BlobMotion::V v){double data[]={v.x,v.y,v.z};cJSON_AddItemToObject(o,key,cJSON_CreateDoubleArray(data,3));};
  vector("motion_specific_g",s.motion.specific);vector("motion_gyro_rads",s.motion.gyro);
  vector("motion_gravity_g",s.motion.gravity);vector("motion_linear_g",s.motion.linear);
  vector("motion_shake_g",s.motion.shake);vector("motion_wave",s.motion_wave);
  vector("material_scale",s.material_scale);vector("material_memory",s.material_memory);
  vector("material_shear",s.material_shear);vector("material_support",s.material_support);vector("material_gravity",s.material_gravity);
  cJSON_AddNumberToObject(o,"material_volume",s.material_volume);
  vector("motion_gyro_bias",s.motion.gyro_bias);vector("motion_omega",s.motion.omega);vector("motion_alpha",s.motion.alpha);
  vector("motion_offset",{s.motion_x,s.motion_y,s.motion_z});
  vector("motion_bend",{s.motion_bend_x,s.motion_bend_y,s.motion_bend_z});
  vector("motion_spin",{s.motion_spin_x,s.motion_spin_y,s.motion_spin_z});
  cJSON_AddNumberToObject(o,"motion_depth",s.motion_depth);
  cJSON_AddNumberToObject(o, "physics_ms", s.physics_ms);
  cJSON_AddNumberToObject(o,"glow_mask_ms",s.glow_mask_ms);
  cJSON_AddNumberToObject(o,"glow_blur_ms",s.glow_blur_ms);
  cJSON_AddNumberToObject(o,"glow_palette_ms",s.glow_palette_ms);
  cJSON_AddNumberToObject(o, "physics_activity", s.physics_activity);
  cJSON_AddNumberToObject(o, "annoyance", s.annoyance);
  cJSON_AddNumberToObject(o, "slime_stretch", s.slime_stretch);
  cJSON_AddNumberToObject(o, "slime_press", s.slime_press);
  cJSON_AddNumberToObject(o, "face_x_eyes", s.face_x_eyes);
  vector("face_gaze",s.face_gaze);
  cJSON_AddStringToObject(o,"behavior",DittoBehavior::name(DittoBehavior::Mode(s.behavior_mode)));
  cJSON_AddNumberToObject(o,"behavior_contacts",s.behavior_contacts);
  cJSON_AddNumberToObject(o,"face_oval",s.face_oval);cJSON_AddNumberToObject(o,"face_melon",s.face_melon);
  cJSON_AddNumberToObject(o,"face_dizzy",s.face_dizzy);
  cJSON_AddNumberToObject(o,"face_sleep",s.face_sleep);cJSON_AddNumberToObject(o,"face_eye_open",s.face_eye_open);
  cJSON_AddNumberToObject(o,"face_eye_width",s.face_eye_width);
  cJSON_AddNumberToObject(o,"shake_damping",s.shake_damping);
  cJSON_AddBoolToObject(o,"shake_active",s.shake_active);cJSON_AddBoolToObject(o,"shake_recovering",s.shake_recovering);
  cJSON_AddNumberToObject(o, "face_side_eye", s.face_side_eye);
  cJSON_AddNumberToObject(o, "tap_depth", s.tap_depth);
  cJSON_AddNumberToObject(o, "tap_seen_press", s.tap_seen_press);
  cJSON_AddNumberToObject(o, "body_squash", s.body_squash);
  cJSON_AddNumberToObject(o, "tap_count", s.tap_count);
  cJSON_AddBoolToObject(o, "tap_hit", s.tap_hit);
  cJSON_AddNumberToObject(o, "tap_local_x", s.tap_local_x);
  cJSON_AddNumberToObject(o, "tap_local_y", s.tap_local_y);
  cJSON_AddNumberToObject(o, "tap_bounce_x", s.tap_bounce_x);
  cJSON_AddNumberToObject(o, "tap_bounce_y", s.tap_bounce_y);
  cJSON_AddNumberToObject(o, "tap_bounce_z", s.tap_bounce_z);
  cJSON_AddNumberToObject(o, "tap_ripple", s.tap_ripple);
  cJSON_AddNumberToObject(o, "anim_ms", s.anim_ms);
  cJSON_AddNumberToObject(o, "skin_ms", s.skin_ms);
  cJSON_AddNumberToObject(o, "bin_ms", s.bin_ms);
  cJSON_AddNumberToObject(o, "draw_ms", s.draw_ms);
  cJSON_AddNumberToObject(o, "compose_ms", s.compose_ms);
  cJSON_AddNumberToObject(o, "text_ms", s.text_ms);
  cJSON_AddNumberToObject(o, "upload_ms", s.upload_ms);
  cJSON_AddNumberToObject(o, "diff_ms", s.diff_ms);
  cJSON_AddNumberToObject(o, "copy_ms", s.copy_ms);
  cJSON_AddNumberToObject(o, "dma_wait_ms", s.dma_wait_ms);
  cJSON_AddNumberToObject(o, "submit_ms", s.submit_ms);
  cJSON_AddNumberToObject(o,"presented_frames",s.presented_frames);
  cJSON_AddNumberToObject(o,"te_timeouts",s.te_timeouts);
  cJSON_AddNumberToObject(o,"scan_deadline_misses",s.scan_deadline_misses);
  cJSON_AddNumberToObject(o,"te_hz",s.te_hz);
  cJSON_AddNumberToObject(o,"present_strips",s.present_strips);
  cJSON_AddNumberToObject(o,"present_unmerged_strips",s.present_unmerged_strips);
  cJSON_AddNumberToObject(o,"present_pixels",s.present_pixels);
  cJSON_AddNumberToObject(o,"present_unmerged_pixels",s.present_unmerged_pixels);
  cJSON_AddNumberToObject(o,"present_ms",s.present_ms);
  cJSON_AddNumberToObject(o,"te_wait_ms",s.te_wait_ms);
  cJSON_AddNumberToObject(o,"scan_margin_ms",s.scan_margin_ms);
  cJSON_AddNumberToObject(o, "ui_stack_free", s.ui_stack_free);
  cJSON_AddNumberToObject(o, "render_stack_free", s.render_stack_free);
  cJSON_AddNumberToObject(o, "control_stack_free",
                          uxTaskGetStackHighWaterMark(nullptr));
  for (const char *name : {"mic", "speaker"}) {
    auto task = xTaskGetHandle(name);
    char key[40];
    snprintf(key, sizeof(key), "%s_stack_free", name);
    cJSON_AddNumberToObject(o, key,
                            task ? uxTaskGetStackHighWaterMark(task) : 0);
  }
  cJSON_AddNumberToObject(o, "frame_ms", s.frame_ms);
  cJSON_AddNumberToObject(o, "frame_p95_ms", s.frame_p95_ms);
  cJSON_AddNumberToObject(o, "fps", s.fps);
  cJSON_AddNumberToObject(o, "frames", s.frames);
  cJSON_AddNumberToObject(o, "frames_within25", s.frames_within25);
  cJSON_AddNumberToObject(o, "missed_frames", s.missed_frames);
  cJSON_AddNumberToObject(o, "display_errors", s.display_errors);
  cJSON_AddNumberToObject(o, "audio_rx_errors", s.audio_rx_errors);
  cJSON_AddNumberToObject(o, "audio_tx_errors", s.audio_tx_errors);
  cJSON_AddNumberToObject(o, "audio_overflows", s.audio_overflows);
  cJSON_AddNumberToObject(o, "mic_rms", s.mic_rms);
  cJSON_AddNumberToObject(o,"mic_raw_rms",s.mic_raw_rms);
  cJSON_AddNumberToObject(o,"mic_right_rms",s.mic_right_rms);
  cJSON_AddNumberToObject(o,"mic_raw_peak",s.mic_raw_peak);
  cJSON_AddNumberToObject(o,"mic_peak",s.mic_peak);
  cJSON_AddBoolToObject(o,"mic_settling",s.mic_settling);
  cJSON_AddNumberToObject(o,"mic_dsp_max_us",s.mic_dsp_max_us);
  cJSON_AddNumberToObject(o,"usb_first_audio_ms",scout_usb_first_audio_ms());
  cJSON_AddNumberToObject(o,"usb_audio_starts",scout_usb_audio_starts());
  cJSON_AddBoolToObject(o,"usb_connected",scout_usb_connected());
  cJSON_AddBoolToObject(o,"bluetooth_connected",ble_remote_connected());
  cJSON_AddBoolToObject(o,"bluetooth_available",ble_remote_available());
  ble_remote_status(o);crash_note_status(o);
  cJSON_AddNumberToObject(o,"battery_percent",s.battery_percent);
  cJSON_AddBoolToObject(o,"battery_charging",s.battery_charging);
  cJSON_AddStringToObject(o,"battery_state",Battery::name(s.battery_mode));
  cJSON_AddBoolToObject(o,"battery_external_power",s.battery_external);
  cJSON_AddBoolToObject(o,"battery_present",s.battery_present);
  cJSON_AddBoolToObject(o,"battery_status_valid",s.battery_status_valid);
  cJSON_AddNumberToObject(o,"battery_status0",s.battery_status0);
  cJSON_AddNumberToObject(o,"battery_status1",s.battery_status1);
  cJSON_AddBoolToObject(o,"usb_recording",scout_usb_recording());
  cJSON_AddBoolToObject(o,"settings_pending",settings_pending.load());
  cJSON_AddNumberToObject(o,"usb_audio_drops",scout_usb_audio_drops());
  cJSON_AddNumberToObject(o,"usb_audio_packets",scout_usb_audio_packets());
  const char* usb_metrics[]={"usb_fifo_overwrites","usb_audio_bytes","usb_empty_packets","usb_short_packets","usb_long_packets","usb_packet_gap_max_us","usb_underrun_capture_age_us"};
  for(unsigned i=0;i<7;i++)cJSON_AddNumberToObject(o,usb_metrics[i],scout_usb_metric(i));
  cJSON_AddNumberToObject(o, "speaker_rms", s.speaker_rms);
  cJSON_AddNumberToObject(o, "battery_v", s.battery_v);
  cJSON_AddNumberToObject(
      o, "internal_free",
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(o,"internal_free_before_status",internal_before);
  cJSON_AddNumberToObject(o,"internal_largest_before_status",internal_largest);
  cJSON_AddNumberToObject(o,"internal_low_watermark",heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(o, "psram_free",
                          heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  cJSON_AddNumberToObject(o, "uptime_s", esp_timer_get_time() / 1e6);
  cJSON_AddNumberToObject(o, "epoch", time(nullptr));
  cJSON_AddStringToObject(o, "draft_ssid", s.wifi_draft_ssid);
  cJSON_AddNumberToObject(o, "password_length", strlen(s.wifi_password));
  cJSON_AddNumberToObject(o, "network_count", s.network_count);
  cJSON_AddNumberToObject(o, "edit_index", s.wifi_edit_index);
  cJSON_AddNumberToObject(o, "edit_count", s.wifi_edit_count);
  cJSON_AddNumberToObject(o, "edit_removed", s.wifi_edit_removed);
  cJSON_AddNumberToObject(o, "timer_id", s.timer_id);
  cJSON_AddBoolToObject(o, "sleeping", s.sleeping);
  cJSON_AddNumberToObject(
      o, "timer_remaining",
      std::max(0., (s.timer_end_us - esp_timer_get_time()) / 1e6));
  return o;
}
static void feedback(Screen screen, const char *text, int clip = Avatar::Yes,
                     bool sticky = false) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.screen = screen;
  snprintf(state.caption, sizeof(state.caption), "%s", text);
  state.clip = clip;
  state.clip_serial++;
  state.screen_until_us = sticky ? 0 : esp_timer_get_time() + 6000000;
  xSemaphoreGive(state_mutex);
  notify_state();
}
// Flash operations disable cache/USB service briefly. Apply controls immediately
// in RAM, then persist between recordings so a mute/volume change stays clean.
static void save() {settings_pending.store(true);}
static void flush_settings() {
  if(!settings_pending.exchange(false))return;
  State s = snapshot();
  char tz[96];xSemaphoreTake(state_mutex,portMAX_DELAY);
  memcpy(tz,pending_timezone,sizeof(tz));xSemaphoreGive(state_mutex);
  if(tz[0])nvs_set_str(settings,"tz",tz);
  nvs_set_i32(settings, "volume", s.volume);
  nvs_set_i32(settings, "brightness", s.brightness);
  nvs_set_u8(settings, "muted", s.muted);
  nvs_commit(settings);
}
static void connection_failed(const char *message) {
  wifi_desired = false;
  reconnect_at = connection_deadline = 0;
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.wifi_connected = state.wifi_connecting = false;
  snprintf(state.wifi_error, sizeof(state.wifi_error), "%s", message);
  if (state.screen == Screen::Wifi)
    snprintf(state.caption, sizeof(state.caption), "%s", message);
  state.revision++;
  xSemaphoreGive(state_mutex);
  event_message("wifi.failed", message);
}
static void network_event(void *, esp_event_base_t base, int32_t id,
                          void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (intentional_disconnect.exchange(false)) return;
    auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
    if (wifi_desired && event &&
        (event->reason == WIFI_REASON_AUTH_FAIL ||
         event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         event->reason == WIFI_REASON_HANDSHAKE_TIMEOUT) && ++auth_failures >= 2) {
      connection_failed("Check the Wi-Fi password");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.wifi_connected = false;
    state.wifi_connecting = wifi_desired;
    state.revision++;
    if (state.screen == Screen::Wifi)
      snprintf(state.caption, sizeof(state.caption), "%s",
               wifi_desired ? "Reconnecting..." : "Wi-Fi disconnected");
    xSemaphoreGive(state_mutex);
    if (wifi_desired) {
      if (!connection_deadline) connection_deadline = esp_timer_get_time() + 30000000;
      reconnect_at = esp_timer_get_time() + reconnect_delay * 1000000LL;
      reconnect_delay = std::min(reconnect_delay * 2, 30);
    }
    event_message("wifi.disconnected", "Connection unavailable");
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    // A cancelled/replaced attempt must not report a stale success.
    if (!wifi_desired) return;
    wifi_ap_record_t ap = {};
    State requested = snapshot();
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
        strncmp(reinterpret_cast<const char *>(ap.ssid), requested.ssid, 32)) return;
    reconnect_delay = 1;
    reconnect_at = connection_deadline = 0;
    auth_failures = 0;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.wifi_connected = true;
    state.wifi_connecting = false;
    state.wifi_error[0] = 0;
    state.revision++;
    if (state.screen == Screen::Wifi) {
      snprintf(state.caption, sizeof(state.caption), "You're connected");
      state.clip = Avatar::Happy;
      state.clip_serial++;
      state.screen_until_us = esp_timer_get_time() + 6000000;
    }
    xSemaphoreGive(state_mutex);
    event_message("wifi.connected", "Connected");
  }
}
static void scan_task(void *) {
  wifi_scan_config_t c = {};
  esp_err_t e = esp_wifi_scan_start(&c, true);
  uint16_t n = 16;
  wifi_ap_record_t *list = nullptr;
  if (e == ESP_OK) {
    list = (wifi_ap_record_t *)heap_caps_calloc(
        n, sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!list)
      e = ESP_ERR_NO_MEM;
    else
      e = esp_wifi_scan_get_ap_records(&n, list);
  }
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.wifi_scanning = false;
  state.wifi_scan_done = true;
  state.wifi_scan_failed = e != ESP_OK;
  state.network_count = e == ESP_OK ? std::min(4, (int)n) : 0;
  for (int k = 0; k < state.network_count; k++)
    snprintf(state.networks[k], 33, "%s", list[k].ssid);
  // A scan may finish after a subsequent spoken selection or navigation.
  // Keep that newer intent, while still publishing the network results.
  if (state.screen == Screen::Wifi && !state.wifi_draft_ssid[0])
    snprintf(state.caption, sizeof(state.caption), "%s",
             e != ESP_OK ? "Couldn't scan"
                         : (n ? "Which network?" : "No networks found"));
  state.revision++;
  xSemaphoreGive(state_mutex);
  cJSON *ev = cJSON_CreateObject();
  cJSON_AddStringToObject(ev, "event", "wifi.scan.result");
  cJSON_AddBoolToObject(ev, "ok", e == ESP_OK);
  cJSON *arr = cJSON_AddArrayToObject(ev, "networks");
  if (e == ESP_OK) {
    if (list) {
      for (int i = 0; i < n; i++) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", (char *)list[i].ssid);
        cJSON_AddNumberToObject(a, "rssi", list[i].rssi);
        cJSON_AddBoolToObject(a, "secured", list[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, a);
      }
    }
  }
  free(list);
  emit(ev);
  scanning = false;
  vTaskDelete(nullptr);
}
static const char *str(cJSON *o, const char *k) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) ? v->valuestring : nullptr;
}
static bool integer(cJSON *o, const char *k, int lo, int hi, int &out) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
  if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) ||
      v->valuedouble != floor(v->valuedouble) || v->valuedouble < lo ||
      v->valuedouble > hi)
    return false;
  out = v->valueint;
  return true;
}
static void reply(const char *id, bool ok, const char *error = nullptr,
                  cJSON *result = nullptr) {
  cJSON *r = cJSON_CreateObject();
  cJSON_AddStringToObject(r, "id", id ? id : "");
  cJSON_AddBoolToObject(r, "ok", ok);
  if (error)
    cJSON_AddStringToObject(r, "error", error);
  if (result)
    cJSON_AddItemToObject(r, "result", result);
  emit(r);
}
static constexpr const char *tool_names[] = {"system.status",
                                             "input.remote", "bluetooth.start",
                                             "system.tools",
                                             "display.brightness.set",
                                             "audio.volume.set",
                                             "audio.microphone.set",
                                             "wifi.scan",
                                             "wifi.draft.set",
                                             "wifi.password.edit",
                                             "wifi.connect",
                                             "wifi.disconnect",
                                             "wifi.forget",
                                             "clock.sync",
                                             "app.open",
                                             "app.home",
                                             "timer.start",
                                             "timer.cancel",
                                             "avatar.emote",
                                             "avatar.expression",
                                             "voice.state.set",
                                             "voice.keepalive",
                                             "audio.capture.start",
                                             "audio.capture.stop",
                                             "audio.play",
                                             "audio.clear",
                                             "audio.tone",
                                             "display.sleep",
                                             "display.wake",
                                             "system.screenshot",
                                             "system.reboot"};
void dispatch(cJSON *r) {
  const char *id = str(r, "id"), *tool = str(r, "tool");
  cJSON *a = cJSON_GetObjectItemCaseSensitive(r, "arguments");
  if (!tool || strlen(tool) > 64) {
    reply(id, false, "Missing tool name");
    return;
  }
  if (a && !cJSON_IsObject(a)) {
    reply(id, false, "arguments must be an object");
    return;
  }
  int value = 0;
  if (!strcmp(tool, "system.status")) {
    reply(id, true, nullptr, status());
    return;
  }
  if(!strcmp(tool,"power.log.info")){reply(id,true,nullptr,power_log_info());return;}
  if(!strcmp(tool,"power.log.read")){
    auto* seq=cJSON_GetObjectItemCaseSensitive(a,"after");
    if(seq && (!cJSON_IsNumber(seq)||!std::isfinite(seq->valuedouble)||seq->valuedouble<0||seq->valuedouble>4294967295.0||std::floor(seq->valuedouble)!=seq->valuedouble)){
      reply(id,false,"after must be an unsigned integer");return;
    }
    reply(id,true,nullptr,power_log_read(seq?uint32_t(seq->valuedouble):0,8));return;
  }
  if (!strcmp(tool,"bluetooth.start")) {
    ble_remote_init();reply(id,ble_remote_available(),ble_remote_available()?nullptr:"Bluetooth starting or unavailable; check status");return;
  }
  if (!strcmp(tool,"input.remote")) {
    auto item=cJSON_GetObjectItemCaseSensitive(a,"after");
    if(item && (!cJSON_IsNumber(item)||!std::isfinite(item->valuedouble)||item->valuedouble<0||item->valuedouble>4294967295.0||std::floor(item->valuedouble)!=item->valuedouble)) {
      reply(id,false,"after must be an unsigned integer");return;
    }
    auto recording=cJSON_GetObjectItemCaseSensitive(a,"dictating");
    auto feedback=cJSON_GetObjectItemCaseSensitive(a,"feedback");
    const char* client=feedback?str(feedback,"client"):nullptr;
    const char* kind=feedback?str(feedback,"kind"):nullptr;
    auto seq=feedback?cJSON_GetObjectItemCaseSensitive(feedback,"seq"):nullptr;
    if((recording && !cJSON_IsBool(recording)) || (feedback &&
       (!cJSON_IsObject(feedback)||!client||!*client||strlen(client)>40||!kind||
        (strcmp(kind,"end")&&strcmp(kind,"enter")&&strcmp(kind,"undo"))||
        !cJSON_IsNumber(seq)||!std::isfinite(seq->valuedouble)||seq->valuedouble<1||
        seq->valuedouble>4294967295.0||std::floor(seq->valuedouble)!=seq->valuedouble))) {
      reply(id,false,"Invalid remote feedback");return;
    }
    remote_feedback(recording!=nullptr,cJSON_IsTrue(recording),client,feedback?uint32_t(seq->valuedouble):0,kind);
    auto packet=board_remote_input(item?uint32_t(item->valuedouble):0);
    if(feedback)cJSON_AddNumberToObject(packet,"feedback_ack",seq->valuedouble);
    reply(id,true,nullptr,packet);return;
  }
  if (!strcmp(tool, "system.tools")) {
    cJSON *list = cJSON_CreateArray();
    for (auto name : tool_names)
      cJSON_AddItemToArray(list, cJSON_CreateString(name));
    reply(id, true, nullptr, list);
    return;
  }
  if (!strcmp(tool, "display.brightness.set") ||
      !strcmp(tool, "audio.volume.set")) {
    bool brightness = !strcmp(tool, "display.brightness.set");
    if (!integer(a, "percent", brightness ? 1 : 0, 100, value)) {
      reply(id, false,
            brightness ? "percent must be an integer from 1 to 100"
                       : "percent must be an integer from 0 to 100");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int previous_value = brightness ? state.brightness : state.volume;
    if (brightness)
      state.brightness = value;
    else
      state.volume = value;
    xSemaphoreGive(state_mutex);
    if (!brightness)
      board_volume(value);
    save();
    feedback(brightness ? Screen::Brightness : Screen::Volume,
             value == previous_value ? "Just right."
             : brightness ? (value > previous_value ? "That's brighter."
                                                    : "A little softer.")
             : value == 0 ? "Sound off."
             : value > previous_value ? "A little louder."
                                      : "A little quieter.");
  } else if (!strcmp(tool, "audio.microphone.set")) {
    cJSON *m = cJSON_GetObjectItemCaseSensitive(a, "muted");
    if (!cJSON_IsBool(m)) {
      reply(id, false, "muted must be boolean");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.muted = cJSON_IsTrue(m);
    xSemaphoreGive(state_mutex);
    save();
    feedback(Screen::Microphone,
             cJSON_IsTrue(m) ? "Microphone off" : "Microphone on",
             cJSON_IsTrue(m) ? Avatar::No : Avatar::Yes);
  } else if (!strcmp(tool, "wifi.scan")) {
    if (scanning) {
      reply(id, false, "Scan already running");
      return;
    }
    scanning = true;
    // An explicit scan starts a fresh network selection. A previously edited
    // draft must not hide the newly discovered network list.
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.wifi_draft_ssid[0] = 0;
    memset(state.wifi_password, 0, sizeof(state.wifi_password));
    state.wifi_edit_index = -1;
    state.wifi_edit_count = state.wifi_edit_removed = 0;
    state.network_count = 0;
    state.wifi_scanning = true;
    state.wifi_scan_done = false;
    state.wifi_scan_failed = false;
    state.wifi_error[0] = 0;
    xSemaphoreGive(state_mutex);
    feedback(Screen::Wifi, "Finding networks", Avatar::Idle, true);
    if (xTaskCreate(scan_task, "wifi_scan", 4096, nullptr, 3, nullptr) !=
        pdPASS) {
      scanning = false;
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      state.wifi_scanning = false;
      state.wifi_scan_done = true;
      state.wifi_scan_failed = true;
      strcpy(state.caption, "Couldn't scan");
      state.revision++;
      xSemaphoreGive(state_mutex);
      reply(id, false, "Cannot start scan");
      return;
    }
  } else if (!strcmp(tool, "wifi.draft.set")) {
    const char *ssid = str(a, "ssid"), *password = str(a, "password");
    if (!ssid || !strlen(ssid) || strlen(ssid) > 32 ||
        !password || strlen(password) > 63) {
      reply(id, false, "Invalid network name or password length");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    snprintf(state.wifi_draft_ssid, 33, "%s", ssid);
    snprintf(state.wifi_password, 65, "%s", password ? password : "");
    state.wifi_edit_index = -1;
    state.wifi_edit_count = state.wifi_edit_removed = 0;
    state.wifi_error[0] = 0;
    xSemaphoreGive(state_mutex);
    feedback(Screen::Wifi, password[0] ? "Review password" : "Password is empty", Avatar::Idle, true);
  } else if (!strcmp(tool, "wifi.password.edit")) {
    int index, remove;
    const char *insert = str(a, "insert");
    State s = snapshot();
    size_t len = strlen(s.wifi_password);
    if (!s.wifi_draft_ssid[0]) {
      reply(id, false, "Choose a network before editing its password");
      return;
    }
    if (!insert || !integer(a, "index", 0, len, index) ||
        !integer(a, "delete_count", 0, len - index, remove) ||
        len - remove + strlen(insert) > 63) {
      reply(id, false, "Invalid edit range or password length");
      return;
    }
    if(remove==0 && !insert[0]) {reply(id,true,nullptr,status());return;}
    char next[65];
    memcpy(next, s.wifi_password, index);
    strcpy(next + index, insert);
    strcpy(next + index + strlen(insert), s.wifi_password + index + remove);
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    strcpy(state.wifi_password, next);
    state.wifi_edit_index = index;
    state.wifi_edit_count = strlen(insert);
    state.wifi_edit_removed = remove;
    state.wifi_error[0] = 0;
    xSemaphoreGive(state_mutex);
    memset(next, 0, sizeof(next));
    feedback(Screen::Wifi, "Correction made", Avatar::Yes, true);
  } else if (!strcmp(tool, "wifi.connect")) {
    State s = snapshot();
    size_t n = strlen(s.wifi_password);
    if (!s.wifi_draft_ssid[0] || (n > 0 && n < 8)) {
      const char* message = s.wifi_draft_ssid[0] ? "Use at least 8 characters" : "Choose a network first";
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      snprintf(state.wifi_error,sizeof(state.wifi_error),"%s",message);
      xSemaphoreGive(state_mutex);
      feedback(Screen::Wifi,message,Avatar::No,true);
      reply(id, false, message);
      return;
    }
    wifi_desired = false;
    reconnect_at = connection_deadline = 0;
    intentional_disconnect = true;
    if (esp_wifi_disconnect() != ESP_OK) intentional_disconnect = false;
    wifi_config_t c = {};
    memcpy(c.sta.ssid, s.wifi_draft_ssid, strlen(s.wifi_draft_ssid));
    strcpy((char *)c.sta.password, s.wifi_password);
    c.sta.pmf_cfg.capable = true;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &c);
    memset(c.sta.password, 0, sizeof(c.sta.password));
    if (e != ESP_OK) {
      connection_failed("Could not configure Wi-Fi");
      reply(id, false, "Could not configure Wi-Fi");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.wifi_connected = false;
    state.wifi_connecting = true;
    state.wifi_error[0] = 0;
    snprintf(state.ssid, 33, "%s", s.wifi_draft_ssid);
    memset(state.wifi_password, 0, sizeof(state.wifi_password));
    state.wifi_edit_index=-1;state.wifi_edit_count=state.wifi_edit_removed=0;
    xSemaphoreGive(state_mutex);
    wifi_desired = true;
    auth_failures = 0;
    connection_deadline = esp_timer_get_time() + 30000000;
    reconnect_delay = 1;
    e = esp_wifi_connect();
    if (e != ESP_OK) {
      connection_failed("Could not begin connection");
      reply(id, false, "Could not begin connection");
      return;
    }
    feedback(Screen::Wifi, "Connecting...", Avatar::Idle, true);
  } else if (!strcmp(tool, "wifi.disconnect") || !strcmp(tool, "wifi.forget")) {
    wifi_desired = false;
    reconnect_at = connection_deadline = 0;
    intentional_disconnect = true;
    if (esp_wifi_disconnect() != ESP_OK) intentional_disconnect = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.wifi_connected = false;
    state.wifi_connecting = false;
    state.wifi_error[0] = 0;
    xSemaphoreGive(state_mutex);
    if (!strcmp(tool, "wifi.forget")) {
      wifi_config_t c = {};
      esp_wifi_set_config(WIFI_IF_STA, &c);
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      state.ssid[0] = 0;
      state.wifi_draft_ssid[0] = 0;
      memset(state.wifi_password, 0, sizeof(state.wifi_password));
      state.wifi_edit_index=-1;state.wifi_edit_count=state.wifi_edit_removed=0;
      xSemaphoreGive(state_mutex);
    }
    feedback(Screen::Wifi, "Wi-Fi disconnected", Avatar::Idle);
  } else if (!strcmp(tool, "clock.sync")) {
    cJSON *t = cJSON_GetObjectItemCaseSensitive(a, "epoch");
    const char *tz = str(a, "timezone");
    if (!cJSON_IsNumber(t) || !std::isfinite(t->valuedouble) ||
        t->valuedouble < 1577836800 || t->valuedouble > 4102444800 ||
        (tz && strlen(tz) > 95)) {
      reply(id, false, "Invalid time or timezone");
      return;
    }
    timeval tv = {};
    tv.tv_sec = (time_t)t->valuedouble;
    settimeofday(&tv, nullptr);
    board_clock_write(tv.tv_sec);
    if (tz) {
      setenv("TZ", tz, 1);
      tzset();
      xSemaphoreTake(state_mutex,portMAX_DELAY);
      snprintf(pending_timezone,sizeof(pending_timezone),"%s",tz);
      xSemaphoreGive(state_mutex);save();
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.bridge = true;
    state.revision++;
    xSemaphoreGive(state_mutex);
  } else if (!strcmp(tool, "app.open") || !strcmp(tool, "app.home")) {
    const char *name = !strcmp(tool, "app.home") ? "home" : str(a, "name");
    int app = -1;
    if (name)
      for (int i = 0; i < 7; i++)
        if (!strcmp(name, apps[i]))
          app = i;
    if (app < 0) {
      reply(id, false, "Unknown app");
      return;
    }
    feedback((Screen)app, app == 0 ? "Ready for you" : "What would you like?",
             Avatar::Idle, true);
  } else if (!strcmp(tool, "timer.start")) {
    if (!integer(a, "seconds", 1, 86400, value)) {
      reply(id, false, "seconds must be an integer from 1 to 86400");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.timer_end_us = esp_timer_get_time() + value * 1000000LL;
    state.timer_id++;
    xSemaphoreGive(state_mutex);
    feedback(Screen::Timer, "I'll keep time", Avatar::Happy, true);
  } else if (!strcmp(tool, "timer.cancel")) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.timer_end_us = 0;
    state.timer_id++;
    xSemaphoreGive(state_mutex);
    feedback(Screen::Timer, "Timer cancelled", Avatar::Idle);
  } else if (!strcmp(tool, "avatar.emote")) {
    const char *name = str(a, "name");
    int clip = -1;
    if (name)
      for (int i = 0; i < Avatar::ClipCount; i++)
        if (!strcmp(name, clips[i]))
          clip = i;
    if (clip < 0) {
      reply(id, false, "Unknown emote");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.clip = clip;
    state.clip_serial++;
    xSemaphoreGive(state_mutex);
    notify_state();
  } else if (!strcmp(tool, "avatar.expression")) {
    const char *name = str(a, "name");
    const char *expressions[] = {"neutral", "curious", "sad", "angry"};
    int index = -1;
    if (name)
      for (int i = 0; i < 4; i++)
        if (!strcmp(name, expressions[i]))
          index = i;
    if (index < 0) {
      reply(id, false, "Unknown expression");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.expression = index;
    xSemaphoreGive(state_mutex);
    notify_state();
  } else if (!strcmp(tool, "voice.keepalive")) {
    voice_contact_us = esp_timer_get_time();
    reply(id, true);
    return;
  } else if (!strcmp(tool, "voice.state.set")) {
    const char *mode = str(a, "state"), *caption = str(a, "caption");
    if (!mode ||
        (strcmp(mode, "idle") && strcmp(mode, "listening") &&
         strcmp(mode, "thinking") && strcmp(mode, "speaking") &&
         strcmp(mode, "offline")) ||
        (caption && strlen(caption) > 120)) {
      reply(id, false, "Invalid voice state or caption");
      return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    strcpy(state.conversation, mode);
    state.voice_active = strcmp(mode, "offline") && strcmp(mode, "idle");
    if (caption)
      snprintf(state.caption, 128, "%s", caption);
    xSemaphoreGive(state_mutex);
    voice_contact_us = esp_timer_get_time();
    notify_state();
  } else if (!strcmp(tool, "audio.capture.start") ||
             !strcmp(tool, "audio.capture.stop")) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.audio_stream = !strcmp(tool, "audio.capture.start");
    xSemaphoreGive(state_mutex);
    voice_contact_us = esp_timer_get_time();
  } else if (!strcmp(tool, "audio.play")) {
    const char *pcm = str(a, "pcm");
    int16_t samples[320];
    size_t n = 0;
    const size_t encoded_size=pcm?strlen(pcm):0;
    if (!pcm || encoded_size > 856 ||
        (!PcmBase64::decode((uint8_t*)samples,sizeof(samples),n,pcm,encoded_size) &&
         mbedtls_base64_decode((unsigned char *)samples, sizeof(samples), &n,
                              (const unsigned char *)pcm, encoded_size)) ||
        n % 2 || !board_audio_enqueue(samples, n / 2)) {
      reply(id, false, "Invalid PCM packet or playback queue full");
      return;
    }
    reply(id, true);
    return;
  } else if (!strcmp(tool, "audio.clear")) {
    board_audio_clear();
  } else if (!strcmp(tool, "audio.tone")) {
    board_tone();
  } else if (!strcmp(tool,"debug.usb_pattern")) {
    // Local diagnostics only. Bounded synthetic PCM never goes to the speaker.
    scout_usb_test_pattern(10000);reply(id,true);return;
  } else if (!strcmp(tool,"debug.motion.stall")) {
    // Local-only fault injection: stop the real IMU and let its watchdog recover.
    board_motion_stall_request();reply(id,true);return;
  } else if (!strcmp(tool,"debug.motion")) {
    // USB diagnostic, absent from AI skills. Screen-frame g and rad/s;
    // 250 ms lease restores real sensors automatically if a test disconnects.
    auto enabled=cJSON_GetObjectItemCaseSensitive(a,"enabled");
    if(cJSON_IsFalse(enabled)) {
      xSemaphoreTake(state_mutex,portMAX_DELAY);state.motion_sim_until_us=0;xSemaphoreGive(state_mutex);
      reply(id,true);return;
    }
    auto vector=[](cJSON *args,const char *key,float limit,BlobMotion::V &out) {
      auto v=cJSON_GetObjectItemCaseSensitive(args,key);if(!cJSON_IsArray(v)||cJSON_GetArraySize(v)!=3)return false;
      float values[3];for(int i=0;i<3;i++){auto n=cJSON_GetArrayItem(v,i);if(!cJSON_IsNumber(n)||!std::isfinite(n->valuedouble)||std::abs(n->valuedouble)>limit)return false;values[i]=n->valuedouble;}
      out={values[0],values[1],values[2]};return true;
    };
    BlobMotion::V acceleration,gyro;
    if(!vector(a,"acceleration",8,acceleration)||!vector(a,"gyro",9,gyro)) {
      reply(id,false,"Motion requires three acceleration values in g and three gyro values in rad/s");return;
    }
    input_motion(acceleration,gyro,true);reply(id,true);return;
  } else if (!strcmp(tool, "debug.touch.status")) {
    reply(id,true,nullptr,board_touch_stats());return;
  } else if (!strcmp(tool, "debug.touch.reset")) {
    board_touch_reset_request();
    reply(id,true); return;
  } else if (!strcmp(tool, "debug.touch")) {
    cJSON *down=cJSON_GetObjectItemCaseSensitive(a,"down");
    int x,y;
    if(!cJSON_IsBool(down) || !integer(a,"x",0,SCREEN_W-1,x) || !integer(a,"y",0,SCREEN_H-1,y)) {
      reply(id,false,"Touch requires down, x 0..409, y 0..501"); return;
    }
    // Local USB diagnostic only; deliberately absent from the AI skill catalog.
    input_touch(cJSON_IsTrue(down),x,y,true);
    reply(id,true); return;
  } else if (!strcmp(tool, "debug.power")) {
    reply(id,true,nullptr,power_task_stats());return;
  } else if (!strcmp(tool, "debug.cpu.start")) {
    if(!integer(a,"event",0,6,value)){reply(id,false,"event must be 0..6");return;}
    reply(id,true,nullptr,CpuProbe::start(value));return;
  } else if (!strcmp(tool, "debug.cpu.stop")) {
    reply(id,true,nullptr,CpuProbe::stop());return;
  } else if (!strcmp(tool, "debug.copy")) {
    reply(id,true,nullptr,CopyProbe::run());return;
  } else if (!strcmp(tool, "display.sleep")) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.sleeping = true;
    state.revision++;
    xSemaphoreGive(state_mutex);
  } else if (!strcmp(tool, "display.wake")) {
    notify_state();
  } else if (!strcmp(tool, "system.screenshot")) {
    ui_capture(id);
    return;
  } else if (!strcmp(tool, "system.reboot")) {
    reply(id, true);
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
    return;
  } else {
    reply(id, false, "Unknown tool");
    return;
  }
  reply(id, true, nullptr, status());
}
static State* housekeeping_snapshot=nullptr;
static void housekeeping(void *) {
  bool pressed = false;
  int64_t start = 0;
  while (true) {
    board_sensor_tick();
    int64_t now = esp_timer_get_time();
    bool down = gpio_get_level(GPIO_NUM_0) == 0;
    if (down && !pressed) {
      start = now;
      pressed = true;
    } else if (!down && pressed) {
      pressed = false;
      if (now - start > 700000) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        state.muted = !state.muted;
        bool muted=state.muted;
        xSemaphoreGive(state_mutex);
        save();
        feedback(Screen::Microphone,
                 muted ? "Microphone off" : "Microphone on");
      } else
        feedback(Screen::Home, "Ready for you", Avatar::Wave, true);
    }
    bool timer = false, voice_lost = false;
    uint32_t completed_timer_id = 0;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const bool fresh_motion=state.motion.valid && now-state.motion.time_us<=150000;
    if(!fresh_motion)state.motion_active=false;
    if (!state.sleeping && Activity::shouldSleep(now,state.last_activity_us,
        state.touch.down,state.motion_active,state.voice_active || state.audio_stream,fresh_motion)) {
      state.sleeping=true; state.revision++;
    }
    if ((state.voice_active || state.audio_stream) &&
        now - voice_contact_us.load() > 8000000) {
      state.voice_active = false;
      state.audio_stream = false;
      strcpy(state.conversation, "offline");
      strcpy(state.caption, "Voice disconnected");
      state.revision++;
      voice_lost = true;
    }
    if (state.screen_until_us && now > state.screen_until_us) {
      state.screen = Screen::Home;
      state.screen_until_us = 0;
      strcpy(state.caption, state.muted ? "Microphone off" : "Ready for you");
      state.revision++;
    }
    if (state.timer_end_us && now >= state.timer_end_us) {
      state.timer_end_us = 0;
      completed_timer_id = state.timer_id;
      timer = true;
      // Complete atomically with the deadline. Background work must not replace
      // a newer foreground action or interrupt a conversation caption.
      if (!state.voice_active &&
          (state.screen == Screen::Timer || state.screen == Screen::Home)) {
        strcpy(state.caption, "Time's up!");
        state.clip = Avatar::Celebrate;
        state.clip_serial++;
      }
      state.revision++;
      state.last_activity_us = now;
      state.sleeping = false;
    }
    xSemaphoreGive(state_mutex);
    if (voice_lost) {
      board_audio_clear();
      event_message("voice.disconnected", "Voice session timed out");
    }
    if (timer) {
      board_tone();
      cJSON *event = cJSON_CreateObject();
      cJSON_AddStringToObject(event, "event", "timer.finished");
      cJSON_AddStringToObject(event, "message", "Timer finished");
      cJSON_AddNumberToObject(event, "timer_id", completed_timer_id);
      emit(event);
    }
    if (wifi_desired && connection_deadline && now >= connection_deadline) {
      connection_failed("Couldn't connect. Try again");
      intentional_disconnect = true;
      if (esp_wifi_disconnect() != ESP_OK) intentional_disconnect = false;
    }
    ble_remote_tick();
    xSemaphoreTake(state_mutex,portMAX_DELAY);
    state.usb_connected=scout_usb_connected();state.bluetooth_connected=ble_remote_connected();
    *housekeeping_snapshot=state;
    xSemaphoreGive(state_mutex);
    const State& power=*housekeeping_snapshot;
    // Brief tail avoids repeatedly powering the ADC down between utterances.
    // It remains fully parked during real standby; no background USB recording.
    static int64_t mic_warm_until=0;
    if(scout_usb_recording()||ble_remote_recording())mic_warm_until=now+5000000;
    board_audio_sleep(power.sleeping && !power.voice_active && !power.audio_stream &&
                      !scout_usb_recording() && now>=mic_warm_until);
    if(!scout_usb_recording() && now>=mic_warm_until)flush_settings();
    power_log_tick(power,!scout_usb_recording() && now>=mic_warm_until && !power.audio_stream);
    // Keep an existing Wi-Fi connection (modem sleep handles the radio),
    // but don't spend battery retrying an absent network while resting.
    if (!power.sleeping && wifi_desired && reconnect_at && now >= reconnect_at) {
      reconnect_at = 0;
      if (esp_wifi_connect() != ESP_OK) reconnect_at = now + 1000000;
    }
    vTaskDelay(pdMS_TO_TICKS(power.sleeping?32:16));
  }
}
void services_init() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }
  ESP_ERROR_CHECK(nvs_open("scout", NVS_READWRITE, &settings));
  int32_t v;
  if (nvs_get_i32(settings, "volume", &v) == ESP_OK)
    state.volume = std::clamp((int)v, 0, 100);
  if (nvs_get_i32(settings, "brightness", &v) == ESP_OK)
    state.brightness = std::clamp((int)v, 1, 100);
  uint8_t m;
  if (nvs_get_u8(settings, "muted", &m) == ESP_OK)
    state.muted = m;
  char tz[96] = "UTC0";
  size_t n = sizeof(tz);
  nvs_get_str(settings, "tz", tz, &n);
  setenv("TZ", tz, 1);
  tzset();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&c));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             network_event, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
  wifi_config_t saved = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0]) {
    wifi_desired = true;
    connection_deadline = esp_timer_get_time() + 30000000;
    state.wifi_connecting = true;
    snprintf(state.ssid, 33, "%.32s", saved.sta.ssid);
    esp_wifi_connect();
  }
  memset(&saved, 0, sizeof(saved));
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(
      [](struct timeval *now) { board_clock_write(now->tv_sec); });
  esp_sntp_init();
  state.last_activity_us = esp_timer_get_time();
  power_log_init();
  board_touch_start();
  housekeeping_snapshot=(State*)heap_caps_malloc(sizeof(State),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  ESP_ERROR_CHECK(housekeeping_snapshot?ESP_OK:ESP_ERR_NO_MEM);
  xTaskCreate(housekeeping, "services", 4096, nullptr, 3, nullptr);
}
