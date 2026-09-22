#ifndef _ESP_CAM_IO_PARL_AF_H_
#define _ESP_CAM_IO_PARL_AF_H_

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_cam_sensor_io_parl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Auto-focus modes.
 *
 * - AUTO: sensor runs its internal AF routine (continuous, if supported).
 * - MANUAL: AF is not running automatically; user triggers focus explicitly.
 */
typedef enum {
    ESP_CAM_IO_PARL_AF_MODE_AUTO = 0,
    ESP_CAM_IO_PARL_AF_MODE_MANUAL = 1,
} esp_cam_io_parl_af_mode_t;

/**
 * @brief Auto-focus configuration.
 *
 * Notes:
 * - Some fields are sensor-dependent (e.g., manual position control).
 * - For sensors without AF hardware/firmware support, APIs return ESP_ERR_NOT_SUPPORTED.
 */
typedef struct {
    esp_cam_io_parl_af_mode_t mode;

    /**
     * @brief Step size for manual stepping algorithms.
     *
     * Used by software search helpers and sensors that expose manual lens control.
     */
    uint16_t step_size;

    /**
     * @brief Inclusive focus range limits for manual stepping algorithms.
     */
    uint16_t range_min;
    uint16_t range_max;

    /**
     * @brief Default timeout used by AF operations, in milliseconds.
     */
    uint32_t timeout_ms;
} esp_cam_io_parl_af_config_t;

/**
 * @brief AF status values (sensor-specific raw values).
 */
typedef struct {
    uint8_t raw;
    bool focused;
    bool busy;
} esp_cam_io_parl_af_status_t;

/**
 * @brief Return whether the attached sensor supports AF through this module.
 */
bool esp_cam_io_parl_af_is_supported(const esp_cam_sensor_io_parl_handle_t cam_sensor);

/**
 * @brief Initialize AF support for the attached sensor.
 */
esp_err_t esp_cam_io_parl_af_init(esp_cam_sensor_io_parl_handle_t cam_sensor, const esp_cam_io_parl_af_config_t *config);

/**
 * @brief Change AF mode.
 */
esp_err_t esp_cam_io_parl_af_set_mode(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_io_parl_af_mode_t mode);

/**
 * @brief Trigger a single autofocus cycle (if supported).
 */
esp_err_t esp_cam_io_parl_af_trigger(esp_cam_sensor_io_parl_handle_t cam_sensor);

/**
 * @brief Read current AF status.
 */
esp_err_t esp_cam_io_parl_af_get_status(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_io_parl_af_status_t *out_status);

/**
 * @brief Wait for AF to finish or report focused.
 *
 * @param timeout_ms If 0, uses config timeout passed to esp_camera_af_init().
 */
esp_err_t esp_cam_io_parl_af_wait(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms, esp_cam_io_parl_af_status_t *out_status);

/**
 * @brief Set manual lens position.
 *
 * Some sensors (including OV5640 when using internal AF firmware only) may not expose a safe
 * manual lens position register via the public driver; in those cases this returns ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t esp_cam_io_parl_af_set_manual_position(esp_cam_sensor_io_parl_handle_t cam_sensor, uint16_t position);

#ifdef __cplusplus
}
#endif
#endif