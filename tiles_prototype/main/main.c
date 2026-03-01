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

/**
 * Hardware Specifications: Waveshare ESP32-S3-Touch-LCD-5
 * - MCU: ESP32-S3 (16MB Flash, 8MB Octal PSRAM)
 * - Display: 5-inch 800x480 RGB LCD (ST7262)
 * - Touch Controller: GT911 (I2C address 0x5D/0x14)
 * - IO Expander: CH422G (Controls resets, backlight, and SD CS)
 */

static const char *TAG = "TILES_PROTOTYPE";

// --- Hardware Pins ---
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9
#define TP_INT_PIN           4  // Touch Interrupt (Active LOW/High based on config)

// --- CH422G EXIO Bit Mapping ---
#define CH422G_EXIO_LCD_RST  (1 << 0) // Bit 0: LCD Reset (Active LOW)
#define CH422G_EXIO_TP_RST   (1 << 1) // Bit 1: Touch Reset (Active LOW)
#define CH422G_EXIO_DISP     (1 << 2) // Bit 2: Display Backlight (Active LOW for ON)
#define CH422G_EXIO_SD_CS    (1 << 4) // Bit 4: SD Card Chip Select (Active LOW)

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
esp_lcd_touch_handle_t touch_handle = NULL;
SemaphoreHandle_t lvgl_mux = NULL;

void hardware_init(void);
void lvgl_init_task(void *arg);

void app_main(void) {
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
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_bus));

    // CH422G IO Expander Initialization
    ESP_LOGI(TAG, "Initializing CH422G...");
    ESP_ERROR_CHECK(ch422g_init(i2c_bus));
    ESP_ERROR_CHECK(ch422g_set_config(0x05)); // Enable both EXIO and OC outputs

    // --- GT911 Reset Sequence ---
    // Address selection: TP_INT high during reset -> 0x14, low -> 0x5D
    ESP_LOGI(TAG, "Resetting GT911 (Address 0x5D)...");
    gpio_config_t tp_int_conf = {
        .pin_bit_mask = (1ULL << TP_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&tp_int_conf);
    gpio_set_level(TP_INT_PIN, 0); // Pull INT low for address 0x5D

    // Reset via CH422G EXIO
    // Bit 0: LCD_RST, Bit 1: TP_RST, Bit 2: DISP (Active Low for ON)
    ch422g_write_output(CH422G_EXIO_LCD_RST | CH422G_EXIO_DISP); // LCD_RST high, TP_RST low, DISP high (OFF)
    vTaskDelay(pdMS_TO_TICKS(100));
    ch422g_write_output(CH422G_EXIO_LCD_RST | CH422G_EXIO_TP_RST | CH422G_EXIO_DISP); // TP_RST high
    vTaskDelay(pdMS_TO_TICKS(10));

    // Switch TP_INT back to input
    tp_int_conf.mode = GPIO_MODE_INPUT;
    tp_int_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&tp_int_conf);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Turn ON Backlight and release resets
    ESP_LOGI(TAG, "Enabling Backlight and LCD...");
    ch422g_write_output(CH422G_EXIO_LCD_RST | CH422G_EXIO_TP_RST); // Backlight bit is 0 (ON)
    ch422g_write_od(0x00);
    vTaskDelay(pdMS_TO_TICKS(500));

    // RGB LCD Initialization
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
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .hsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .flags.pclk_active_neg = 1,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_conf, &lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));

    // Touch Controller Initialization
    ESP_LOGI(TAG, "Initializing GT911 Touch Controller...");
    esp_lcd_panel_io_i2c_config_t tp_io_conf = {
        .dev_addr = 0x5D,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 0,
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz = 400000,
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_conf, &tp_io_handle));

    esp_lcd_touch_config_t tp_conf = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1, // Handled via CH422G
        .int_gpio_num = TP_INT_PIN,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_conf, &touch_handle));
}

// LVGL Flush Callback
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(lcd_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

// LVGL Input Callback: Reads touch data from the GT911 controller
void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    esp_lcd_touch_point_data_t point;
    uint8_t tp_num = 0;

    // Read data from the touch controller
    esp_lcd_touch_read_data(touch_handle);

    // Fetch the coordinates
    esp_err_t err = esp_lcd_touch_get_data(touch_handle, &point, &tp_num, 1);

    if (err == ESP_OK && tp_num > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
        // Periodic logging of touch coordinates
        ESP_LOGI(TAG, "Touch detected at: x=%d, y=%d", point.x, point.y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// LVGL Tick Callback
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

    // Register Input Device (Touch)
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);

    // Simple UI
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello World! (Touch Enabled)");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    ESP_LOGI(TAG, "LVGL initialization complete.");

    while (1) {
        xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
        uint32_t time_till_next = lv_timer_handler();
        xSemaphoreGiveRecursive(lvgl_mux);
        vTaskDelay(pdMS_TO_TICKS(LV_MAX(time_till_next, 1)));
    }
}
