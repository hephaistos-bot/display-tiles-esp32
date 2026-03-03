#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ch422g.h"

static const char *TAG = "HELLO_WORLD_LCD";

// --- Hardware Pins ---
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9

// --- CH422G EXIO Bit Mapping (Waveshare 5-inch Board) ---
#define CH422G_EXIO_LCD_RST  (1 << 0) // Bit 0: LCD Reset (Active LOW)
#define CH422G_EXIO_TP_RST   (1 << 1) // Bit 1: Touch Reset (Active LOW)
#define CH422G_EXIO_DISP     (1 << 2) // Bit 2: Display Backlight (Active LOW for ON)

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
SemaphoreHandle_t lvgl_mux = NULL;

void hardware_init(void);
void lvgl_init_task(void *arg);

extern "C" void app_main(void) {
    // 1. Initialize core hardware (I2C, CH422G, LCD)
    hardware_init();

    // 2. Setup LVGL Synchronization and Task
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(lvgl_init_task, "LVGL", 1024 * 16, NULL, 5, NULL);
}

void hardware_init(void) {
    // Hardware needs time to settle after power-up
    ESP_LOGI(TAG, "Hardware stabilization (2.5s)...");
    vTaskDelay(pdMS_TO_TICKS(2500));

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
    ESP_ERROR_CHECK(ch422g_set_config(0x05)); // Enable both EXIO and OC outputs

    // Reset LCD and Turn on Backlight via CH422G
    // EXIO Bit 0: LCD_RST, Bit 2: DISP (Active LOW for ON)
    ESP_LOGI(TAG, "Resetting LCD and turning on backlight...");
    ESP_ERROR_CHECK(ch422g_write_output(0x00)); // All bits LOW: LCD_RST active, DISP active (ON)
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(ch422g_write_output(CH422G_EXIO_LCD_RST)); // Release LCD_RST, DISP still active
    vTaskDelay(pdMS_TO_TICKS(100));

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

void lvgl_init_task(void *arg) {
    ESP_LOGI(TAG, "Starting LVGL task...");
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    // Allocate draw buffers in PSRAM
    uint32_t buffer_size = LCD_H_RES * 40;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        abort();
    }

    // Initialize LVGL Display
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_buffers(disp, buf1, buf2, buffer_size * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // Create a screen with white background
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    // Create "Hello world" label centered in the screen
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello world");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);

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
