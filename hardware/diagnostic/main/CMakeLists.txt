idf_component_register(SRCS "main.c" "sensors.c" "display.c" "audio.c" "radio.c" "finish.c"
 INCLUDE_DIRS "."
 REQUIRES esp_driver_usb_serial_jtag esp_timer esp_psram spi_flash esp_partition esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_i2s esp_driver_tsens esp_lcd esp_lcd_sh8601 esp_codec_dev esp_wifi esp_event esp_netif nvs_flash fatfs sdmmc bt)

option(PROBE_FINISH_ONLY "Run sleep checks and leave calibration grid" ON)
if(PROBE_FINISH_ONLY)
 target_compile_definitions(${COMPONENT_LIB} PRIVATE PROBE_FINISH_ONLY=1)
endif()
