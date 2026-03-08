#include "CH422GController.hpp"
#include "esp_log.h"

CH422GController::CH422GController(i2c_master_bus_handle_t bus_handle,
                                   uint8_t addr_config,
                                   uint8_t addr_rd_io,
                                   uint8_t addr_exio,
                                   uint8_t addr_oc)
    : m_bus_handle(bus_handle),
      m_addr_config(addr_config),
      m_addr_rd_io(addr_rd_io),
      m_addr_exio(addr_exio),
      m_addr_oc(addr_oc) {
}

CH422GController::~CH422GController() {
    if (m_dev_config) i2c_master_bus_rm_device(m_dev_config);
    if (m_dev_rd_io)  i2c_master_bus_rm_device(m_dev_rd_io);
    if (m_dev_exio)   i2c_master_bus_rm_device(m_dev_exio);
    if (m_dev_oc)     i2c_master_bus_rm_device(m_dev_oc);
}

esp_err_t CH422GController::init() {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 400000;

    dev_cfg.device_address = m_addr_config;
    esp_err_t ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_config);
    if (ret != ESP_OK) return ret;

    dev_cfg.device_address = m_addr_rd_io;
    ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_rd_io);
    if (ret != ESP_OK) return ret;

    dev_cfg.device_address = m_addr_exio;
    ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_exio);
    if (ret != ESP_OK) return ret;

    dev_cfg.device_address = m_addr_oc;
    ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_oc);
    if (ret != ESP_OK) return ret;

    // 1. Send system config (enable IO, enable OC, awake)
    uint8_t config = 0x01 | 0x04;
    ret = i2c_master_transmit(m_dev_config, &config, 1, 100);
    if (ret != ESP_OK) return ret;

    // 2. Initialize EXIO (DI0, DI1 high; DO0, DO1 low)
    m_exio_cache = BIT_EXIO_DI0 | BIT_EXIO_DI1;
    ret = writeEXIO(m_exio_cache);
    if (ret != ESP_OK) return ret;

    // 3. Initialize OC (Resets and CS high/inactive; Backlight low/off)
    m_oc_cache = BIT_OC_TP_RST | BIT_OC_LCD_RST | BIT_OC_SD_CS;
    return writeOC(m_oc_cache);
}

// --- Setters (Output Pins) ---

esp_err_t CH422GController::setLCDReset(bool active) {
    if (active) m_oc_cache &= ~BIT_OC_LCD_RST;
    else        m_oc_cache |= BIT_OC_LCD_RST;
    return writeOC(m_oc_cache);
}

esp_err_t CH422GController::setTouchReset(bool active) {
    if (active) m_oc_cache &= ~BIT_OC_TP_RST;
    else        m_oc_cache |= BIT_OC_TP_RST;
    return writeOC(m_oc_cache);
}

esp_err_t CH422GController::setBacklight(bool active) {
    if (active) m_oc_cache |= BIT_OC_DISP;
    else        m_oc_cache &= ~BIT_OC_DISP;
    return writeOC(m_oc_cache);
}

esp_err_t CH422GController::setSDCardSelected(bool selected) {
    if (selected) m_oc_cache &= ~BIT_OC_SD_CS;
    else          m_oc_cache |= BIT_OC_SD_CS;
    return writeOC(m_oc_cache);
}

esp_err_t CH422GController::setDigitalOutput0(bool active) {
    if (active) m_exio_cache |= BIT_EXIO_DO0;
    else        m_exio_cache &= ~BIT_EXIO_DO0;
    return writeEXIO(m_exio_cache);
}

esp_err_t CH422GController::setDigitalOutput1(bool active) {
    if (active) m_exio_cache |= BIT_EXIO_DO1;
    else        m_exio_cache &= ~BIT_EXIO_DO1;
    return writeEXIO(m_exio_cache);
}

// --- Getters ---

esp_err_t CH422GController::getLCDReset(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = !(m_oc_cache & BIT_OC_LCD_RST);
    return ESP_OK;
}

esp_err_t CH422GController::getTouchReset(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = !(m_oc_cache & BIT_OC_TP_RST);
    return ESP_OK;
}

esp_err_t CH422GController::getBacklight(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = (m_oc_cache & BIT_OC_DISP);
    return ESP_OK;
}

esp_err_t CH422GController::getSDCardSelected(bool *selected) {
    if (!selected) return ESP_ERR_INVALID_ARG;
    *selected = !(m_oc_cache & BIT_OC_SD_CS);
    return ESP_OK;
}

esp_err_t CH422GController::getDigitalInput0(bool *level) {
    if (!level) return ESP_ERR_INVALID_ARG;
    uint8_t val;
    esp_err_t err = readEXIO(&val);
    if (err == ESP_OK) *level = (val & BIT_EXIO_DI0);
    return err;
}

esp_err_t CH422GController::getDigitalInput1(bool *level) {
    if (!level) return ESP_ERR_INVALID_ARG;
    uint8_t val;
    esp_err_t err = readEXIO(&val);
    if (err == ESP_OK) *level = (val & BIT_EXIO_DI1);
    return err;
}

esp_err_t CH422GController::getDigitalOutput0(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = (m_exio_cache & BIT_EXIO_DO0);
    return ESP_OK;
}

esp_err_t CH422GController::getDigitalOutput1(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = (m_exio_cache & BIT_EXIO_DO1);
    return ESP_OK;
}

// --- Private Helpers ---

esp_err_t CH422GController::writeEXIO(uint8_t val) {
    if (!m_dev_exio) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(m_dev_exio, &val, 1, 100);
}

esp_err_t CH422GController::writeOC(uint8_t val) {
    if (!m_dev_oc) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(m_dev_oc, &val, 1, 100);
}

esp_err_t CH422GController::readEXIO(uint8_t *val) {
    if (!m_dev_rd_io) return ESP_ERR_INVALID_STATE;
    return i2c_master_receive(m_dev_rd_io, val, 1, 100);
}
