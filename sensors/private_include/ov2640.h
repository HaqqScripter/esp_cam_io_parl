/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * OV2640 driver.
 *
 */
#ifndef __OV2640_H__
#define __OV2640_H__
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
int ov2640_detect(int sccb_address, esp_cam_sensor_io_parl_id_t *id);

/**
 * @brief initialize sensor function pointers
 *
 * @param sensor pointer of sensor
 * @return
 *      Always 0
 */
int ov2640_init(esp_cam_sensor_io_parl_handle_t cam_sensor);

#endif