#include "ble_remote.h"
#include "ble_control.h"
#include "mic_adpcm.h"
#include "scout.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs.h"
extern "C" {
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
void ble_store_config_init(void);
}
#include <cstring>
#include <atomic>
static const ble_uuid128_t service=BLE_UUID128_INIT(0x01,0x47,0x33,0xa1,0x94,0xf1,0x82,0xa9,0xf1,0x47,0x6b,0xa7,0x00,0x00,0xd7,0xd1);
static const ble_uuid128_t input=BLE_UUID128_INIT(0x01,0x47,0x33,0xa1,0x94,0xf1,0x82,0xa9,0xf1,0x47,0x6b,0xa7,0x01,0x00,0xd7,0xd1);
static const ble_uuid128_t control=BLE_UUID128_INIT(0x01,0x47,0x33,0xa1,0x94,0xf1,0x82,0xa9,0xf1,0x47,0x6b,0xa7,0x02,0x00,0xd7,0xd1);
static const ble_uuid128_t audio=BLE_UUID128_INIT(0x01,0x47,0x33,0xa1,0x94,0xf1,0x82,0xa9,0xf1,0x47,0x6b,0xa7,0x03,0x00,0xd7,0xd1);
static std::atomic<bool> audio_subscribed{false};
static std::atomic<int64_t> audio_until{0};
static std::atomic<uint32_t> audio_packets{0},audio_drops{0};
static uint16_t audio_handle;
static std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
static std::atomic<bool> subscribed{false},encrypted{false},available{false};
static std::atomic<uint32_t> notifications{0},notify_errors{0},controls{0},disconnects{0};
static std::atomic<int64_t> synced_at{0};
static std::atomic<int> security_status{-1};
static bool attempted=false;
static_assert(CONFIG_BT_NIMBLE_MAX_CCCDS>=3*CONFIG_BT_NIMBLE_MAX_BONDS,
 "Each bond needs Service Changed, input, and microphone subscription storage");
static int startup_result=0;static uint32_t startup_free=0;
static uint16_t input_handle;
static uint8_t address_type;
static TaskHandle_t host_task=nullptr;
// If radio bring-up crashes, the next boot keeps USB/display usable. Clear only
// after a stable 30 seconds, not merely after initialization returns.
RTC_NOINIT_ATTR static uint32_t startup_guard;
static constexpr uint32_t Guard=0xd1770b1e;
static void persist(bool value){nvs_handle_t n;if(nvs_open("ditto_ble",NVS_READWRITE,&n)==ESP_OK){nvs_set_u8(n,"enabled",value);nvs_commit(n);nvs_close(n);}}
static void advertise();
static int gap(ble_gap_event* event,void*){
 switch(event->type){
 case BLE_GAP_EVENT_CONNECT:
  if(event->connect.status==0){connection=event->connect.conn_handle;
   // Let the Mac initiate pairing when it reads/writes the protected attributes.
   // Early peripheral security requests can precede application discovery.
}
  else advertise();
  break;
 case BLE_GAP_EVENT_DISCONNECT:
  connection=BLE_HS_CONN_HANDLE_NONE;subscribed=false;audio_subscribed=false;audio_until=0;encrypted=false;disconnects++;advertise();break;
 case BLE_GAP_EVENT_SUBSCRIBE:if(event->subscribe.attr_handle==input_handle)subscribed=event->subscribe.cur_notify;
  if(event->subscribe.attr_handle==audio_handle)audio_subscribed=event->subscribe.cur_notify;
  break;
 case BLE_GAP_EVENT_ENC_CHANGE:{security_status=event->enc_change.status;ble_gap_conn_desc desc={};if(ble_gap_conn_find(event->enc_change.conn_handle,&desc)==0)encrypted=desc.sec_state.encrypted;break;}
 // Never silently remove a bond on an unauthenticated pairing request.
 case BLE_GAP_EVENT_REPEAT_PAIRING:return BLE_GAP_REPEAT_PAIRING_IGNORE;
 case BLE_GAP_EVENT_ADV_COMPLETE:advertise();break;
 }
 return 0;
}
static int access_input(uint16_t,uint16_t,ble_gatt_access_ctxt* context,void*){
 uint8_t packet[92];size_t n=board_remote_packet(packet,sizeof(packet));
 return n && os_mbuf_append(context->om,packet,n)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
static int access_control(uint16_t,uint16_t,ble_gatt_access_ctxt* context,void*){
 uint8_t bytes[BleControl::Size];uint16_t length=0;BleControl::Command cmd;
 if(context->op!=BLE_GATT_ACCESS_OP_WRITE_CHR || ble_hs_mbuf_to_flat(context->om,bytes,sizeof(bytes),&length)!=0 || !BleControl::decode(bytes,length,cmd))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
 audio_until=cmd.audio?esp_timer_get_time()+600000:0;
 remote_feedback(true,cmd.dictating,cmd.client,cmd.sequence,cmd.kind);controls++;
 return 0; // ATT write response acknowledges application, retries share USB IDs.
}
static ble_gatt_chr_def chars[4];
static ble_gatt_svc_def services[2];
static void advertise(){
 ble_hs_adv_fields f={};f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
 f.uuids128=const_cast<ble_uuid128_t*>(&service);f.num_uuids128=1;f.uuids128_is_complete=1;
 if(ble_gap_adv_set_fields(&f)!=0)return;
 ble_hs_adv_fields response={};response.name=(uint8_t*)"Ditto";response.name_len=5;response.name_is_complete=1;ble_gap_adv_rsp_set_fields(&response);
 ble_gap_adv_params params={};params.conn_mode=BLE_GAP_CONN_MODE_UND;params.disc_mode=BLE_GAP_DISC_MODE_GEN;params.itvl_min=160;params.itvl_max=240;
 ble_gap_adv_start(address_type,nullptr,BLE_HS_FOREVER,&params,gap,nullptr);
}
static void synced(){if(ble_hs_id_infer_auto(0,&address_type)==0){available=true;synced_at=esp_timer_get_time();ble_svc_gatt_changed(1,0xffff);advertise();}}
static void host(void*){nimble_port_run();vTaskDelete(nullptr);}
void ble_remote_boot(){
 if(__atomic_load_n(&startup_guard,__ATOMIC_ACQUIRE)==Guard || esp_reset_reason()==ESP_RST_PANIC || esp_reset_reason()==ESP_RST_INT_WDT || esp_reset_reason()==ESP_RST_TASK_WDT){startup_result=5;__atomic_store_n(&startup_guard,0,__ATOMIC_RELEASE);persist(false);event_message("bluetooth.recovery","Radio startup interrupted; USB recovery retained");return;}
 __atomic_store_n(&startup_guard,0,__ATOMIC_RELEASE);uint8_t enabled=0;nvs_handle_t n;
 if(nvs_open("ditto_ble",NVS_READONLY,&n)==ESP_OK){nvs_get_u8(n,"enabled",&enabled);nvs_close(n);}
 if(enabled)ble_remote_init();else startup_result=6;
}
void ble_remote_init(){
 if(attempted)return;
 startup_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 // Controller queues/task require internal RAM even with the host in PSRAM.
 if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<45000 || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<12000){startup_result=1;event_message("bluetooth.error","Insufficient radio headroom; USB remains available");return;}
 startup_result=0;attempted=true;persist(true);__atomic_store_n(&startup_guard,Guard,__ATOMIC_RELEASE);
 esp_err_t e=nimble_port_init();
 if(e!=ESP_OK){__atomic_store_n(&startup_guard,0,__ATOMIC_RELEASE);persist(false);event_message("bluetooth.error","Bluetooth could not start; USB remains available");return;}
 ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("Ditto");
 // Protect subscription itself so the central finishes encryption before its
 // first input notification. No separate encrypted read is needed at startup.
 chars[0].uuid=&input.u;chars[0].access_cb=access_input;chars[0].flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC;chars[0].val_handle=&input_handle;
 chars[1].uuid=&control.u;chars[1].access_cb=access_control;chars[1].flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC;
 chars[2].uuid=&audio.u;chars[2].access_cb=access_input;chars[2].flags=BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC;chars[2].val_handle=&audio_handle;
 services[0].type=BLE_GATT_SVC_TYPE_PRIMARY;services[0].uuid=&service.u;services[0].characteristics=chars;
 int rc=ble_gatts_count_cfg(services);if(!rc)rc=ble_gatts_add_svcs(services);
 if(rc){nimble_port_deinit();__atomic_store_n(&startup_guard,0,__ATOMIC_RELEASE);persist(false);event_message("bluetooth.error","Bluetooth service allocation failed");return;}
 ble_hs_cfg.sync_cb=synced;ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_sc=1;
 ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
 ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
 ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
 ble_store_config_init();ble_att_set_preferred_mtu(185);
 // Keep radio work off core 1: its USB isochronous packets have 1 ms deadlines.
 // A clean FIFO does not detect host-visible corruption when those deadlines slip.
 if(xTaskCreatePinnedToCore(host,"nimble_host",4096,nullptr,configMAX_PRIORITIES-4,&host_task,CONFIG_BT_NIMBLE_PINNED_TO_CORE)!=pdPASS){nimble_port_deinit();__atomic_store_n(&startup_guard,0,__ATOMIC_RELEASE);persist(false);event_message("bluetooth.error","Bluetooth task allocation failed");}
}
bool ble_remote_connected(){return connection!=BLE_HS_CONN_HANDLE_NONE && subscribed && encrypted;}
bool ble_remote_available(){return available;}
void ble_remote_status(cJSON* o){
 cJSON_AddNumberToObject(o,"bluetooth_startup_result",startup_result);cJSON_AddNumberToObject(o,"bluetooth_startup_free",startup_free);
 cJSON_AddBoolToObject(o,"bluetooth_linked",connection!=BLE_HS_CONN_HANDLE_NONE);cJSON_AddNumberToObject(o,"bluetooth_security_status",security_status);
 cJSON_AddBoolToObject(o,"bluetooth_encrypted",encrypted);cJSON_AddNumberToObject(o,"bluetooth_mtu",connection!=BLE_HS_CONN_HANDLE_NONE?ble_att_mtu(connection):0);
 cJSON_AddNumberToObject(o,"bluetooth_notifications",notifications);cJSON_AddNumberToObject(o,"bluetooth_notify_errors",notify_errors);
 cJSON_AddNumberToObject(o,"bluetooth_controls",controls);cJSON_AddNumberToObject(o,"bluetooth_disconnects",disconnects);
 cJSON_AddNumberToObject(o,"bluetooth_audio_packets",audio_packets);cJSON_AddNumberToObject(o,"bluetooth_audio_drops",audio_drops);
 cJSON_AddBoolToObject(o,"bluetooth_audio_active",ble_remote_recording());
 cJSON_AddBoolToObject(o,"bluetooth_audio_subscribed",audio_subscribed);cJSON_AddBoolToObject(o,"bluetooth_audio_requested",esp_timer_get_time()<audio_until);
 cJSON_AddNumberToObject(o,"bluetooth_stack_free",host_task?uxTaskGetStackHighWaterMark(host_task):0);
}
void ble_remote_tick(){
 static int64_t last=0;int64_t now=esp_timer_get_time();
 if(__atomic_load_n(&startup_guard,__ATOMIC_ACQUIRE)==Guard && synced_at && now-synced_at>30000000)__atomic_store_n(&startup_guard,0,__ATOMIC_RELEASE);
 if(now-last<32000 || !ble_remote_connected())return;
 last=now;
 uint16_t conn=connection;if(ble_att_mtu(conn)<95)return;
 uint8_t packet[92];size_t n=board_remote_packet(packet,sizeof(packet));if(!n)return;
 os_mbuf* data=ble_hs_mbuf_from_flat(packet,n);
 if(data && ble_gatts_notify_custom(conn,input_handle,data)==0)notifications++;else notify_errors++;
}

bool ble_remote_recording(){return ble_remote_connected() && audio_subscribed && esp_timer_get_time()<audio_until;}
void ble_remote_microphone(const int16_t* pcm,size_t count,bool muted){
 if(count!=MicADPCM::Samples||!ble_remote_recording())return;
 static uint32_t sequence=0;static int index=0;
 uint16_t conn=connection;if(ble_att_mtu(conn)<MicADPCM::Bytes+3)return;
 uint8_t packet[MicADPCM::Bytes];MicADPCM::encode(pcm,packet,++sequence,index,muted);
 auto* data=ble_hs_mbuf_from_flat(packet,sizeof(packet));
 if(data && ble_gatts_notify_custom(conn,audio_handle,data)==0)audio_packets++;else audio_drops++;
}
