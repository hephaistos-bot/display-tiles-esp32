#include "CH422GController.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

CH422GController::CH422GController(i2c_master_bus_handle_t bus_handle,
                                   uint8_t addr_config,
                                   uint8_t addr_rd_io,
                                   uint8_t addr_io)
    : m_bus_handle(bus_handle),
      m_addr_config(addr_config),
      m_addr_rd_io(addr_rd_io),
      m_addr_io(addr_io) {
}

CH422GController::~CH422GController() {
    if (m_dev_config) i2c_master_bus_rm_device(m_dev_config);
    if (m_dev_rd_io)  i2c_master_bus_rm_device(m_dev_rd_io);
    if (m_dev_io)     i2c_master_bus_rm_device(m_dev_io);
}

esp_err_t CH422GController::init() {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 400000;

    esp_err_t ret;
    if (!m_dev_config) {
        dev_cfg.device_address = m_addr_config;
        ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_config);
        if (ret != ESP_OK) return ret;
    }

    if (!m_dev_rd_io) {
        dev_cfg.device_address = m_addr_rd_io;
        ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_rd_io);
        if (ret != ESP_OK) return ret;
    }

    if (!m_dev_io) {
        dev_cfg.device_address = m_addr_io;
        ret = i2c_master_bus_add_device(m_bus_handle, &dev_cfg, &m_dev_io);
        if (ret != ESP_OK) return ret;
    }

    // 1. Configure CH422G to output mode
    uint8_t config = 0x01;
    ret = i2c_master_transmit(m_dev_config, &config, 1, 100);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. Initial state: Backlight OFF (0x1A)
    m_io_cache = 0x1A;
    return writeIO(m_io_cache);
}

// --- Setters (Output Pins) ---

esp_err_t CH422GController::setLCDReset(bool active) {
    if (active) m_io_cache &= ~BIT_IO_LCD_RST; 
    else        m_io_cache |= BIT_IO_LCD_RST;
    return writeIO(m_io_cache);
}

esp_err_t CH422GController::setTouchReset(bool active) {
    if (active) m_io_cache &= ~BIT_IO_TP_RST;
    else        m_io_cache |= BIT_IO_TP_RST;
    return writeIO(m_io_cache);
}

esp_err_t CH422GController::setBacklight(bool active) {
    if (active) m_io_cache |= BIT_IO_LCD_BL;
    else        m_io_cache &= ~BIT_IO_LCD_BL;
    return writeIO(m_io_cache);
}

esp_err_t CH422GController::setSDCardSelected(bool selected) {
    if (selected) m_io_cache &= ~BIT_IO_SD_CS;
    else          m_io_cache |= BIT_IO_SD_CS;
    return writeIO(m_io_cache);
}

// --- Getters ---

esp_err_t CH422GController::getLCDReset(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = !(m_io_cache & BIT_IO_LCD_RST);
    return ESP_OK;
}

esp_err_t CH422GController::getTouchReset(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = !(m_io_cache & BIT_IO_TP_RST);
    return ESP_OK;
}

esp_err_t CH422GController::getBacklight(bool *active) {
    if (!active) return ESP_ERR_INVALID_ARG;
    *active = (m_io_cache & BIT_IO_LCD_BL);
    return ESP_OK;
}

esp_err_t CH422GController::getSDCardSelected(bool *selected) {
    if (!selected) return ESP_ERR_INVALID_ARG;
    *selected = !(m_io_cache & BIT_IO_SD_CS);
    return ESP_OK;
}

// --- Private Helpers ---

esp_err_t CH422GController::writeIO(uint8_t val) {
    if (!m_dev_io) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(m_dev_io, &val, 1, 100);
}

esp_err_t CH422GController::readIO(uint8_t *val) {
    if (!m_dev_rd_io) return ESP_ERR_INVALID_STATE;
    return i2c_master_receive(m_dev_rd_io, val, 1, 100);
}
