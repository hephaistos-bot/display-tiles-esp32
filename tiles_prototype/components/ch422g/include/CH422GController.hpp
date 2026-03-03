#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief CH422G I/O Expander Controller class for Waveshare ESP32-S3-Touch-LCD-5.
 *
 * This class provides explicit control over the pins managed by the CH422G chip.
 * It maintains an internal cache of output states to prevent accidental modification
 * of neighboring pins.
 *
 * NOTE: This class is NOT thread-safe. Caller must ensure synchronized access
 * if used from multiple tasks.
 */
class CH422GController {
public:
    static constexpr uint8_t DEFAULT_ADDR_CONFIG = 0x24;
    static constexpr uint8_t DEFAULT_ADDR_EXIO   = 0x27;
    static constexpr uint8_t DEFAULT_ADDR_OC     = 0x38;

    /**
     * @brief Construct a new CH422GController object.
     *
     * @param bus_handle Handle to the initialized I2C master bus.
     * @param addr_config I2C address for the configuration register.
     * @param addr_exio I2C address for the EXIO register.
     * @param addr_oc I2C address for the OC (Open-Drain) register.
     */
    CH422GController(i2c_master_bus_handle_t bus_handle,
                     uint8_t addr_config = DEFAULT_ADDR_CONFIG,
                     uint8_t addr_exio = DEFAULT_ADDR_EXIO,
                     uint8_t addr_oc = DEFAULT_ADDR_OC);

    ~CH422GController();

    /**
     * @brief Initialize the CH422G chip and add it to the I2C bus.
     *
     * Adds the required I2C devices to the bus and sends the default configuration:
     * enables both EXIO and OC outputs and disables sleep mode.
     *
     * @return esp_err_t ESP_OK on success, or I2C error code.
     */
    esp_err_t init();

    /**
     * @brief Set the initial state of the EXIO cache.
     *
     * This does NOT write to the chip. Use this to pre-load the desired state
     * of all EXIO pins before the first individual setter call.
     *
     * @param val The 8-bit value to set in the cache.
     */
    void setEXIOInitialState(uint8_t val) { m_exio_cache = val; }

    /**
     * @brief Set the initial state of the OC cache.
     *
     * This does NOT write to the chip.
     *
     * @param val The 8-bit value to set in the cache.
     */
    void setOCInitialState(uint8_t val) { m_oc_cache = val; }

    // --- Setters (Output Pins) ---

    /**
     * @brief Set the LCD Reset pin state (Active LOW).
     * @param active True to assert reset (LOW), false to release (HIGH).
     */
    esp_err_t setLCDReset(bool active);

    /**
     * @brief Set the Touch Reset pin state (Active LOW).
     * @param active True to assert reset (LOW), false to release (HIGH).
     */
    esp_err_t setTouchReset(bool active);

    /**
     * @brief Set the Backlight (DISP) pin state.
     * @param active True to enable backlight (HIGH), false to disable (LOW).
     */
    esp_err_t setBacklight(bool active);

    /**
     * @brief Set the SD Card Chip Select pin state (Active LOW).
     * @param selected True to select (LOW), false to deselect (HIGH).
     */
    esp_err_t setSDCardSelected(bool selected);

    /**
     * @brief Set Digital Output 0 (Open-Drain).
     * @param active True to enable (sinks current), false to disable (Hi-Z).
     */
    esp_err_t setDigitalOutput0(bool active);

    /**
     * @brief Set Digital Output 1 (Open-Drain).
     * @param active True to enable (sinks current), false to disable (Hi-Z).
     */
    esp_err_t setDigitalOutput1(bool active);

    // --- Getters (State Reads) ---

    /**
     * @brief Get the current state of the LCD Reset pin.
     * @param[out] active Pointer to store result (true if LOW/Active).
     */
    esp_err_t getLCDReset(bool *active);

    /**
     * @brief Get the current state of the Touch Reset pin.
     * @param[out] active Pointer to store result (true if LOW/Active).
     */
    esp_err_t getTouchReset(bool *active);

    /**
     * @brief Get the current state of the Backlight pin.
     * @param[out] active Pointer to store result (true if HIGH/Enabled).
     */
    esp_err_t getBacklight(bool *active);

    /**
     * @brief Get the current state of the SD Card CS pin.
     * @param[out] selected Pointer to store result (true if LOW/Selected).
     */
    esp_err_t getSDCardSelected(bool *selected);

    /**
     * @brief Get the state of Digital Input 0.
     * @param[out] level Pointer to store result.
     */
    esp_err_t getDigitalInput0(bool *level);

    /**
     * @brief Get the state of Digital Input 1.
     * @param[out] level Pointer to store result.
     */
    esp_err_t getDigitalInput1(bool *level);

    /**
     * @brief Get the state of Digital Output 0.
     * @param[out] active Pointer to store result (true if enabled/sinking).
     */
    esp_err_t getDigitalOutput0(bool *active);

    /**
     * @brief Get the state of Digital Output 1.
     * @param[out] active Pointer to store result (true if enabled/sinking).
     */
    esp_err_t getDigitalOutput1(bool *active);

private:
    i2c_master_bus_handle_t m_bus_handle;
    uint8_t m_addr_config;
    uint8_t m_addr_exio;
    uint8_t m_addr_oc;

    i2c_master_dev_handle_t m_dev_config = nullptr;
    i2c_master_dev_handle_t m_dev_exio   = nullptr;
    i2c_master_dev_handle_t m_dev_oc     = nullptr;

    uint8_t m_exio_cache = 0;
    uint8_t m_oc_cache   = 0;

    static constexpr uint8_t BIT_EXIO_DI0     = (1 << 0);
    static constexpr uint8_t BIT_EXIO_TP_RST  = (1 << 1);
    static constexpr uint8_t BIT_EXIO_DISP    = (1 << 2);
    static constexpr uint8_t BIT_EXIO_LCD_RST = (1 << 3);
    static constexpr uint8_t BIT_EXIO_SD_CS   = (1 << 4);
    static constexpr uint8_t BIT_EXIO_DI1     = (1 << 5);

    static constexpr uint8_t BIT_OC_DO0       = (1 << 0);
    static constexpr uint8_t BIT_OC_DO1       = (1 << 1);

    esp_err_t writeEXIO(uint8_t val);
    esp_err_t writeOC(uint8_t val);
    esp_err_t readEXIO(uint8_t *val);
    esp_err_t readOC(uint8_t *val);
};
