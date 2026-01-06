#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "sccb.h"
#include "esp_check.h"
#include "hal/i2c_types.h"
#include "sensor.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "sccb";

#define LITTLETOBIG(x) ((x << 8) | (x >> 8))

#include "driver/i2c_master.h"
#include "driver/i2c_types.h"

#define SCCB_TIMEOUT_MS 1000 /*!< I2C timeout duration */

#ifndef CONFIG_ESP_CAM_IO_PARL_SCCB_CLK_FREQ
#define CONFIG_ESP_CAM_IO_PARL_SCCB_CLK_FREQ 100000
#endif
#define SCCB_FREQ CONFIG_ESP_CAM_IO_PARL_SCCB_CLK_FREQ /*!< I2C master frequency */

#if CONFIG_ESP_CAM_IO_PARL_SCCB_I2C_PORT1
const int SCCB_I2C_PORT_DEFAULT = 1;
#else
const int SCCB_I2C_PORT_DEFAULT = 0;
#endif

#define SCCB_MAX_DEVICES UINT8_MAX - 1

typedef struct {
    i2c_master_dev_handle_t dev_handle;
    uint16_t address;
} sccb_device_t;

static sccb_device_t devices[SCCB_MAX_DEVICES];
static uint8_t device_count = 0;
static int sccb_i2c_port;
static bool sccb_owns_i2c_port;

i2c_master_dev_handle_t *get_handle_from_address(uint8_t slave_address) {
    for (uint8_t i = 0; i < device_count; i++) {
        if (slave_address == devices[i].address) {
            return &(devices[i].dev_handle);
        }
    }

    ESP_LOGE(TAG, "Device with address %02x not found", slave_address);
    return NULL;
}

esp_err_t sccb_add_device(uint8_t slave_address) {
    i2c_master_bus_handle_t bus_handle;

    ESP_RETURN_ON_FALSE(device_count <= SCCB_MAX_DEVICES, ESP_FAIL, TAG, "Can not add more than %d devices", SCCB_MAX_DEVICES);
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(sccb_i2c_port, &bus_handle), TAG, "Failed to get SCCB I2C Bus handle for port %d", sccb_i2c_port);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = slave_address, // not yet set
        .scl_speed_hz = SCCB_FREQ,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &(devices[device_count].dev_handle));
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to install SCCB I2C device: %s", esp_err_to_name(ret));

    devices[device_count++].address = slave_address;
    return ESP_OK;
}

esp_err_t sccb_init(int pin_sda, int pin_scl) {
    ESP_LOGI(TAG, "pin_sda=%d, pin_scl=%d", pin_sda, pin_scl);

#if CONFIG_ESP_CAM_IO_PARL_SCCB_LP_I2C_PORT0
    sccb_i2c_port = LP_I2C_NUM_0;
#else
    sccb_i2c_port = SCCB_I2C_PORT_DEFAULT;
#endif
    sccb_owns_i2c_port = true;
    ESP_LOGI(TAG, "sccb_i2c_port=%d", sccb_i2c_port);

    i2c_master_bus_config_t i2c_mst_config = {
#if CONFIG_ESP_CAM_IO_PARL_SCCB_LP_I2C_PORT0 // Using the LP I2C requires fixed SDA and SCL pins
        .clk_source = LP_I2C_SCLK_DEFAULT,
        .i2c_port = LP_I2C_NUM_0,
#else
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = SCCB_I2C_PORT_DEFAULT,
#endif
        .scl_io_num = pin_scl,
        .sda_io_num = pin_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1
    };

    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to install SCCB I2C master bus on port %d: %s", sccb_i2c_port, esp_err_to_name(ret));

    return ESP_OK;
}

esp_err_t sccb_deinit(void) {
    for (uint8_t i = 0; i < device_count; i++) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(devices[i].dev_handle), TAG, "Failed to remove SCCB I2C Device");

        devices[i].dev_handle = NULL;
        devices[i].address = 0;
    }
    device_count = 0;

    if (!sccb_owns_i2c_port) {
        return ESP_OK;
    }
    sccb_owns_i2c_port = false;

    i2c_master_bus_handle_t bus_handle;
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(sccb_i2c_port, &bus_handle), TAG, "failed to get SCCB I2C Bus handle for port %d", sccb_i2c_port);
    ESP_RETURN_ON_ERROR(i2c_del_master_bus(bus_handle), TAG, "failed to get delete SCCB I2C Master Bus at port %d", sccb_i2c_port);

    return ESP_OK;
}

esp_err_t sccb_use_port(int i2c_port) {
    // Deinitialize SCCB port if used
    if (sccb_owns_i2c_port) {
        sccb_deinit();
    }
    if (i2c_port < 0 || i2c_port > I2C_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    sccb_i2c_port = i2c_port;

    return ESP_OK;
}

uint8_t sccb_probe(void) {
    uint8_t slave_address = 0x0;
    i2c_master_bus_handle_t bus_handle;

    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(sccb_i2c_port, &bus_handle),TAG, "Failed to get SCCB I2C Bus handle for port %d", sccb_i2c_port);

    for (size_t i = 0; i < CAMERA_MODEL_MAX; i++) {
        if (slave_address == camera_sensor[i].sccb_address) {
            continue;
        }
        slave_address = camera_sensor[i].sccb_address;

        esp_err_t ret = i2c_master_probe(bus_handle, slave_address, SCCB_TIMEOUT_MS);
        if (ret == ESP_OK) {
            if (sccb_add_device(slave_address) != ESP_OK) {
                return 0;
            }
            return slave_address;
        }
    }
    return 0;
}

uint8_t sccb_read(uint8_t slave_address, uint8_t reg) {
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slave_address));

    uint8_t tx_buffer[1];
    uint8_t rx_buffer[1];

    tx_buffer[0] = reg;

    esp_err_t ret = i2c_master_transmit_receive(dev_handle, tx_buffer, 1, rx_buffer, 1, SCCB_TIMEOUT_MS);
    ESP_RETURN_ON_ERROR(ret, TAG, "SCCB_Read Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slave_address, reg, rx_buffer[0], ret);

    return rx_buffer[0];
}

esp_err_t sccb_write(uint8_t slave_address, uint8_t reg, uint8_t data) {
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slave_address));

    uint8_t tx_buffer[2];
    tx_buffer[0] = reg;
    tx_buffer[1] = data;

    esp_err_t ret = i2c_master_transmit(dev_handle, tx_buffer, 2, SCCB_TIMEOUT_MS);
    ESP_RETURN_ON_ERROR(ret, TAG, "SCCB_Write Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slave_address, reg, data, ret);

    return ESP_OK;
}

uint8_t sccb_read16(uint8_t slave_address, uint16_t reg) {
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slave_address));

    uint8_t rx_buffer[1];

    uint16_t reg_htons = LITTLETOBIG(reg);
    uint8_t *reg_u8 = (uint8_t *)&reg_htons;

    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(dev_handle, reg_u8, 2, rx_buffer, 1, SCCB_TIMEOUT_MS), TAG, "W [%04x]=%02x fail\n", reg, rx_buffer[0]);

    return rx_buffer[0];
}

esp_err_t sccb_write16(uint8_t slave_address, uint16_t reg, uint8_t data) {
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slave_address));

    uint8_t tx_buffer[3];
    tx_buffer[0] = reg >> 8;
    tx_buffer[1] = reg & 0x00ff;
    tx_buffer[2] = data;

    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev_handle, tx_buffer, 3, SCCB_TIMEOUT_MS), TAG, "W [%04x]=%02x fail\n", reg, data);

    return ESP_OK;
}

uint16_t sccb_read_address16_value16(uint8_t slave_address, uint16_t reg) {
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slave_address));

    uint8_t rx_buffer[2];

    uint16_t reg_htons = LITTLETOBIG(reg);
    uint8_t *reg_u8 = (uint8_t *)&reg_htons;

    esp_err_t ret = i2c_master_transmit_receive(dev_handle, reg_u8, 2, rx_buffer, 2, SCCB_TIMEOUT_MS);
    uint16_t data = ((uint16_t)rx_buffer[0] << 8) | (uint16_t)rx_buffer[1];

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "W [%04x]=%02x fail\n", reg, data);
    }

    return data;
}

esp_err_t sccb_write_address16_value16(uint8_t slave_address, uint16_t reg, uint16_t data) {
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slave_address));

    uint8_t tx_buffer[4];
    tx_buffer[0] = reg >> 8;
    tx_buffer[1] = reg & 0x00ff;
    tx_buffer[2] = data >> 8;
    tx_buffer[3] = data & 0x00ff;

    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev_handle, tx_buffer, 4, SCCB_TIMEOUT_MS), TAG, "W [%04x]=%02x fail\n", reg, data);

    return ESP_OK;
}