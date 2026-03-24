#ifndef __ESP_CAM_IO_PARL_SCCB_H__
#define __ESP_CAM_IO_PARL_SCCB_H__
#include <stdint.h>
#include "esp_err.h"

esp_err_t sccb_init(int pin_sda, int pin_scl);
esp_err_t sccb_deinit(void);
esp_err_t sccb_use_port(int i2c_port);
uint8_t sccb_probe(void);
uint8_t sccb_read(uint8_t slave_address, uint8_t reg);
esp_err_t sccb_write(uint8_t slave_address, uint8_t reg, uint8_t data);
uint8_t sccb_read16(uint8_t slave_address, uint16_t reg);
esp_err_t sccb_write16(uint8_t slave_address, uint16_t reg, uint8_t data);
uint16_t sccb_read_address16_value16(uint8_t slave_address, uint16_t reg);
esp_err_t sccb_write_address16_value16(uint8_t slave_address, uint16_t reg, uint16_t data);
#endif