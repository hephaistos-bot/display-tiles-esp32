#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief CH422G I/O Expander Controller class for Waveshare ESP32-S3-Touch-LCD-5.
 *
 * Mapping Summary (based on hardware investigation):
 *   - OC (0x38): TP_RST (0), DISP/BL (1), LCD_RST (2), SD_CS (3)
 *   - EXIO (0x27): DI0 (0), DI1 (5), DO0 (6), DO1 (7)
 *
 * NOTE: This class is NOT thread-safe.
 */
class CH422GController {
public:
    static constexpr uint8_t DEFAULT_ADDR_CONFIG = 0x24;
    static constexpr uint8_t DEFAULT_ADDR_RD_IO  = 0x26;
    static constexpr uint8_t DEFAULT_ADDR_EXIO   = 0x27;
    static constexpr uint8_t DEFAULT_ADDR_OC     = 0x38;

    CH422GController(i2c_master_bus_handle_t bus_handle,
                     uint8_t addr_config = DEFAULT_ADDR_CONFIG,
                     uint8_t addr_rd_io  = DEFAULT_ADDR_RD_IO,
                     uint8_t addr_exio   = DEFAULT_ADDR_EXIO,
                     uint8_t addr_oc     = DEFAULT_ADDR_OC);

    ~CH422GController();

    esp_err_t init();

    // --- Setters (Output Pins) ---
    esp_err_t setLCDReset(bool active);
    esp_err_t setTouchReset(bool active);
    esp_err_t setBacklight(bool active);
    esp_err_t setSDCardSelected(bool selected);
    esp_err_t setDigitalOutput0(bool active);
    esp_err_t setDigitalOutput1(bool active);

    // --- Getters (State Reads) ---
    esp_err_t getLCDReset(bool *active);
    esp_err_t getTouchReset(bool *active);
    esp_err_t getBacklight(bool *active);
    esp_err_t getSDCardSelected(bool *selected);
    esp_err_t getDigitalInput0(bool *level);
    esp_err_t getDigitalInput1(bool *level);
    esp_err_t getDigitalOutput0(bool *active);
    esp_err_t getDigitalOutput1(bool *active);

    void setEXIOInitialState(uint8_t val) { m_exio_cache = val; }
    void setOCInitialState(uint8_t val) { m_oc_cache = val; }

private:
    i2c_master_bus_handle_t m_bus_handle;
    uint8_t m_addr_config;
    uint8_t m_addr_rd_io;
    uint8_t m_addr_exio;
    uint8_t m_addr_oc;

    i2c_master_dev_handle_t m_dev_config = nullptr;
    i2c_master_dev_handle_t m_dev_rd_io  = nullptr;
    i2c_master_dev_handle_t m_dev_exio   = nullptr;
    i2c_master_dev_handle_t m_dev_oc     = nullptr;

    uint8_t m_exio_cache = 0;
    uint8_t m_oc_cache   = 0;

    // OC Register (4-bit output-only at 0x38)
    static constexpr uint8_t BIT_OC_TP_RST    = (1 << 0); // OC0
    static constexpr uint8_t BIT_OC_DISP      = (1 << 1); // OC1 (LCD_BL)
    static constexpr uint8_t BIT_OC_LCD_RST   = (1 << 2); // OC2
    static constexpr uint8_t BIT_OC_SD_CS     = (1 << 3); // OC3

    // EXIO Register (8-bit bidirectional at 0x27)
    static constexpr uint8_t BIT_EXIO_DI0     = (1 << 0); // IO0
    static constexpr uint8_t BIT_EXIO_DI1     = (1 << 5); // IO5
    static constexpr uint8_t BIT_EXIO_DO0     = (1 << 6); // IO6
    static constexpr uint8_t BIT_EXIO_DO1     = (1 << 7); // IO7

    esp_err_t writeEXIO(uint8_t val);
    esp_err_t writeOC(uint8_t val);
    esp_err_t readEXIO(uint8_t *val);
};
