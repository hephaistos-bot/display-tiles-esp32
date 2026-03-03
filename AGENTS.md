# 📑 Technical Reference: ESP32-S3-Touch-LCD-5

This document serves as a technical reference for developers working with the Waveshare ESP32-S3-Touch-LCD-5 using **ESP-IDF 6.1** and **LVGL 9.4** in C++.

## 1. Architectural Complexity: The "Missing" GPIOs

The ESP32-S3's 16-bit RGB interface consumes nearly all available GPIOs. To manage peripherals, Waveshare uses a **CH422G I/O Expander** on the I2C bus.

> [!IMPORTANT]
> **Critical Developer Note:** You cannot control the LCD Reset, Touch Reset, or SD Card Chip Select via standard `gpio_set_level()` commands. You must send I2C commands to the CH422G.

## 2. Bus Specifications & Speeds

### I2C Bus (Peripherals & Expansion)
* **Pins:** SDA: GPIO 8 | SCL: GPIO 9
* **Expected Speed:** 400 kHz (Fast Mode).
  * *Note:* While the CH422G supports 1 MHz, the GT911 and PCF85063 RTC are rated for 400 kHz. Running the bus at 400 kHz ensures stability across all shared devices.
* **Component Addresses:**
  * **CH422G (IO Expander):** 0x24 (Set Output), 0x20 (Set System)
  * **GT911 (Touch Controller):** 0x5D (Default) or 0x14
  * **PCF85063 (RTC):** 0x51

### SPI Bus (SD Card)
* **Interface Type:** SPI Mode (not SDMMC).
* **Pins:** MOSI: GPIO 11 | SCK: GPIO 12 | MISO: GPIO 13
* **Chip Select (CS):** Managed by CH422G EXIO4.
* **Expected Speed:** 20 MHz.
  * *Note:* In ESP-IDF, initialize the bus with `spi_bus_initialize`. Since CS is not a real GPIO, set the `spics_io_num` to -1 and manually toggle the CH422G EXIO4 bit before/after SPI transactions.

### RGB Interface (Display)
* **PCLK (Clock):** GPIO 7.
* **Expected Frequency:** ~21 MHz (standard for 800x480 @ 40-50 FPS).
* **Sync Pins:** VSYNC: GPIO 3 | HSYNC: GPIO 46 | DE: GPIO 5.
* **Data Pins:**
    * R[3-7]: 1, 2, 42, 41, 40
    * G[2-7]: 39, 0, 45, 48, 47, 21
    * B[3-7]: 14, 38, 18, 17, 10

## 3. Peripheral Control via CH422G

The CH422G manages the board's "housekeeping" signals. You must implement an I2C driver for this chip to enable other hardware.

| Pin | Label | Function | Requirement |
|---|---|---|---|
| EXIO1 | TP_RST | Touch Reset | Pulse LOW then HIGH to wake GT911. |
| EXIO2 | DISP | Backlight/Disp EN | Set HIGH to enable screen backlight. |
| EXIO3 | LCD_RST | LCD Reset | Pulse LOW then HIGH to initialize RGB panel. |
| EXIO4 | SD_CS | SD Chip Select | Pull LOW for SD operations; HIGH to release. |

## 4. Hardware Implementation Guide

### Screen Backlight
* **Control Type:** Binary (ON/OFF) via CH422G EXIO2.
* **Complexity:** Most Waveshare 5" models do not support hardware PWM dimming via a direct ESP32 GPIO. The AP3032KTR-G1 boost converter is typically toggled by the expander.

### Touch Screen (GT911)
* **Interrupt (IRQ):** GPIO 4.
* **Implementation:** Use the `esp_lcd_touch` component. Ensure you initialize the CH422G and pull EXIO1 (TP_RST) high before attempting to probe the touch IC.

### SD Card Mounting
For IDF 6.1, use the `sdspi_host` driver:
1. Initialize SPI bus on GPIO 11, 12, 13.
2. Command CH422G to pull EXIO4 LOW.
3. Call `esp_vfs_fat_sdspi_mount()`.
* **Complexity:** If you share the SPI bus, you must manage the CH422G CS state manually within the SPI transaction callbacks.

### I2C Conflicts
Because SDA/SCL are on GPIO 8/9, avoid using these pins for the RGB data bus. In the 5" version, these are specifically reserved for I2C to avoid interference with the high-speed display signals.

## 5. C++ Developer Reminders

* **Memory:** Always allocate LVGL frame buffers in PSRAM using `MALLOC_CAP_SPIRAM` due to the large memory footprint of 800x480 resolution.
* **LVGL 9.4:** Use the new `lv_display_set_flush_cb` with the `esp_lcd_panel_draw_bitmap` function.
* **Isolated IO:** The digital outputs (5-36V) are also mapped to the remaining EXIO pins on the CH422G. Check your specific board revision for the exact bit-mapping of the isolated output terminals.
