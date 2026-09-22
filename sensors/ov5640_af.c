/*
 * OV5640 Autofocus Implementation
 * 
 * This file contains the sensor-specific autofocus implementation for OV5640.
 */

#include "sdkconfig.h"

#if defined(CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT) && CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT
#if defined(CONFIG_ESP_CAM_IO_PARL_OV5640) && CONFIG_ESP_CAM_IO_PARL_OV5640

#include "ov5640.h"
#include "ov5640_af_firmware.h"
#include "esp_cam_sensor_io_parl.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "esp_log.h"
static const char *TAG = "ov5640_af";

// OV5640 AF registers
#define OV5640_CMD_MAIN      0x3022
#define OV5640_CMD_ACK       0x3023
#define OV5640_CMD_PARA0     0x3024
#define OV5640_CMD_PARA1     0x3025
#define OV5640_CMD_PARA2     0x3026
#define OV5640_CMD_PARA3     0x3027
#define OV5640_CMD_PARA4     0x3028
#define OV5640_CMD_FW_STATUS 0x3029

#define OV5640_AF_TRIG_SINGLE 0x03
#define OV5640_AF_CONTINUOUS  0x04

#define OV5640_FW_STATUS_IDLE    0x70
#define OV5640_FW_STATUS_FOCUSED 0x10

static esp_err_t ov5640_reg_write(esp_cam_sensor_io_parl_handle_t cam_sensor, int reg, int value) {
    if (cam_sensor->set_reg(cam_sensor, reg, 0xff, value) < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ov5640_reg_read(esp_cam_sensor_io_parl_handle_t cam_sensor, int reg, int *out_value) {
    int v = cam_sensor->get_reg(cam_sensor, reg, 0xff);
    if (v < 0) {
        return ESP_FAIL;
    }
    *out_value = v;
    return ESP_OK;
}

static esp_err_t ov5640_af_wait_ack_clear(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms) {
    const uint64_t start = esp_timer_get_time();
    while (true) {
        int ack = 0;
        if (ov5640_reg_read(cam_sensor, OV5640_CMD_ACK, &ack) != ESP_OK) {
            return ESP_FAIL;
        }
        if (ack == 0x00) {
            return ESP_OK;
        }
        if (timeout_ms && ((esp_timer_get_time() - start) / 1000ULL) > timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static esp_err_t ov5640_af_wait_fw_idle(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms) {
    const uint64_t start = esp_timer_get_time();
    while (true) {
        int st = 0;
        if (ov5640_reg_read(cam_sensor, OV5640_CMD_FW_STATUS, &st) != ESP_OK) {
            return ESP_FAIL;
        }
        if ((uint8_t)st == OV5640_FW_STATUS_IDLE) {
            return ESP_OK;
        }
        if (timeout_ms && ((esp_timer_get_time() - start) / 1000ULL) > timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static esp_err_t ov5640_af_firmware_load(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms) {
    esp_err_t ret = ov5640_reg_write(cam_sensor, 0x3000, 0x20); // reset MCU
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t addr = 0x8000;
    for (size_t i = 0; i < sizeof(ov5640_af_firmware); i++) {
        ret = ov5640_reg_write(cam_sensor, addr, ov5640_af_firmware[i]);
        if (ret != ESP_OK) {
            return ret;
        }
        addr++;
    }

    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_MAIN, 0x00);
    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_ACK, 0x00);
    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_PARA0, 0x00);
    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_PARA1, 0x00);
    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_PARA2, 0x00);
    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_PARA3, 0x00);
    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_PARA4, 0x00);

    (void)ov5640_reg_write(cam_sensor, OV5640_CMD_FW_STATUS, 0x7f);
    (void)ov5640_reg_write(cam_sensor, 0x3000, 0x00); // start MCU

    return ov5640_af_wait_fw_idle(cam_sensor, timeout_ms);
}

static esp_err_t ov5640_af_start(esp_cam_sensor_io_parl_handle_t cam_sensor, bool continuous, uint32_t timeout_ms) {
    esp_err_t ret = ov5640_reg_write(cam_sensor, OV5640_CMD_MAIN, 0x01);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ov5640_reg_write(cam_sensor, OV5640_CMD_MAIN, 0x08);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ov5640_af_wait_ack_clear(cam_sensor, timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ov5640_reg_write(cam_sensor, OV5640_CMD_ACK, 0x01);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ov5640_reg_write(cam_sensor, OV5640_CMD_MAIN, continuous ? OV5640_AF_CONTINUOUS : OV5640_AF_TRIG_SINGLE);
    if (ret != ESP_OK) {
        return ret;
    }

    return ov5640_af_wait_ack_clear(cam_sensor, timeout_ms);
}

// Sensor function pointer implementations
int ov5640_af_is_supported(esp_cam_sensor_io_parl_handle_t cam_sensor) {
    (void)cam_sensor;
    return 1;  // OV5640 always supports AF
}

int ov5640_af_init(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms) {
    if (!cam_sensor) {
        return -1;
    }

    esp_err_t ret = ov5640_af_firmware_load(cam_sensor, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AF firmware init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    return 0;
}

int ov5640_af_set_mode(esp_cam_sensor_io_parl_handle_t cam_sensor, int mode) {
    if (!cam_sensor) {
        return -1;
    }

    esp_err_t ret;
    switch (mode) {
    case 0: // ESP_CAMERA_AF_MODE_AUTO
        ret = ov5640_af_start(cam_sensor, true, 2000);
        break;
    case 1: // ESP_CAMERA_AF_MODE_MANUAL
        ret = ov5640_reg_write(cam_sensor, OV5640_CMD_MAIN, 0x00);
        break;
    default:
        return -1;
    }

    return (ret == ESP_OK) ? 0 : -1;
}

int ov5640_af_trigger(esp_cam_sensor_io_parl_handle_t cam_sensor) {
    if (!cam_sensor) {
        return -1;
    }

    esp_err_t ret = ov5640_af_start(cam_sensor, false, 2000);
    return (ret == ESP_OK) ? 0 : -1;
}

int ov5640_af_get_status(esp_cam_sensor_io_parl_handle_t cam_sensor, uint8_t *out_raw, bool *out_focused, bool *out_busy) {
    if (!cam_sensor || !out_raw || !out_focused || !out_busy) {
        return -1;
    }

    int st = 0;
    esp_err_t ret = ov5640_reg_read(cam_sensor, OV5640_CMD_FW_STATUS, &st);
    if (ret != ESP_OK) {
        return -1;
    }

    *out_raw = (uint8_t)st;
    *out_focused = ((uint8_t)st == OV5640_FW_STATUS_FOCUSED);
    *out_busy = !(((uint8_t)st == OV5640_FW_STATUS_IDLE) || *out_focused);
    
    return 0;
}

int ov5640_af_set_manual_position(esp_cam_sensor_io_parl_handle_t cam_sensor, uint16_t position) {
    (void)cam_sensor;
    (void)position;
    // Manual lens position is not exposed safely in this driver yet.
    return -1;
}

#endif // CONFIG_ESP_CAM_IO_PARL_OV5640
#endif // CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT