
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

#endif