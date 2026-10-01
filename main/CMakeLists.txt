idf_component_register(SRCS "main.cpp" "board.cpp" "services.cpp" "ui.cpp" "power.cpp" "power_log.cpp" "usb_device.cpp" "ble_remote.cpp" "crash_note.cpp"
 INCLUDE_DIRS "."
 REQUIRES bt usb espressif__tinyusb a3d perfmon esp_app_format esp_pm esp_driver_usb_serial_jtag esp_timer esp_psram esp_partition esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_i2s esp_lcd esp_lcd_sh8601 esp_codec_dev esp_wifi esp_event esp_netif nvs_flash json mbedtls lwip
 EMBED_FILES "../assets/avatar.a3d")
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wno-missing-field-initializers)
set_source_files_properties(ui.cpp PROPERTIES COMPILE_OPTIONS "-mtext-section-literals")

# Opt in only on the S3; other targets and header-only consumers keep C++.
if(CONFIG_IDF_TARGET_ESP32S3)
  target_sources(${COMPONENT_LIB} PRIVATE "surface_span_s3.S")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE SCOUT_S3_SPAN=1 A3D_GOURAUD_IN_FLASH=1)
endif()

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}")

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
