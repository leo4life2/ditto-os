idf_component_register(SRCS "esp_lcd_sh8601.c" INCLUDE_DIRS "include" REQUIRES esp_lcd esp_driver_gpio)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP_LCD_SH8601_VER_MAJOR=2 ESP_LCD_SH8601_VER_MINOR=0 ESP_LCD_SH8601_VER_PATCH=0)
