#include "ch422g.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CH422G";
static i2c_master_bus_handle_t ch422_bus_handle = NULL;
static i2c_master_dev_handle_t ch422_dev_handle_io = NULL;
static i2c_master_dev_handle_t ch422_dev_handle_od = NULL;
static i2c_master_dev_handle_t ch422_dev_handle_set = NULL;

esp_err_t ch422g_init(i2c_master_bus_handle_t bus_handle) {
    ch422_bus_handle = bus_handle;
    i2c_device_config_t dev_cfg_io = {
        .device_address = CH422G_I2C_ADDR_IO,
        .scl_speed_hz = 100000,
    };
    i2c_device_config_t dev_cfg_od = {
        .device_address = CH422G_I2C_ADDR_OC,
        .scl_speed_hz = 100000,
    };
    i2c_device_config_t dev_cfg_set = {
        .device_address = CH422G_I2C_ADDR_SET,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg_io, &ch422_dev_handle_io));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg_od, &ch422_dev_handle_od));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg_set, &ch422_dev_handle_set));

    // Default configuration: enable IO output, enable OC output, disable sleep
    uint8_t config = 0x01 | 0x04; // BIT0: IO enable, BIT2: OC enable
    return i2c_master_transmit(ch422_dev_handle_set, &config, 1, 100);
}

esp_err_t ch422g_write_output(uint8_t bits) {
    if (ch422_dev_handle_io == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(ch422_dev_handle_io, &bits, 1, 100);
}

esp_err_t ch422g_read_input(uint8_t *bits) {
    if (ch422_dev_handle_io == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_receive(ch422_dev_handle_io, bits, 1, 100);
}

esp_err_t ch422g_write_od(uint8_t bits) {
    if (ch422_dev_handle_od == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(ch422_dev_handle_od, &bits, 1, 100);
}

esp_err_t ch422g_write_oc_bit(uint8_t pin, bool level) {
    if (ch422_bus_handle == NULL) return ESP_ERR_INVALID_STATE;
    if (pin > 7) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .device_address = (uint16_t)(CH422G_I2C_ADDR_OC | pin),
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(ch422_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) return ret;

    uint8_t dummy = 0; // The level is in the address, but some devices want a byte
    ret = i2c_master_transmit(dev_handle, &dummy, 0, 100);
    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

esp_err_t ch422g_set_config(uint8_t config) {
    if (ch422_dev_handle_set == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(ch422_dev_handle_set, &config, 1, 100);
}
