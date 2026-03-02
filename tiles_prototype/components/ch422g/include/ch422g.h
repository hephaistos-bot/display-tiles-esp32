#ifndef CH422G_H
#define CH422G_H

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C Addresses for CH422G (7-bit)
#define CH422G_I2C_ADDR_IO     0x27  // EXIO (Byte-wide write: 4EH)
#define CH422G_I2C_ADDR_OC     0x38  // Open-Drain (Byte-wide write: 70H)
#define CH422G_I2C_ADDR_SET    0x24  // Config/Setting register (48H)

esp_err_t ch422g_init(i2c_master_bus_handle_t bus_handle);
esp_err_t ch422g_write_output(uint8_t bits);
esp_err_t ch422g_read_input(uint8_t *bits);
esp_err_t ch422g_write_od(uint8_t bits);
esp_err_t ch422g_write_oc_bit(uint8_t pin, bool level);
esp_err_t ch422g_set_config(uint8_t config);

#ifdef __cplusplus
}
#endif

#endif // CH422G_H
