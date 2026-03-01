#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ch422g.h"

static const char *TAG = "TILES_PROTOTYPE";

// Hardware Pins
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9

#define SD_MOSI_PIN          11
#define SD_CLK_PIN           12
#define SD_MISO_PIN          13

// RGB LCD Settings (800x480)
#define LCD_H_RES            800
#define LCD_V_RES            480
#define LCD_PIXEL_CLOCK_HZ   (18 * 1000 * 1000)

// LCD RGB Pinout
#define LCD_PIN_R3           1
#define LCD_PIN_R4           2
#define LCD_PIN_R5           42
#define LCD_PIN_R6           41
#define LCD_PIN_R7           40
#define LCD_PIN_G2           39
#define LCD_PIN_G3           0
#define LCD_PIN_G4           45
#define LCD_PIN_G5           48
#define LCD_PIN_G6           47
#define LCD_PIN_G7           21
#define LCD_PIN_B3           14
#define LCD_PIN_B4           38
#define LCD_PIN_B5           18
#define LCD_PIN_B6           17
#define LCD_PIN_B7           10
#define LCD_PIN_PCLK         7
#define LCD_PIN_HSYNC        46
#define LCD_PIN_VSYNC        3
#define LCD_PIN_DE           5

// GT911 TP_IRQ
#define TP_IRQ_PIN           4

// Global Handles
i2c_master_bus_handle_t i2c_bus = NULL;
esp_lcd_panel_handle_t lcd_panel = NULL;
SemaphoreHandle_t lvgl_mux = NULL;
char sd_status_msg[64] = "SD Card: Initializing...";

// Current CH422G EXIO state
static uint8_t ch422_exio_bits = 0;

// Forward Declarations
void hardware_init(void);
void lvgl_init_task(void *arg);
void sd_card_test(void);

void app_main(void) {
    hardware_init();
    // sd_card_test(); // De-risk SD card for now

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(lvgl_init_task, "LVGL", 1024 * 16, NULL, 5, NULL);
}

void hardware_init(void) {
    ESP_LOGI(TAG, "Hardware stabilization (1.5s)...");
    vTaskDelay(pdMS_TO_TICKS(1500));

    // I2C Init (for CH422G)
    ESP_LOGI(TAG, "Initializing I2C Master Bus...");
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_bus));

    // I2C Bus Scan
    ESP_LOGI(TAG, "Scanning I2C Bus for devices...");
    for (int addr = 0x01; addr < 0x7F; addr++) {
        esp_err_t res = i2c_master_probe(i2c_bus, addr, 100);
        if (res == ESP_OK) {
            ESP_LOGI(TAG, "Found device at I2C address 0x%02X", addr);
        }
    }

    // CH422G Init
    ESP_LOGI(TAG, "Initializing CH422G IO Expander...");
    ESP_ERROR_CHECK(ch422g_init(i2c_bus));
    ESP_ERROR_CHECK(ch422g_set_config(0x05)); // Enable IO/OC

    // CH422G Blinker Test (Diagnostic)
    ESP_LOGI(TAG, "Performing 5s backlight blinking test (toggling all EXIO/OC bits)...");
    for (int i = 0; i < 5; i++) {
        ESP_LOGI(TAG, "Blink iteration %d: ON (0xFF)", i);
        ch422g_write_output(0xFF);
        ch422g_write_od(0xFF);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "Blink iteration %d: OFF (0x00)", i);
        ch422g_write_output(0x00);
        ch422g_write_od(0x00);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // LCD Reset via CH422G
    ESP_LOGI(TAG, "Resetting LCD and enabling backlight...");
    // Final state: LCD_RST, TP_RST, DISP, EXIO3, SD_CS all HIGH
    ch422_exio_bits = 0xFF;
    ESP_ERROR_CHECK(ch422g_write_output(ch422_exio_bits));
    vTaskDelay(pdMS_TO_TICKS(500)); // Longer delay for stabilization

    // RGB LCD Init
    ESP_LOGI(TAG, "Initializing RGB LCD Panel...");
    esp_lcd_rgb_panel_config_t panel_conf = {
        .data_width = 16,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = -1,
        .pclk_gpio_num = LCD_PIN_PCLK,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .data_gpio_nums = {
            LCD_PIN_B3, LCD_PIN_B4, LCD_PIN_B5, LCD_PIN_B6, LCD_PIN_B7,
            LCD_PIN_G2, LCD_PIN_G3, LCD_PIN_G4, LCD_PIN_G5, LCD_PIN_G6, LCD_PIN_G7,
            LCD_PIN_R3, LCD_PIN_R4, LCD_PIN_R5, LCD_PIN_R6, LCD_PIN_R7,
        },
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = 40,
            .hsync_front_porch = 48,
            .hsync_pulse_width = 40,
            .vsync_back_porch = 32,
            .vsync_front_porch = 13,
            .vsync_pulse_width = 23,
            .flags.pclk_active_neg = 1,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_conf, &lcd_panel));
    ESP_LOGI(TAG, "LCD RGB panel created. Resetting and initializing...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    ESP_LOGI(TAG, "LCD RGB panel initialized.");

    // Simple RED Screen Fill Test
    ESP_LOGI(TAG, "Performing hardware color fill test (RED)...");
    uint16_t *test_buf = (uint16_t *)heap_caps_malloc(LCD_H_RES * 40 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (test_buf) {
        for (int i = 0; i < LCD_H_RES * 40; i++) {
            test_buf[i] = 0xF800; // Red in RGB565
        }
        for (int y = 0; y < LCD_V_RES; y += 40) {
            esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, LCD_H_RES, y + 40, test_buf);
        }
        heap_caps_free(test_buf);
        ESP_LOGI(TAG, "Hardware color fill test complete.");
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for color fill test.");
    }
}

void sd_card_test(void) {
    ESP_LOGI(TAG, "Initializing SD card via SPI and CH422G CS");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = -1; // Manual control
    slot_config.host_id = host.slot;

    // Ensure SD_CS is high before starting (inactive)
    ch422_exio_bits |= CH422G_PIN_SD_CS;
    ch422g_write_output(ch422_exio_bits);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Manually pull SD_CS low (active)
    ch422_exio_bits &= ~CH422G_PIN_SD_CS;
    ch422g_write_output(ch422_exio_bits);

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card. Error: %s", esp_err_to_name(ret));
        snprintf(sd_status_msg, sizeof(sd_status_msg), "SD Card: Mount Failed (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD Card mounted successfully!");
        sdmmc_card_print_info(stdout, card);
        snprintf(sd_status_msg, sizeof(sd_status_msg), "SD Card: OK (%lld MB)", (long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024));
    }
}

void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(lcd_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}


static uint32_t lvgl_tick_cb(void) {
    return esp_timer_get_time() / 1000;
}

void lvgl_init_task(void *arg) {
    ESP_LOGI(TAG, "Starting LVGL task...");
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    uint32_t buffer_size = LCD_H_RES * 40;
    lv_color_t *buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_buffers(disp, buf1, buf2, buffer_size * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello World from ESP32-S3!");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sd_label = lv_label_create(lv_screen_active());
    lv_label_set_text(sd_label, sd_status_msg);
    lv_obj_align(sd_label, LV_ALIGN_CENTER, 0, 20);

    ESP_LOGI(TAG, "LVGL initialization complete. Entering main loop...");

    while (1) {
        xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
        uint32_t time_till_next = lv_timer_handler();
        xSemaphoreGiveRecursive(lvgl_mux);

        if (time_till_next < 1) {
            time_till_next = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}
