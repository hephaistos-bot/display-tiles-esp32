#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ch422g.h"

static const char *TAG = "TILES_PROTOTYPE";

// Hardware Pins
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9

// RGB LCD Settings (800x480)
#define LCD_H_RES            800
#define LCD_V_RES            480
#define LCD_PIXEL_CLOCK_HZ   (12 * 1000 * 1000)

// LCD RGB Pinout (Waveshare ESP32-S3-Touch-LCD-5 Corrected)
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

void app_main(void) {
    ESP_LOGI(TAG, "Starting Minimal Screen Test (V8 Final)...");
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 1. I2C Init
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&i2c_bus_conf, &i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C Bus Init Failed!");
        return;
    }

    // 2. CH422G Init and Force Backlight
    ESP_LOGI(TAG, "Enabling CH422G Backlight (EXIO=0xFF)...");
    ch422g_init(i2c_bus);
    ch422g_set_config(0x05); // Enable both EXIO and OC

    // Explicit reset pulse (active LOW)
    ch422g_write_output(0xFE); // Bit 0 LOW
    vTaskDelay(pdMS_TO_TICKS(100));
    ch422g_write_output(0xFF); // Bit 0 HIGH, all others HIGH
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. RGB LCD Init
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

    if (esp_lcd_new_rgb_panel(&panel_conf, &lcd_panel) == ESP_OK) {
        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        ESP_LOGI(TAG, "LCD initialized.");
    } else {
        ESP_LOGE(TAG, "LCD Init Failed!");
    }

    // 4. Fill screen with RED and Maintain Backlight
    uint16_t *line_buf = heap_caps_malloc(LCD_H_RES * 20 * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (line_buf) {
        for (int i = 0; i < LCD_H_RES * 20; i++) line_buf[i] = 0xF800; // Red
    }

    ESP_LOGI(TAG, "Entering Main Loop...");
    while (1) {
        // Keep backlight alive
        ch422g_write_output(0xFF);
        ch422g_write_od(0x00);

        // Redraw RED pattern if buffer exists
        if (line_buf && lcd_panel) {
            for (int y = 0; y < LCD_V_RES; y += 20) {
                esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, LCD_H_RES, y + 20, line_buf);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
