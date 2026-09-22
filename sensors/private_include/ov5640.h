
#ifndef __OV5640_H__
#define __OV5640_H__

#include "esp_cam_sensor_io_parl.h"

/**
 * @brief Detect sensor pid
 *
 * @param sccb_address SCCB address
 * @param id Detection result
 * @return
 *     0:       Can't detect this sensor
 *     Nonzero: This sensor has been detected
 */
int ov5640_detect(int sccb_address, esp_cam_sensor_io_parl_id_t *id);

/**
 * @brief initialize sensor function pointers
 *
 * @param sensor pointer of sensor
 * @return
 *      Always 0
 */
int ov5640_init(esp_cam_sensor_io_parl_handle_t cam_sensor);

// Autofocus function implementations (in ov5640_af.c)
#if defined(CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT) && CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT
int ov5640_af_is_supported(esp_cam_sensor_io_parl_handle_t cam_sensor);
int ov5640_af_init(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms);
int ov5640_af_set_mode(esp_cam_sensor_io_parl_handle_t cam_sensor, int mode);
int ov5640_af_trigger(esp_cam_sensor_io_parl_handle_t cam_sensor);
int ov5640_af_get_status(esp_cam_sensor_io_parl_handle_t cam_sensor, uint8_t *out_raw, bool *out_focused, bool *out_busy);
int ov5640_af_set_manual_position(esp_cam_sensor_io_parl_handle_t cam_sensor, uint16_t position);
#endif
#endif