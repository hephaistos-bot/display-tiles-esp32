#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ch422g.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "TileEngine.hpp"

static const char *TAG = "TILES_PROTOTYPE";

// --- Hardware Pins ---
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9
#define TP_INT_PIN           4

#define SD_MOSI_PIN          11
#define SD_SCK_PIN           12
#define SD_MISO_PIN          13

// --- CH422G EXIO Bit Mapping (Waveshare 5-inch Board) ---
#define CH422G_EXIO_LCD_RST  (1 << 0) // Bit 0: LCD Reset (Active LOW)
#define CH422G_EXIO_SD_CS    (1 << 4) // Bit 4: SD Card Chip Select (Active LOW)

// --- CH422G OC (Open Collector) Bit Mapping ---
// Based on working example 663ebdf:
// 0x2C (Reset: TP_RST=0, DISP=1)
// 0x2E (Normal: TP_RST=1, DISP=1)
#define CH422G_OC_TP_RST     (1 << 1) // Bit 1: Touch Reset
#define CH422G_OC_DISP       (1 << 2) // Bit 2: Display Backlight

// --- RGB LCD Settings (800x480) ---
#define LCD_H_RES            800
#define LCD_V_RES            480
#define LCD_PIXEL_CLOCK_HZ   (12 * 1000 * 1000) // 12MHz for stability

// --- LCD RGB Pinout ---
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

// Global Handles
i2c_master_bus_handle_t i2c_bus = NULL;
esp_lcd_panel_handle_t lcd_panel = NULL;
esp_lcd_touch_handle_t tp_handle = NULL;
SemaphoreHandle_t lvgl_mux = NULL;
sdmmc_card_t *card = NULL;

void hardware_init(void);
esp_err_t init_sd_card(void);
void lvgl_init_task(void *arg);

extern "C" void app_main(void) {
    // 1. Initialize core hardware (I2C, CH422G, LCD, Touch)
    hardware_init();

    // 2. Setup LVGL Synchronization and Task
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(lvgl_init_task, "LVGL", 1024 * 16, NULL, 5, NULL);
}

void hardware_init(void) {
    // Hardware needs time to settle after power-up
    ESP_LOGI(TAG, "Hardware stabilization (1.5s)...");
    vTaskDelay(pdMS_TO_TICKS(1500));

    // I2C Bus Initialization
    i2c_master_bus_config_t i2c_bus_conf = {};
    i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_conf.i2c_port = -1;
    i2c_bus_conf.sda_io_num = (gpio_num_t)I2C_SDA_PIN;
    i2c_bus_conf.scl_io_num = (gpio_num_t)I2C_SCL_PIN;
    i2c_bus_conf.glitch_ignore_cnt = 7;
    i2c_bus_conf.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_bus));

    // CH422G IO Expander Initialization
    ESP_LOGI(TAG, "Initializing CH422G...");
    ESP_ERROR_CHECK(ch422g_init(i2c_bus));
    // Enable both EXIO and OC outputs (0x01 | 0x04)
    ESP_ERROR_CHECK(ch422g_set_config(0x05));

    // Reset and Backlight Control
    ESP_LOGI(TAG, "Resetting Display and Touch (Commit 663ebdf Logic)...");

    // 1. Initial State for EXIO: LCD_RST Active (0), SD_CS Inactive (1)
    ch422g_write_output(CH422G_EXIO_SD_CS);

    // 2. Initial State for OC: 0x2C (TP_RST=0, DISP=1)
    ch422g_write_od(0x2C);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Release LCD Reset (EXIO Bit 0)
    ch422g_write_output(CH422G_EXIO_SD_CS | CH422G_EXIO_LCD_RST);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. GT911 Reset Sequence for Address 0x5D
    ESP_LOGI(TAG, "GT911 Reset Sequence...");
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TP_INT_PIN);
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)TP_INT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Release Touch Reset (OC Bit 1) -> 0x2E
    ch422g_write_od(0x2E);
    vTaskDelay(pdMS_TO_TICKS(200));

    // RGB LCD Initialization
    ESP_LOGI(TAG, "Initializing RGB LCD Panel...");
    esp_lcd_rgb_panel_config_t panel_conf = {};
    panel_conf.data_width = 16;
    panel_conf.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_conf.disp_gpio_num = (gpio_num_t)-1;
    panel_conf.pclk_gpio_num = (gpio_num_t)LCD_PIN_PCLK;
    panel_conf.vsync_gpio_num = (gpio_num_t)LCD_PIN_VSYNC;
    panel_conf.hsync_gpio_num = (gpio_num_t)LCD_PIN_HSYNC;
    panel_conf.de_gpio_num = (gpio_num_t)LCD_PIN_DE;

    int data_gpios[] = {
        LCD_PIN_B3, LCD_PIN_B4, LCD_PIN_B5, LCD_PIN_B6, LCD_PIN_B7,
        LCD_PIN_G2, LCD_PIN_G3, LCD_PIN_G4, LCD_PIN_G5, LCD_PIN_G6, LCD_PIN_G7,
        LCD_PIN_R3, LCD_PIN_R4, LCD_PIN_R5, LCD_PIN_R6, LCD_PIN_R7,
    };
    for (int i = 0; i < 16; i++) {
        panel_conf.data_gpio_nums[i] = (gpio_num_t)data_gpios[i];
    }

    panel_conf.timings.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    panel_conf.timings.h_res = LCD_H_RES;
    panel_conf.timings.v_res = LCD_V_RES;
    panel_conf.timings.hsync_back_porch = 8;
    panel_conf.timings.hsync_front_porch = 8;
    panel_conf.timings.hsync_pulse_width = 4;
    panel_conf.timings.vsync_back_porch = 8;
    panel_conf.timings.vsync_front_porch = 8;
    panel_conf.timings.vsync_pulse_width = 4;
    panel_conf.timings.flags.pclk_active_neg = 1;
    panel_conf.flags.fb_in_psram = 1;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_conf, &lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    ESP_LOGI(TAG, "LCD RGB panel initialized successfully.");

    // GT911 Initialization
    ESP_LOGI(TAG, "Initializing GT911 Touch Controller...");
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.dev_addr = 0x5D;
    tp_io_config.scl_speed_hz = 400000;

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = LCD_H_RES;
    tp_cfg.y_max = LCD_V_RES;
    tp_cfg.rst_gpio_num = (gpio_num_t)-1; // Managed via CH422G OC
    tp_cfg.int_gpio_num = (gpio_num_t)TP_INT_PIN;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy = 0;
    tp_cfg.flags.mirror_x = 0;
    tp_cfg.flags.mirror_y = 0;

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle));
    ESP_LOGI(TAG, "Touch controller initialized successfully.");

    // SD Card Initialization
    ESP_ERROR_CHECK(init_sd_card());
}

esp_err_t init_sd_card(void) {
    ESP_LOGI(TAG, "Initializing SD card (SPI)");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Use high speed SPI (20MHz+) as requested
    host.max_freq_khz = 20000;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SD_MOSI_PIN;
    bus_cfg.miso_io_num = SD_MISO_PIN;
    bus_cfg.sclk_io_num = SD_SCK_PIN;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)-1; // Managed via CH422G
    slot_config.host_id = (spi_host_device_t)host.slot;

    // Reliability: Toggling SD_CS (High -> Delay -> Low) prior to mounting
    ESP_LOGI(TAG, "Toggling SD_CS via CH422G...");
    ch422g_write_output(CH422G_EXIO_SD_CS | CH422G_EXIO_LCD_RST); // High (Inactive)
    vTaskDelay(pdMS_TO_TICKS(50));
    ch422g_write_output(CH422G_EXIO_LCD_RST); // Low (Active)
    vTaskDelay(pdMS_TO_TICKS(50));

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card VFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at /sdcard");
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

// LVGL Flush Callback
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(lcd_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

// LVGL Tick Callback
static uint32_t lvgl_tick_cb(void) {
    return esp_timer_get_time() / 1000;
}

// LVGL Touch Read Callback
static void lvgl_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    if (!tp) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t point_cnt = 0;
    esp_lcd_touch_point_data_t pt;

    if (esp_lcd_touch_read_data(tp) == ESP_OK) {
        if (esp_lcd_touch_get_data(tp, &pt, &point_cnt, 1) == ESP_OK && point_cnt > 0) {
            data->point.x = pt.x;
            data->point.y = pt.y;
            data->state = LV_INDEV_STATE_PRESSED;

            static uint32_t last_log = 0;
            if (lv_tick_get() - last_log > 2000) {
                ESP_LOGI(TAG, "Touch detected at (%d, %d)", (int)pt.x, (int)pt.y);
                last_log = lv_tick_get();
            }
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}


void lvgl_init_task(void *arg) {
    ESP_LOGI(TAG, "Starting LVGL task...");
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    // Allocate draw buffers in internal SRAM for performance
    uint32_t buffer_size = LCD_H_RES * 60;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers in internal SRAM");
        abort();
    }

    // Initialize LVGL Display
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_buffers(disp, buf1, buf2, buffer_size * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // Register Touch Input Device
    if (tp_handle) {
        lv_indev_t * indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, tp_handle);
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
    }

    // Initialize Tile Engine
    static TileEngine engine;
    engine.init();
    engine.setMapCenter(0.0, 0.0, 5); // Test Case: Equator

    ESP_LOGI(TAG, "LVGL initialization complete. Entering main loop...");

    // Periodic LVGL Task execution
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
