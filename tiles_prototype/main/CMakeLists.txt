idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd lvgl esp_lcd_touch_gt911
                    PRIV_REQUIRES ch422g esp_driver_i2c esp_driver_spi esp_driver_gpio fatfs sdmmc esp_timer)
