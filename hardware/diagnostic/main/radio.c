#include "probe.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
static SemaphoreHandle_t scan_ready,scan_finished;
static volatile unsigned ble_packets;
static volatile bool repeat_wifi;
static volatile unsigned wifi_cycles,wifi_restart_errors;
static wifi_scan_config_t passive_scan={.scan_type=WIFI_SCAN_TYPE_PASSIVE};
static void wifi_done(void *arg,esp_event_base_t base,int32_t id,void *data){if(repeat_wifi){wifi_cycles++;esp_wifi_clear_ap_list();if(esp_wifi_scan_start(&passive_scan,false)!=ESP_OK)wifi_restart_errors++;}}
static void gap_cb(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *p){
 if(event==ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT)xSemaphoreGive(scan_ready);
 if(event==ESP_GAP_BLE_SCAN_RESULT_EVT){if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT)ble_packets++;if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT)xSemaphoreGive(scan_finished);}
}
void probe_radios(void){
 esp_err_t e=nvs_flash_init();if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){TRY(nvs_flash_erase());e=nvs_flash_init();}TRY(e);
 TRY(esp_netif_init());TRY(esp_event_loop_create_default());esp_netif_create_default_wifi_sta();
 wifi_init_config_t w=WIFI_INIT_CONFIG_DEFAULT();TRY(esp_wifi_init(&w));TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));TRY(esp_wifi_set_mode(WIFI_MODE_STA));TRY(esp_wifi_start());
 probe_heaps("wifi_started");
 wifi_scan_config_t scan={.scan_type=WIFI_SCAN_TYPE_PASSIVE};
 int64_t t=esp_timer_get_time();TRY(esp_wifi_scan_start(&scan,false));probe_display_load(120,"audio_and_wifi_passive_scan");vTaskDelay(pdMS_TO_TICKS(3000));uint16_t count=0;e=esp_wifi_scan_get_ap_num(&count);
 RESULT("{\"test\":\"wifi_passive_scan\",\"result\":\"%s\",\"access_point_count\":%u,\"observed_elapsed_us\":%"PRId64",\"joined_network\":false}",esp_err_to_name(e),count,esp_timer_get_time()-t);esp_wifi_clear_ap_list();
 // Start BLE while Wi-Fi remains initialized, preserving the realistic combined heap cost.
 esp_bt_controller_config_t bt=BT_CONTROLLER_INIT_CONFIG_DEFAULT();TRY(esp_bt_controller_init(&bt));TRY(esp_bt_controller_enable(ESP_BT_MODE_BLE));TRY(esp_bluedroid_init());TRY(esp_bluedroid_enable());
 scan_ready=xSemaphoreCreateBinary();scan_finished=xSemaphoreCreateBinary();TRY(esp_ble_gap_register_callback(gap_cb));
 esp_ble_scan_params_t p={.scan_type=BLE_SCAN_TYPE_PASSIVE,.own_addr_type=BLE_ADDR_TYPE_PUBLIC,.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL,.scan_interval=0x50,.scan_window=0x30,.scan_duplicate=BLE_SCAN_DUPLICATE_DISABLE};TRY(esp_ble_gap_set_scan_params(&p));
 if(xSemaphoreTake(scan_ready,pdMS_TO_TICKS(3000))!=pdTRUE){RESULT("{\"test\":\"ble\",\"error\":\"configuration_timeout\"}");return;}
 probe_heaps("wifi_ble_audio_display");TRY(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,wifi_done,NULL));repeat_wifi=true;TRY(esp_ble_gap_start_scanning(12));TRY(esp_wifi_scan_start(&passive_scan,false));probe_display_staged(300,"audio_wifi_ble_passive_scan");
 bool complete=xSemaphoreTake(scan_finished,pdMS_TO_TICKS(7000))==pdTRUE;
 RESULT("{\"test\":\"ble_passive_scan\",\"duration_seconds\":12,\"advertisement_reports\":%u,\"completed\":%s,\"identities_retained\":false}",ble_packets,complete?"true":"false");
 repeat_wifi=false;esp_event_handler_unregister(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,wifi_done);
 RESULT("{\"test\":\"wifi_repeated_scan\",\"completed_cycles\":%u,\"restart_errors\":%u}",wifi_cycles,wifi_restart_errors);
 if(!complete)esp_ble_gap_stop_scanning();
 esp_bluedroid_disable();esp_bluedroid_deinit();esp_bt_controller_disable();esp_bt_controller_deinit();esp_wifi_scan_stop();esp_wifi_clear_ap_list();esp_wifi_stop();esp_wifi_deinit();
 probe_heaps("radios_stopped");
}
