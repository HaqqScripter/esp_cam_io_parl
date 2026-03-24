#ifndef __ESP_CAM_IO_PARL_XCLK_H__
#define __ESP_CAM_IO_PARL_XCLK_H__
#pragma once

#include "esp_cam_sensor_io_parl.h"

esp_err_t xclk_timer_conf(int ledc_timer, int xclk_freq_hz);
esp_err_t camera_enable_out_clock(const esp_cam_sensor_io_parl_config_t *config);
void camera_disable_out_clock(void);
#endif