/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Sensor abstraction layer.
 *
 */
#ifndef __CAMERA_SENSOR_H__
#define __CAMERA_SENSOR_H__
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OV2640_PID = 0x26,
    OV3660_PID = 0x3660,
    OV5640_PID = 0x5640,
    NT99141_PID = 0x1410,
} camera_pid_t;

typedef enum {
    CAMERA_OV2640,
    CAMERA_OV3660,
    CAMERA_OV5640,
    CAMERA_NT99141,
    CAMERA_MODEL_MAX,
    CAMERA_NONE,
} camera_model_t;

typedef enum {
    OV2640_SCCB_ADDR   = 0x30,// 0x60 >> 1
    OV3660_SCCB_ADDR   = 0x3C,// 0x78 >> 1
    OV5640_SCCB_ADDR   = 0x3C,// 0x78 >> 1
    NT99141_SCCB_ADDR  = 0x2A,// 0x54 >> 1
} camera_sccb_addr_t;

typedef enum {
    PIXFORMAT_RGB565,    // 2BPP/RGB565
    PIXFORMAT_YUV422,    // 2BPP/YUV422
    PIXFORMAT_YUV420,    // 1.5BPP/YUV420
    PIXFORMAT_GRAYSCALE, // 1BPP/GRAYSCALE
    PIXFORMAT_JPEG,      // JPEG/COMPRESSED
    PIXFORMAT_RGB888,    // 3BPP/RGB888
    PIXFORMAT_RAW,       // RAW
    PIXFORMAT_RGB444,    // 3BP2P/RGB444
    PIXFORMAT_RGB555,    // 3BP2P/RGB555
} camera_pixformat_t;

typedef enum {
    FRAMESIZE_96X96,    // 96x96
    FRAMESIZE_128X128,  // 128x128
    FRAMESIZE_QQVGA,    // 160x120
    FRAMESIZE_QCIF,     // 176x144
    FRAMESIZE_HQVGA,    // 240x176
    FRAMESIZE_240X240,  // 240x240
    FRAMESIZE_QVGA,     // 320x240
    FRAMESIZE_320X320,  // 320x320
    FRAMESIZE_CIF,      // 400x296
    FRAMESIZE_HVGA,     // 480x320
    FRAMESIZE_640X360,  // 640x360
    FRAMESIZE_VGA,      // 640x480
    FRAMESIZE_SVGA,     // 800x600
    FRAMESIZE_XGA,      // 1024x768
    FRAMESIZE_HD,       // 1280x720
    FRAMESIZE_SXGAM,    // 1280x960
    FRAMESIZE_SXGA,     // 1280x1024
    FRAMESIZE_UXGA,     // 1600x1200
    // 3MP Sensors
    FRAMESIZE_FHD,      // 1920x1080
    FRAMESIZE_WUXGA,    // 1920x1200
    FRAMESIZE_P_HD,     //  720x1280
    FRAMESIZE_P_3MP,    //  864x1536
    FRAMESIZE_QXGA,     // 2048x1536
    // 5MP Sensors
    FRAMESIZE_2160X1440,// 2160x1440
    FRAMESIZE_QHD,      // 2560x1440
    FRAMESIZE_WQXGA,    // 2560x1600
    FRAMESIZE_P_FHD,    // 1080x1920
    FRAMESIZE_QSXGA,    // 2560x1920
    FRAMESIZE_5MP,      // 2592x1944
    FRAMESIZE_INVALID
} camera_framesize_t;

typedef struct {
    const camera_model_t model;
    const char *name;
    const camera_sccb_addr_t sccb_address;
    const camera_pid_t pid;
    const camera_framesize_t max_size;
    const bool support_jpeg;
} camera_sensor_info_t;

typedef enum {
    ASPECT_RATIO_4X3,
    ASPECT_RATIO_3X2,
    ASPECT_RATIO_16X10,
    ASPECT_RATIO_5X3,
    ASPECT_RATIO_16X9,
    ASPECT_RATIO_21X9,
    ASPECT_RATIO_5X4,
    ASPECT_RATIO_1X1,
    ASPECT_RATIO_9X16
} camera_aspect_ratio_t;

typedef enum {
    GAINCEILING_2X,
    GAINCEILING_4X,
    GAINCEILING_8X,
    GAINCEILING_16X,
    GAINCEILING_32X,
    GAINCEILING_64X,
    GAINCEILING_128X,
} camera_gainceiling_t;

typedef struct {
    uint16_t max_width;
    uint16_t max_height;
    uint16_t start_x;
    uint16_t start_y;
    uint16_t end_x;
    uint16_t end_y;
    uint16_t offset_x;
    uint16_t offset_y;
    uint16_t total_x;
    uint16_t total_y;
} camera_ratio_settings_t;

typedef struct {
    const uint16_t width;
    const uint16_t height;
    const camera_aspect_ratio_t aspect_ratio;
} camera_resolution_info_t;

// Resolution table (in sensor.c)
extern const camera_resolution_info_t camera_resolution[];
// camera sensor table (in sensor.c)
extern const camera_sensor_info_t camera_sensor[];

typedef struct {
    uint8_t MIDH;
    uint8_t MIDL;
    uint16_t PID;
    uint8_t VER;
} camera_sensor_id_t;

typedef struct {
    camera_framesize_t framesize;//0 - 10
    bool scale;
    bool binning;
    uint8_t quality;//0 - 63
    int8_t brightness;//-2 - 2
    int8_t contrast;//-2 - 2
    int8_t saturation;//-2 - 2
    int8_t sharpness;//-2 - 2
    uint8_t denoise;
    uint8_t special_effect;//0 - 6
    uint8_t wb_mode;//0 - 4
    uint8_t awb;
    uint8_t awb_gain;
    uint8_t aec;
    uint8_t aec2;
    int8_t ae_level;//-2 - 2
    uint16_t aec_value;//0 - 1200
    uint8_t agc;
    uint8_t agc_gain;//0 - 30
    uint8_t gainceiling;//0 - 6
    uint8_t bpc;
    uint8_t wpc;
    uint8_t raw_gma;
    uint8_t lenc;
    uint8_t hmirror;
    uint8_t vflip;
    uint8_t dcw;
    uint8_t colorbar;
} camera_status_t;

typedef struct _camera_sensor camera_sensor_t;
typedef struct _camera_sensor {
    camera_sensor_id_t id;          // Sensor ID.
    uint8_t  sccb_address;          // Sensor I2C slave address.
    camera_pixformat_t pixformat;
    camera_status_t status;
    int xclk_freq_hz;

    // Sensor function pointers
    int  (*init_status)         (camera_sensor_t *sensor);
    int  (*reset)               (camera_sensor_t *sensor); // Reset the configuration of the sensor, and return ESP_OK if reset is successful
    int  (*set_pixformat)       (camera_sensor_t *sensor, camera_pixformat_t pixformat);
    int  (*set_framesize)       (camera_sensor_t *sensor, camera_framesize_t framesize);
    int  (*set_contrast)        (camera_sensor_t *sensor, int level);
    int  (*set_brightness)      (camera_sensor_t *sensor, int level);
    int  (*set_saturation)      (camera_sensor_t *sensor, int level);
    int  (*set_sharpness)       (camera_sensor_t *sensor, int level);
    int  (*set_denoise)         (camera_sensor_t *sensor, int level);
    int  (*set_gainceiling)     (camera_sensor_t *sensor, camera_gainceiling_t gainceiling);
    int  (*set_quality)         (camera_sensor_t *sensor, int quality);
    int  (*set_colorbar)        (camera_sensor_t *sensor, int enable);
    int  (*set_whitebal)        (camera_sensor_t *sensor, int enable);
    int  (*set_gain_ctrl)       (camera_sensor_t *sensor, int enable);
    int  (*set_exposure_ctrl)   (camera_sensor_t *sensor, int enable);
    int  (*set_hmirror)         (camera_sensor_t *sensor, int enable);
    int  (*set_vflip)           (camera_sensor_t *sensor, int enable);

    int  (*set_aec2)            (camera_sensor_t *sensor, int enable);
    int  (*set_awb_gain)        (camera_sensor_t *sensor, int enable);
    int  (*set_agc_gain)        (camera_sensor_t *sensor, int gain);
    int  (*set_aec_value)       (camera_sensor_t *sensor, int gain);

    int  (*set_special_effect)  (camera_sensor_t *sensor, int effect);
    int  (*set_wb_mode)         (camera_sensor_t *sensor, int mode);
    int  (*set_ae_level)        (camera_sensor_t *sensor, int level);

    int  (*set_dcw)             (camera_sensor_t *sensor, int enable);
    int  (*set_bpc)             (camera_sensor_t *sensor, int enable);
    int  (*set_wpc)             (camera_sensor_t *sensor, int enable);

    int  (*set_raw_gma)         (camera_sensor_t *sensor, int enable);
    int  (*set_lenc)            (camera_sensor_t *sensor, int enable);

    int  (*get_reg)             (camera_sensor_t *sensor, int reg, int mask);
    int  (*set_reg)             (camera_sensor_t *sensor, int reg, int mask, int value);
    int  (*set_res_raw)         (camera_sensor_t *sensor, int startX, int startY, int endX, int endY, int offsetX, int offsetY, int totalX, int totalY, int outputX, int outputY, bool scale, bool binning);
    int  (*set_pll)             (camera_sensor_t *sensor, int bypass, int mul, int sys, int root, int pre, int seld5, int pclken, int pclk);
    int  (*set_xclk)            (camera_sensor_t *sensor, int timer, int xclk);
} camera_sensor_t;

camera_sensor_info_t *esp_camera_sensor_get_info(camera_sensor_id_t *id);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_SENSOR_H__ */