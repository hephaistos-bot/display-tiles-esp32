#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief CH422G I/O Expander Controller class for Waveshare ESP32-S3-Touch-LCD-5.
 *
 * Mapping Summary:
 *   - WR_SET (0x24): System Config (0x01 enables IO block)
 *   - WR_IO  (0x38): IO0-IO7 Output Control
 *     - IO1: TP_RST  (Active LOW)
 *     - IO2: LCD_BL  (Active HIGH)
 *     - IO3: LCD_RST (Active LOW)
 *     - IO4: SD_CS   (Active LOW)
 *   - RD_IO  (0x26): IO0-IO7 Read
 *
 * NOTE: This class is NOT thread-safe.
 */
class CH422GController {
public:
    static constexpr uint8_t DEFAULT_ADDR_CONFIG = 0x24; // WR_SET
    static constexpr uint8_t DEFAULT_ADDR_RD_IO  = 0x26; // RD_IO
    static constexpr uint8_t DEFAULT_ADDR_IO     = 0x38; // WR_IO

    CH422GController(i2c_master_bus_handle_t bus_handle,
                     uint8_t addr_config = DEFAULT_ADDR_CONFIG,
                     uint8_t addr_rd_io  = DEFAULT_ADDR_RD_IO,
                     uint8_t addr_io     = DEFAULT_ADDR_IO);

    ~CH422GController();

    esp_err_t init();

    // --- Configuration ---
    esp_err_t setSleepMode(bool sleep);
    esp_err_t setOpenDrain(bool enable);
    esp_err_t setIOOutputEnable(bool enable);

    // --- Setters (Output Pins) ---
    esp_err_t setLCDReset(bool active);
    esp_err_t setTouchReset(bool active);
    esp_err_t setBacklight(bool active);
    esp_err_t setSDCardSelected(bool selected);

    // --- Getters (State Reads) ---
    esp_err_t getLCDReset(bool *active);
    esp_err_t getTouchReset(bool *active);
    esp_err_t getBacklight(bool *active);
    esp_err_t getSDCardSelected(bool *selected);

private:
    // Config Register (8-bit at 0x24)
    static constexpr uint8_t BIT_CFG_SLEEP    = (1 << 7);
    static constexpr uint8_t BIT_CFG_OD_EN    = (1 << 4);
    static constexpr uint8_t BIT_CFG_A_SCAN   = (1 << 2);
    static constexpr uint8_t BIT_CFG_IO_OE    = (1 << 0);

    // IO Register (8-bit at 0x38)
    static constexpr uint8_t BIT_IO_TP_RST    = (1 << 1);
    static constexpr uint8_t BIT_IO_LCD_BL    = (1 << 2);
    static constexpr uint8_t BIT_IO_LCD_RST   = (1 << 3);
    static constexpr uint8_t BIT_IO_SD_CS     = (1 << 4);

    i2c_master_bus_handle_t m_bus_handle;
    uint8_t m_addr_config;
    uint8_t m_addr_rd_io;
    uint8_t m_addr_io;

    i2c_master_dev_handle_t m_dev_config = nullptr;
    i2c_master_dev_handle_t m_dev_rd_io  = nullptr;
    i2c_master_dev_handle_t m_dev_io     = nullptr;

    uint8_t m_cfg_cache = BIT_CFG_IO_OE; // Initial config: IO_OE=1, others=0
    uint8_t m_io_cache  = BIT_IO_TP_RST | BIT_IO_LCD_RST | BIT_IO_SD_CS; // TP_RST=H, LCD_BL=L, LCD_RST=H, SD_CS=H

    esp_err_t writeConfig(uint8_t val);
    esp_err_t writeIO(uint8_t val);
    esp_err_t readIO(uint8_t *val);
};
