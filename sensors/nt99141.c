/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for
 * details.
 *
 * NT99141 driver.
 *
 */
#include "nt99141.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nt99141_regs.h"
#include "nt99141_settings.h"
#include "esp_cam_io_parl_sccb.h"
#include "esp_cam_io_parl_xclk.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
static const char *TAG = "NT99141";

// #define REG_DEBUG_ON

static int read_reg(uint8_t sccb_address, const uint16_t reg) {
	int ret = sccb_read16(sccb_address, reg);
#ifdef REG_DEBUG_ON

	if (ret < 0) {
		ESP_LOGE(TAG, "READ REG 0x%04x FAILED: %d", reg, ret);
	}

#endif
	return ret;
}

static int check_reg_mask(uint8_t sccb_address, uint16_t reg, uint8_t mask) {
	return (read_reg(sccb_address, reg) & mask) == mask;
}

static int read_reg16(uint8_t sccb_address, const uint16_t reg) {
	int ret = 0, ret2 = 0;
	ret = read_reg(sccb_address, reg);

	if (ret >= 0) {
		ret = (ret & 0xFF) << 8;
		ret2 = read_reg(sccb_address, reg + 1);

		if (ret2 < 0) {
			ret = ret2;
		} else {
			ret |= ret2 & 0xFF;
		}
	}

	return ret;
}

static int write_reg(uint8_t sccb_address, const uint16_t reg, uint8_t value) {
	int ret = 0;
#ifndef REG_DEBUG_ON
	ret = sccb_write16(sccb_address, reg, value);
#else
	int old_value = read_reg(sccb_address, reg);

	if (old_value < 0) {
		return old_value;
	}

	if ((uint8_t)old_value != value) {
		ESP_LOGD(TAG, "NEW REG 0x%04x: 0x%02x to 0x%02x", reg,
				 (uint8_t)old_value, value);
		ret = sccb_write16(sccb_address, reg, value);
	} else {
		ESP_LOGD(TAG, "OLD REG 0x%04x: 0x%02x", reg, (uint8_t)old_value);
		ret = sccb_write16(sccb_address, reg, value); // maybe not?
	}

	if (ret < 0) {
		ESP_LOGE(TAG, "WRITE REG 0x%04x FAILED: %d", reg, ret);
	}

#endif
	return ret;
}

static int set_reg_bits(uint8_t sccb_address, uint16_t reg, uint8_t offset, uint8_t mask, uint8_t value) {
	int ret = 0;
	uint8_t c_value, new_value;
	ret = read_reg(sccb_address, reg);

	if (ret < 0) {
		return ret;
	}

	c_value = ret;
	new_value = (c_value & ~(mask << offset)) | ((value & mask) << offset);
	ret = write_reg(sccb_address, reg, new_value);
	return ret;
}

static int write_regs(uint8_t sccb_address, const uint16_t (*regs)[2]) {
	int i = 0, ret = 0;

	while (!ret && regs[i][0] != REGLIST_TAIL) {
		if (regs[i][0] == REG_DLY) {
			vTaskDelay(regs[i][1] / portTICK_PERIOD_MS);
		} else {
			ret = write_reg(sccb_address, regs[i][0], regs[i][1]);
		}

		i++;
	}

	return ret;
}

static int write_reg16(uint8_t sccb_address, const uint16_t reg, uint16_t value) {
	if (write_reg(sccb_address, reg, value >> 8) ||
		write_reg(sccb_address, reg + 1, value)) {
		return -1;
	}

	return 0;
}

static int write_addr_reg(uint8_t sccb_address, const uint16_t reg, uint16_t x_value, uint16_t y_value) {
	if (write_reg16(sccb_address, reg, x_value) ||
		write_reg16(sccb_address, reg + 2, y_value)) {
		return -1;
	}

	return 0;
}

#define write_reg_bits(sccb_address, reg, mask, enable) set_reg_bits(sccb_address, reg, 0, mask, enable ? mask : 0)

static int set_pll(esp_cam_sensor_io_parl_handle_t cam_sensor, bool bypass, uint8_t multiplier, uint8_t sys_div, uint8_t pre_div, bool root_2x, uint8_t seld5, bool pclk_manual, uint8_t pclk_div) {
	return -1;
}

static int set_ae_level(esp_cam_sensor_io_parl_handle_t cam_sensor, int level);

static int reset(esp_cam_sensor_io_parl_handle_t cam_sensor) {
	int ret = 0;
	// Software Reset: clear all registers and reset them to their default
	// values
	ret = write_reg(cam_sensor->sccb_address, SYSTEM_CTROL0, 0x01);

	if (ret) {
		ESP_LOGE(TAG, "Software Reset FAILED!");
		return ret;
	}

	vTaskDelay(100 / portTICK_PERIOD_MS);
	ret = write_regs(cam_sensor->sccb_address, sensor_default_regs); // re-initial

	if (ret == 0) {
		ESP_LOGD(TAG, "Camera defaults loaded");
		ret = set_ae_level(cam_sensor, 0);
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}

	return ret;
}

static int set_pixformat(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_pixformat_t pixformat) {
	int ret = 0;
	const uint16_t(*regs)[2];

	switch (pixformat) {
	case ESP_CAM_IO_PARL_PIXFORMAT_YUV422:
		regs = sensor_fmt_yuv422;
		break;

	case ESP_CAM_IO_PARL_PIXFORMAT_GRAYSCALE:
		regs = sensor_fmt_grayscale;
		break;

	case ESP_CAM_IO_PARL_PIXFORMAT_RGB565:
	case ESP_CAM_IO_PARL_PIXFORMAT_RGB888:
		regs = sensor_fmt_rgb565;
		break;

	case ESP_CAM_IO_PARL_PIXFORMAT_JPEG:
		regs = sensor_fmt_jpeg;
		break;

	case ESP_CAM_IO_PARL_PIXFORMAT_RAW:
		regs = sensor_fmt_raw;
		break;

	default:
		ESP_LOGE(TAG, "Unsupported pixformat: %u", pixformat);
		return -1;
	}

	ret = write_regs(cam_sensor->sccb_address, regs);

	if (ret == 0) {
		cam_sensor->pixformat = pixformat;
		ESP_LOGD(TAG, "Set pixformat to: %u", pixformat);
	}

	return ret;
}

static int set_image_options(esp_cam_sensor_io_parl_handle_t cam_sensor) {
	int ret = 0;
	uint8_t reg20 = 0;
	uint8_t reg21 = 0;
	uint8_t reg4514 = 0;
	uint8_t reg4514_test = 0;

	// V-Flip
	if (cam_sensor->status.vflip) {
		reg20 |= 0x01;
		reg4514_test |= 1;
	}

	// H-Mirror
	if (cam_sensor->status.hmirror) {
		reg21 |= 0x02;
		reg4514_test |= 2;
	}

	switch (reg4514_test) {}

	if (write_reg(cam_sensor->sccb_address, TIMING_TC_REG20, reg20 | reg21)) {
		ESP_LOGE(TAG, "Setting Image Options Failed");
		ret = -1;
	}

	ESP_LOGD(TAG, "Set Image Options: Compression: %u, Binning: %u, V-Flip: %u, H-Mirror: %u, Reg-4514: 0x%02x", cam_sensor->pixformat == ESP_CAM_IO_PARL_PIXFORMAT_JPEG, cam_sensor->status.binning, cam_sensor->status.vflip, cam_sensor->status.hmirror, reg4514);
	return ret;
}

static int set_framesize(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_framesize_t framesize) {
	int ret = 0;

	cam_sensor->status.framesize = framesize;
	ret = write_regs(cam_sensor->sccb_address, sensor_default_regs);

	if (framesize == ESP_CAM_IO_PARL_FRAMESIZE_QVGA) {
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_QVGA");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_QVGA);
#if CONFIG_ESP_CAM_IO_PARL_NT99141_SUPPORT_XSKIP
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_QVGA: xskip mode");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_QVGA_xskip);
#elif CONFIG_ESP_CAM_IO_PARL_NT99141_SUPPORT_CROP
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_QVGA: crop mode");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_QVGA_crop);
#endif
	} else if (framesize == ESP_CAM_IO_PARL_FRAMESIZE_VGA) {
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_VGA");
		// ret = write_regs(cam_sensor->sccb_address, sensor_framesize_VGA);
		ret = write_regs(
			cam_sensor->sccb_address,
			sensor_framesize_VGA_xyskip); // Resolution:640*360 This
										  // configuration is equally-scaled
										  // without deforming
#ifdef CONFIG_ESP_CAM_IO_PARL_NT99141_SUPPORT_XSKIP
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_VGA: xskip mode");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_VGA_xskip);
#elif CONFIG_ESP_CAM_IO_PARL_NT99141_SUPPORT_CROP
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_VGA: crop mode");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_VGA_crop);
#endif
	} else if (framesize >= ESP_CAM_IO_PARL_FRAMESIZE_HD) {
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_HD");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_HD);
	} else {
		ESP_LOGD(TAG, "Set ESP_CAM_IO_PARL_FRAMESIZE_VGA");
		ret = write_regs(cam_sensor->sccb_address, sensor_framesize_VGA);
	}

	return ret;
}

static int set_hmirror(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;
	cam_sensor->status.hmirror = enable;
	ret = set_image_options(cam_sensor);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set h-mirror to: %d", enable);
	}

	return ret;
}

static int set_vflip(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;
	cam_sensor->status.vflip = enable;
	ret = set_image_options(cam_sensor);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set v-flip to: %d", enable);
	}

	return ret;
}

static int set_quality(esp_cam_sensor_io_parl_handle_t cam_sensor, int qs) {
	int ret = 0;
	ret = write_reg(cam_sensor->sccb_address, COMPRESSION_CTRL07, qs & 0x3f);

	if (ret == 0) {
		cam_sensor->status.quality = qs;
		ESP_LOGD(TAG, "Set quality to: %d", qs);
	}

	return ret;
}

static int set_colorbar(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;
	ret = write_reg_bits(cam_sensor->sccb_address, PRE_ISP_TEST_SETTING_1,
						 TEST_COLOR_BAR, enable);

	if (ret == 0) {
		cam_sensor->status.colorbar = enable;
		ESP_LOGD(TAG, "Set colorbar to: %d", enable);
	}

	return ret;
}

static int set_gain_ctrl(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;
	ret = write_reg_bits(cam_sensor->sccb_address, 0x32bb, 0x87, enable);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set gain_ctrl to: %d", enable);
		cam_sensor->status.agc = enable;
	}

	return ret;
}

static int set_exposure_ctrl(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;
	int data = 0;
	// ret = write_reg_bits(cam_sensor->sccb_address, 0x32bb, 0x87, enable);
	data = read_reg(cam_sensor->sccb_address, 0x3201);
	ESP_LOGD(TAG, "set_exposure_ctrl:enable");
	if (enable) {
		ESP_LOGD(TAG, "set_exposure_ctrl:enable");
		ret = write_reg(cam_sensor->sccb_address, 0x3201, (1 << 5) | data);
	} else {
		ESP_LOGD(TAG, "set_exposure_ctrl:disable");
		ret = write_reg(cam_sensor->sccb_address, 0x3201, (~(1 << 5)) & data);
	}

	if (ret == 0) {
		ESP_LOGD(TAG, "Set exposure_ctrl to: %d", enable);
		cam_sensor->status.aec = enable;
	}

	return ret;
}

static int set_whitebal(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set awb to: %d", enable);
		cam_sensor->status.awb = enable;
	}

	return ret;
}

// Advanced AWB
static int set_dcw_dsp(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set dcw to: %d", enable);
		cam_sensor->status.dcw = enable;
	}

	return ret;
}

// night mode enable
static int set_aec2(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set aec2 to: %d", enable);
		cam_sensor->status.aec2 = enable;
	}

	return ret;
}

static int set_bpc_dsp(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set bpc to: %d", enable);
		cam_sensor->status.bpc = enable;
	}

	return ret;
}

static int set_wpc_dsp(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set wpc to: %d", enable);
		cam_sensor->status.wpc = enable;
	}

	return ret;
}

// Gamma enable
static int set_raw_gma_dsp(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set raw_gma to: %d", enable);
		cam_sensor->status.raw_gma = enable;
	}

	return ret;
}

static int set_lenc_dsp(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;

	if (ret == 0) {
		ESP_LOGD(TAG, "Set lenc to: %d", enable);
		cam_sensor->status.lenc = enable;
	}

	return ret;
}

static int get_agc_gain(esp_cam_sensor_io_parl_handle_t cam_sensor) {
	ESP_LOGD(TAG, "get_agc_gain can not be configured at present");
	return 0;
}

// real gain
static int set_agc_gain(esp_cam_sensor_io_parl_handle_t cam_sensor, int gain) {
	ESP_LOGD(TAG, "set_agc_gain can not be configured at present");
	// ESP_LOGD(TAG, "GAIN = %d\n", gain);
	int cnt = gain / 2;

	switch (cnt) {
	case 0:
		ESP_LOGD(TAG, "set_agc_gain: 1x");
		write_reg(cam_sensor->sccb_address, 0X301D, 0X00);
		break;

	case 1:
		ESP_LOGD(TAG, "set_agc_gain: 2x");
		write_reg(cam_sensor->sccb_address, 0X301D, 0X0F);
		break;

	case 2:
		ESP_LOGD(TAG, "set_agc_gain: 4x");
		write_reg(cam_sensor->sccb_address, 0X301D, 0X2F);
		break;

	case 3:
		ESP_LOGD(TAG, "set_agc_gain: 6x");
		write_reg(cam_sensor->sccb_address, 0X301D, 0X37);
		break;

	case 4:
		ESP_LOGD(TAG, "set_agc_gain: 8x");
		write_reg(cam_sensor->sccb_address, 0X301D, 0X3F);
		break;

	default:
		ESP_LOGD(TAG, "fail set_agc_gain");
		break;
	}

	return 0;
}

static int get_aec_value(esp_cam_sensor_io_parl_handle_t cam_sensor) {
	ESP_LOGD(TAG, "get_aec_value can not be configured at present");
	return 0;
}

static int set_aec_value(esp_cam_sensor_io_parl_handle_t cam_sensor, int value) {
	ESP_LOGD(TAG, "set_aec_value can not be configured at present");
	int ret = 0;
	// ESP_LOGD(TAG, " set_aec_value to: %d", value);
	ret =
		write_reg_bits(cam_sensor->sccb_address, 0x3012, 0x00, (value >> 8) & 0xff);
	ret = write_reg_bits(cam_sensor->sccb_address, 0x3013, 0x01, value & 0xff);

	if (ret == 0) {
		ESP_LOGD(TAG, " set_aec_value to: %d", value);
		// cam_sensor->status.aec = enable;
	}

	return ret;
}

static int set_ae_level(esp_cam_sensor_io_parl_handle_t cam_sensor, int level) {
	ESP_LOGD(TAG, "set_ae_level can not be configured at present");
	int ret = 0;

	if (level < 0) {
		level = 0;
	} else if (level > 9) {
		level = 9;
	}

	for (int i = 0; i < 5; i++) {
		ret +=
			write_reg(cam_sensor->sccb_address, sensor_ae_level[5 * level + i][0],
					  sensor_ae_level[5 * level + i][1]);
	}

	if (ret) {
		ESP_LOGE(TAG, " fail to set ae level: %d", ret);
	}

	return 0;
}

static int set_wb_mode(esp_cam_sensor_io_parl_handle_t cam_sensor, int mode) {
	int ret = 0;

	if (mode < 0 || mode > 4) {
		return -1;
	}

	ret = write_reg(cam_sensor->sccb_address, 0x3201, (mode != 0));

	if (ret) {
		return ret;
	}

	switch (mode) {
	case 1: // Sunny
		ret = write_reg16(cam_sensor->sccb_address, 0x3290, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3291, 0x38) ||
			  write_reg16(cam_sensor->sccb_address, 0x3296, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3297, 0x68) ||
			  write_reg16(cam_sensor->sccb_address, 0x3060, 0x01);

		break;

	case 2: // Cloudy

		ret = write_reg16(cam_sensor->sccb_address, 0x3290, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3291, 0x51) ||
			  write_reg16(cam_sensor->sccb_address, 0x3296, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3297, 0x00) ||
			  write_reg16(cam_sensor->sccb_address, 0x3060, 0x01);
		break;

	case 3: // INCANDESCENCE]
		ret = write_reg16(cam_sensor->sccb_address, 0x3290, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3291, 0x30) ||
			  write_reg16(cam_sensor->sccb_address, 0x3296, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3297, 0xCB) ||
			  write_reg16(cam_sensor->sccb_address, 0x3060, 0x01);
		break;

	case 4: // FLUORESCENT
		ret = write_reg16(cam_sensor->sccb_address, 0x3290, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3291, 0x70) ||
			  write_reg16(cam_sensor->sccb_address, 0x3296, 0x01) ||
			  write_reg16(cam_sensor->sccb_address, 0x3297, 0xFF) ||
			  write_reg16(cam_sensor->sccb_address, 0x3060, 0x01);
		break;

	default: // AUTO
		break;
	}

	if (ret == 0) {
		ESP_LOGD(TAG, "Set wb_mode to: %d", mode);
		cam_sensor->status.wb_mode = mode;
	}

	return ret;
}

static int set_awb_gain_dsp(esp_cam_sensor_io_parl_handle_t cam_sensor, int enable) {
	int ret = 0;
	int old_mode = cam_sensor->status.wb_mode;
	int mode = enable ? old_mode : 0;

	ret = set_wb_mode(cam_sensor, mode);

	if (ret == 0) {
		cam_sensor->status.wb_mode = old_mode;
		ESP_LOGD(TAG, "Set awb_gain to: %d", enable);
		cam_sensor->status.awb_gain = enable;
	}

	return ret;
}

static int set_special_effect(esp_cam_sensor_io_parl_handle_t cam_sensor, int effect) {
	int ret = 0;

	if (effect < 0 || effect > 6) {
		return -1;
	}

	uint8_t *regs = (uint8_t *)sensor_special_effects[effect];
	ret = write_reg(cam_sensor->sccb_address, 0x32F1, regs[0]) ||
		  write_reg(cam_sensor->sccb_address, 0x32F4, regs[1]) ||
		  write_reg(cam_sensor->sccb_address, 0x32F5, regs[2]) ||
		  write_reg(cam_sensor->sccb_address, 0x3060, regs[3]);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set special_effect to: %d", effect);
		cam_sensor->status.special_effect = effect;
	}

	return ret;
}

static int set_brightness(esp_cam_sensor_io_parl_handle_t cam_sensor, int level) {
	int ret = 0;
	uint8_t value = 0;

	switch (level) {
	case 3:
		value = 0xA0;
		break;

	case 2:
		value = 0x90;
		break;

	case 1:
		value = 0x88;
		break;

	case -1:
		value = 0x78;
		break;

	case -2:
		value = 0x70;
		break;

	case -3:
		value = 0x60;
		break;

	default: // 0
		break;
	}

	ret = write_reg(cam_sensor->sccb_address, 0x32F2, value);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set brightness to: %d", level);
		cam_sensor->status.brightness = level;
	}

	return ret;
}

static int set_contrast(esp_cam_sensor_io_parl_handle_t cam_sensor, int level) {
	int ret = 0;
	uint8_t value1 = 0, value2 = 0;

	switch (level) {
	case 3:
		value1 = 0xD0;
		value2 = 0xB0;
		break;

	case 2:
		value1 = 0xE0;
		value2 = 0xA0;
		break;

	case 1:
		value1 = 0xF0;
		value2 = 0x90;
		break;

	case 0:
		value1 = 0x00;
		value2 = 0x80;
		break;

	case -1:
		value1 = 0x10;
		value2 = 0x70;
		break;

	case -2:
		value1 = 0x20;
		value2 = 0x60;
		break;

	case -3:
		value1 = 0x30;
		value2 = 0x50;
		break;

	default: // 0
		break;
	}

	ret = write_reg(cam_sensor->sccb_address, 0x32FC, value1);
	ret = write_reg(cam_sensor->sccb_address, 0x32F2, value2);
	ret = write_reg(cam_sensor->sccb_address, 0x3060, 0x01);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set contrast to: %d", level);
		cam_sensor->status.contrast = level;
	}

	return ret;
}

static int set_saturation(esp_cam_sensor_io_parl_handle_t cam_sensor, int level) {
	int ret = 0;

	if (level > 4 || level < -4) {
		return -1;
	}

	uint8_t *regs = (uint8_t *)sensor_saturation_levels[level + 4];
	{
		ret = write_reg(cam_sensor->sccb_address, 0x32F3, regs[0]);

		if (ret) {
			return ret;
		}
	}

	if (ret == 0) {
		ESP_LOGD(TAG, "Set saturation to: %d", level);
		cam_sensor->status.saturation = level;
	}

	return ret;
}

static int set_sharpness(esp_cam_sensor_io_parl_handle_t cam_sensor, int level) {
	int ret = 0;

	if (level > 3 || level < -3) {
		return -1;
	}

	uint8_t mt_offset_2 = (level + 3) * 8;
	uint8_t mt_offset_1 = mt_offset_2 + 1;

	ret = write_reg_bits(cam_sensor->sccb_address, 0x5308, 0x40,
						 false) // 0x40 means auto
		  || write_reg(cam_sensor->sccb_address, 0x5300, 0x10) ||
		  write_reg(cam_sensor->sccb_address, 0x5301, 0x10) ||
		  write_reg(cam_sensor->sccb_address, 0x5302, mt_offset_1) ||
		  write_reg(cam_sensor->sccb_address, 0x5303, mt_offset_2) ||
		  write_reg(cam_sensor->sccb_address, 0x5309, 0x10) ||
		  write_reg(cam_sensor->sccb_address, 0x530a, 0x10) ||
		  write_reg(cam_sensor->sccb_address, 0x530b, 0x04) ||
		  write_reg(cam_sensor->sccb_address, 0x530c, 0x06);

	if (ret == 0) {
		ESP_LOGD(TAG, "Set sharpness to: %d", level);
		cam_sensor->status.sharpness = level;
	}

	return ret;
}

static int set_gainceiling(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_gainceiling_t level) {
	ESP_LOGD(TAG, "set_gainceiling can not be configured at present");
	return 0;
}

static int get_denoise(esp_cam_sensor_io_parl_handle_t cam_sensor) {

	return (read_reg(cam_sensor->sccb_address, 0x5306) / 4) + 1;
}

static int set_denoise(esp_cam_sensor_io_parl_handle_t cam_sensor, int level) {
	ESP_LOGD(TAG, "set_denoise can not be configured at present");
	return 0;
}

static int get_reg(esp_cam_sensor_io_parl_handle_t cam_sensor, int reg, int mask) {
	int ret = 0, ret2 = 0;

	if (mask > 0xFF) {
		ret = read_reg16(cam_sensor->sccb_address, reg);

		if (ret >= 0 && mask > 0xFFFF) {
			ret2 = read_reg(cam_sensor->sccb_address, reg + 2);

			if (ret2 >= 0) {
				ret = (ret << 8) | ret2;
			} else {
				ret = ret2;
			}
		}
	} else {
		ret = read_reg(cam_sensor->sccb_address, reg);
	}

	if (ret > 0) {
		ret &= mask;
	}

	return ret;
}

static int set_reg(esp_cam_sensor_io_parl_handle_t cam_sensor, int reg, int mask, int value) {
	int ret = 0, ret2 = 0;

	if (mask > 0xFF) {
		ret = read_reg16(cam_sensor->sccb_address, reg);

		if (ret >= 0 && mask > 0xFFFF) {
			ret2 = read_reg(cam_sensor->sccb_address, reg + 2);

			if (ret2 >= 0) {
				ret = (ret << 8) | ret2;
			} else {
				ret = ret2;
			}
		}
	} else {
		ret = read_reg(cam_sensor->sccb_address, reg);
	}

	if (ret < 0) {
		return ret;
	}

	value = (ret & ~mask) | (value & mask);

	if (mask > 0xFFFF) {
		ret = write_reg16(cam_sensor->sccb_address, reg, value >> 8);

		if (ret >= 0) {
			ret = write_reg(cam_sensor->sccb_address, reg + 2, value & 0xFF);
		}
	} else if (mask > 0xFF) {
		ret = write_reg16(cam_sensor->sccb_address, reg, value);
	} else {
		ret = write_reg(cam_sensor->sccb_address, reg, value);
	}

	return ret;
}

static int set_res_raw(esp_cam_sensor_io_parl_handle_t cam_sensor, int startX, int startY, int endX, int endY, int offsetX, int offsetY, int totalX, int totalY, int outputX, int outputY, bool scale, bool binning) {
	int ret = 0;
	ret =
		write_addr_reg(cam_sensor->sccb_address, X_ADDR_ST_H, startX, startY) ||
		write_addr_reg(cam_sensor->sccb_address, X_ADDR_END_H, endX, endY) ||
		write_addr_reg(cam_sensor->sccb_address, X_OFFSET_H, offsetX, offsetY) ||
		write_addr_reg(cam_sensor->sccb_address, X_TOTAL_SIZE_H, totalX, totalY) ||
		write_addr_reg(cam_sensor->sccb_address, X_OUTPUT_SIZE_H, outputX, outputY);

	if (!ret) {
		cam_sensor->status.scale = scale;
		cam_sensor->status.binning = binning;
		ret = set_image_options(cam_sensor);
	}

	return ret;
}

static int _set_pll(esp_cam_sensor_io_parl_handle_t cam_sensor, int bypass, int multiplier, int sys_div, int root_2x, int pre_div, int seld5, int pclk_manual, int pclk_div) {
	return set_pll(cam_sensor, bypass > 0, multiplier, sys_div, pre_div,
				   root_2x > 0, seld5, pclk_manual > 0, pclk_div);
}

static int set_xclk(esp_cam_sensor_io_parl_handle_t cam_sensor, int timer, int xclk) {
	int ret = 0;
	if (xclk > 10) {
		ESP_LOGE(
			TAG,
			"only XCLK under 10MHz is supported, and XCLK is now set to 10M");
		xclk = 10;
	}
	cam_sensor->xclk_freq_hz = xclk * 1000000U;
	ret = xclk_timer_conf(timer, cam_sensor->xclk_freq_hz);
	return ret;
}

int nt99141_detect(int sccb_address, esp_cam_sensor_io_parl_id_t *id) {
	if (ESP_CAM_IO_PARL_NT99141_SCCB_ADDR == sccb_address) {
		sccb_write16(sccb_address, 0x3008, 0x01); // bank sensor
		uint16_t h = sccb_read16(sccb_address, 0x3000);
		uint16_t l = sccb_read16(sccb_address, 0x3001);
		uint16_t PID = (h << 8) | l;
		if (ESP_CAM_IO_PARL_NT99141_PID == PID) {
			id->PID = PID;
			return PID;
		} else {
			ESP_LOGI(TAG, "Mismatch PID=0x%x", PID);
		}
	}
	return 0;
}

static int init_status(esp_cam_sensor_io_parl_handle_t cam_sensor) {
	cam_sensor->status.brightness = 0;
	cam_sensor->status.contrast = 0;
	cam_sensor->status.saturation = 0;
	cam_sensor->status.sharpness = (read_reg(cam_sensor->sccb_address, 0x3301));
	cam_sensor->status.denoise = get_denoise(cam_sensor);
	cam_sensor->status.ae_level = 0;
	cam_sensor->status.gainceiling =
		read_reg16(cam_sensor->sccb_address, 0x32F0) & 0xFF;
	cam_sensor->status.awb =
		check_reg_mask(cam_sensor->sccb_address, ISP_CONTROL_01, 0x10);
	cam_sensor->status.dcw = !check_reg_mask(cam_sensor->sccb_address, 0x5183, 0x80);
	cam_sensor->status.agc = !check_reg_mask(cam_sensor->sccb_address, AEC_PK_MANUAL,
										 AEC_PK_MANUAL_AGC_MANUALEN);
	cam_sensor->status.aec = !check_reg_mask(cam_sensor->sccb_address, AEC_PK_MANUAL,
										 AEC_PK_MANUAL_AEC_MANUALEN);
	cam_sensor->status.hmirror = check_reg_mask(
		cam_sensor->sccb_address, TIMING_TC_REG21, TIMING_TC_REG21_HMIRROR);
	cam_sensor->status.vflip = check_reg_mask(cam_sensor->sccb_address, TIMING_TC_REG20,
										  TIMING_TC_REG20_VFLIP);
	cam_sensor->status.colorbar = check_reg_mask(
		cam_sensor->sccb_address, PRE_ISP_TEST_SETTING_1, TEST_COLOR_BAR);
	cam_sensor->status.bpc = check_reg_mask(cam_sensor->sccb_address, 0x5000, 0x04);
	cam_sensor->status.wpc = check_reg_mask(cam_sensor->sccb_address, 0x5000, 0x02);
	cam_sensor->status.raw_gma = check_reg_mask(cam_sensor->sccb_address, 0x5000, 0x20);
	cam_sensor->status.lenc = check_reg_mask(cam_sensor->sccb_address, 0x5000, 0x80);
	cam_sensor->status.quality =
		read_reg(cam_sensor->sccb_address, COMPRESSION_CTRL07) & 0x3f;
	cam_sensor->status.special_effect = 0;
	cam_sensor->status.wb_mode = 0;
	cam_sensor->status.awb_gain =
		check_reg_mask(cam_sensor->sccb_address, 0x3000, 0x01);
	cam_sensor->status.agc_gain = get_agc_gain(cam_sensor);
	cam_sensor->status.aec_value = get_aec_value(cam_sensor);
	cam_sensor->status.aec2 = check_reg_mask(cam_sensor->sccb_address, 0x3000, 0x04);
	return 0;
}

int nt99141_init(esp_cam_sensor_io_parl_handle_t cam_sensor) {
	cam_sensor->reset = reset;
	cam_sensor->set_pixformat = set_pixformat;
	cam_sensor->set_framesize = set_framesize;
	cam_sensor->set_contrast = set_contrast;
	cam_sensor->set_brightness = set_brightness;
	cam_sensor->set_saturation = set_saturation;
	cam_sensor->set_sharpness = set_sharpness;
	cam_sensor->set_gainceiling = set_gainceiling;
	cam_sensor->set_quality = set_quality;
	cam_sensor->set_colorbar = set_colorbar;
	cam_sensor->set_gain_ctrl = set_gain_ctrl;
	cam_sensor->set_exposure_ctrl = set_exposure_ctrl;
	cam_sensor->set_whitebal = set_whitebal;
	cam_sensor->set_hmirror = set_hmirror;
	cam_sensor->set_vflip = set_vflip;
	cam_sensor->init_status = init_status;
	cam_sensor->set_aec2 = set_aec2;
	cam_sensor->set_aec_value = set_aec_value;
	cam_sensor->set_special_effect = set_special_effect;
	cam_sensor->set_wb_mode = set_wb_mode;
	cam_sensor->set_ae_level = set_ae_level;
	cam_sensor->set_dcw = set_dcw_dsp;
	cam_sensor->set_bpc = set_bpc_dsp;
	cam_sensor->set_wpc = set_wpc_dsp;
	cam_sensor->set_awb_gain = set_awb_gain_dsp;
	cam_sensor->set_agc_gain = set_agc_gain;
	cam_sensor->set_raw_gma = set_raw_gma_dsp;
	cam_sensor->set_lenc = set_lenc_dsp;
	cam_sensor->set_denoise = set_denoise;

	cam_sensor->get_reg = get_reg;
	cam_sensor->set_reg = set_reg;
	cam_sensor->set_res_raw = set_res_raw;
	cam_sensor->set_pll = _set_pll;
	cam_sensor->set_xclk = set_xclk;
	return 0;
}