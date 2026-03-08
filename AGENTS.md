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
  * **CH422G (IO Expander):**
    * `0x24`: System Configuration (Read/Write)
    * `0x26`: Read EXIO Status (Special Read Address)
    * `0x27`: EXIO Byte-Write (Bidirectional I/O)
    * `0x38`: OC (Open-Collector) Byte-Write (Output-only)
  * **GT911 (Touch Controller):** 0x5D (Default) or 0x14
  * **PCF85063 (RTC):** 0x51

### SPI Bus (SD Card)
* **Interface Type:** SPI Mode (not SDMMC).
* **Pins:** MOSI: GPIO 11 | SCK: GPIO 12 | MISO: GPIO 13
* **Chip Select (CS):** Managed by CH422G OC3 (Active Low).
* **Expected Speed:** 40 MHz.
  * *Note:* In ESP-IDF, initialize the bus with `spi_bus_initialize`. Since CS is not a real GPIO, set the `spics_io_num` to -1 and manually toggle the CH422G OC3 bit before/after SPI transactions.

### RGB Interface (Display)
* **PCLK (Clock):** GPIO 7.
* **Expected Frequency:** ~12-21 MHz (standard for 800x480 @ 40-50 FPS).
* **Sync Pins:** VSYNC: GPIO 3 | HSYNC: GPIO 46 | DE: GPIO 5.
* **Data Pins:**
    * R[3-7]: 1, 2, 42, 41, 40
    * G[2-7]: 39, 0, 45, 48, 47, 21
    * B[3-7]: 14, 38, 18, 17, 10

## 3. Peripheral Control via CH422G

The CH422G manages the board's "housekeeping" signals. You must implement an I2C driver for this chip to enable other hardware.

### OC Register (Address 0x38) - Output Only
Used for critical system and display signals.

| Bit | Pin | Function | Requirement |
|---|---|---|---|
| 0 | OC0 | TP_RST | Touch Reset. Pulse LOW then HIGH to wake GT911. |
| 1 | OC1 | DISP | Backlight/Disp EN. Set HIGH to enable screen backlight. |
| 2 | OC2 | LCD_RST | LCD Reset. Pulse LOW then HIGH to initialize RGB panel. |
| 3 | OC3 | SD_CS | SD Chip Select. Pull LOW for SD operations; HIGH to release. |

### EXIO Register (Address 0x27) - Bidirectional
Used for isolated digital I/O.

| Bit | Pin | Function | Requirement |
|---|---|---|---|
| 0 | IO0 | DI0 | Digital Input 0. |
| 5 | IO5 | DI1 | Digital Input 1. |
| 6 | IO6 | DO0 | Digital Output 0 (Isolated). |
| 7 | IO7 | DO1 | Digital Output 1 (Isolated). |

## 4. Hardware Implementation Guide

### Isolated I/O (DI / DO)
The board features optically isolated inputs and open-drain outputs accessible via the terminal block.

* **Digital Inputs (DI0, DI1):** Mapped to **IO0** and **IO5** of the EXIO register. These support 5V–36V signals.
* **Digital Outputs (DO0, DO1):** Mapped to **IO6** and **IO7** of the EXIO register.
  * **Software Control:** Controlled via I2C address **0x27**. Writing a `1` to the corresponding bit enables the output (sinks current to GND).

### Screen Backlight
* **Control Type:** Binary (ON/OFF) via CH422G OC1.

### Touch Screen (GT911)
* **Interrupt (IRQ):** GPIO 4.
* **Implementation:** Use the `esp_lcd_touch` component. Ensure you initialize the CH422G and pull OC0 (TP_RST) high before attempting to probe the touch IC.

### SD Card Mounting
For IDF 6.1, use the `sdspi_host` driver:
1. Initialize SPI bus on GPIO 11, 12, 13.
2. **Wake-up Sequence:** Send 100 dummy clocks on SCK with CS High before mounting.
3. Command CH422G to pull OC3 (SD_CS) LOW.
4. Call `esp_vfs_fat_sdspi_mount()`.

### Optimized JPEG Decoding
The ESP32-S3 supports hardware-accelerated JPEG decoding via SIMD instructions. Use the `esp_new_jpeg` component for maximum performance when rendering tiles.

## 5. C++ Developer Reminders

* **Memory:** Always allocate LVGL frame buffers and large image buffers in PSRAM using `MALLOC_CAP_SPIRAM`.
* **I2C Conflicts:** SDA/SCL are on GPIO 8/9. Avoid using these for RGB data.
* **CH422G Communication:** The chip requires the system configuration command (address `0x24`) to be sent once with value `0x05` to enable IO and OC operations.
