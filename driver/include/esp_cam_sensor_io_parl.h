#ifndef _ESP_CAM_SENSOR_IO_PARL_H_
#define _ESP_CAM_SENSOR_IO_PARL_H_

#pragma once

#include "esp_err.h"
#include "soc/gpio_num.h"
#include "hal/ledc_types.h"
#include "esp_cam_io_parl.h"

typedef enum {
    ESP_CAM_IO_PARL_OV2640_PID = 0x26,
    ESP_CAM_IO_PARL_OV3660_PID = 0x3660,
    ESP_CAM_IO_PARL_OV5640_PID = 0x5640,
    ESP_CAM_IO_PARL_NT99141_PID = 0x1410,
} esp_cam_sensor_io_parl_pid_t;

typedef enum {
    ESP_CAM_IO_PARL_OV2640,
    ESP_CAM_IO_PARL_OV3660,
    ESP_CAM_IO_PARL_OV5640,
    ESP_CAM_IO_PARL_NT99141,
    ESP_CAM_IO_PARL_MODEL_MAX,
    ESP_CAM_IO_PARL_MODEL_NONE,
} esp_cam_sensor_io_parl_model_t;

typedef enum {
    ESP_CAM_IO_PARL_OV2640_SCCB_ADDR   = 0x30,// 0x60 >> 1
    ESP_CAM_IO_PARL_OV3660_SCCB_ADDR   = 0x3C,// 0x78 >> 1
    ESP_CAM_IO_PARL_OV5640_SCCB_ADDR   = 0x3C,// 0x78 >> 1
    ESP_CAM_IO_PARL_NT99141_SCCB_ADDR  = 0x2A,// 0x54 >> 1
} esp_cam_sensor_io_parl_sccb_addr_t;

typedef enum {
    ESP_CAM_IO_PARL_PIXFORMAT_RGB565,    // 2BPP/RGB565
    ESP_CAM_IO_PARL_PIXFORMAT_YUV422,    // 2BPP/YUV422
    ESP_CAM_IO_PARL_PIXFORMAT_YUV420,    // 1.5BPP/YUV420
    ESP_CAM_IO_PARL_PIXFORMAT_GRAYSCALE, // 1BPP/GRAYSCALE
    ESP_CAM_IO_PARL_PIXFORMAT_JPEG,      // JPEG/COMPRESSED
    ESP_CAM_IO_PARL_PIXFORMAT_RGB888,    // 3BPP/RGB888
    ESP_CAM_IO_PARL_PIXFORMAT_RAW,       // RAW
    ESP_CAM_IO_PARL_PIXFORMAT_RGB444,    // 3BP2P/RGB444
    ESP_CAM_IO_PARL_PIXFORMAT_RGB555,    // 3BP2P/RGB555
} esp_cam_sensor_io_parl_pixformat_t;

typedef enum {
    ESP_CAM_IO_PARL_FRAMESIZE_96X96,    // 96x96
    ESP_CAM_IO_PARL_FRAMESIZE_128X128,  // 128x128
    ESP_CAM_IO_PARL_FRAMESIZE_QQVGA,    // 160x120
    ESP_CAM_IO_PARL_FRAMESIZE_QCIF,     // 176x144
    ESP_CAM_IO_PARL_FRAMESIZE_HQVGA,    // 240x176
    ESP_CAM_IO_PARL_FRAMESIZE_240X240,  // 240x240
    ESP_CAM_IO_PARL_FRAMESIZE_QVGA,     // 320x240
    ESP_CAM_IO_PARL_FRAMESIZE_320X320,  // 320x320
    ESP_CAM_IO_PARL_FRAMESIZE_CIF,      // 400x296
    ESP_CAM_IO_PARL_FRAMESIZE_HVGA,     // 480x320
    ESP_CAM_IO_PARL_FRAMESIZE_640X360,  // 640x360
    ESP_CAM_IO_PARL_FRAMESIZE_VGA,      // 640x480
    ESP_CAM_IO_PARL_FRAMESIZE_SVGA,     // 800x600
    ESP_CAM_IO_PARL_FRAMESIZE_XGA,      // 1024x768
    ESP_CAM_IO_PARL_FRAMESIZE_HD,       // 1280x720
    ESP_CAM_IO_PARL_FRAMESIZE_SXGAM,    // 1280x960
    ESP_CAM_IO_PARL_FRAMESIZE_SXGA,     // 1280x1024
    ESP_CAM_IO_PARL_FRAMESIZE_UXGA,     // 1600x1200
    // 3MP Sensors
    ESP_CAM_IO_PARL_FRAMESIZE_FHD,      // 1920x1080
    ESP_CAM_IO_PARL_FRAMESIZE_WUXGA,    // 1920x1200
    ESP_CAM_IO_PARL_FRAMESIZE_P_HD,     //  720x1280
    ESP_CAM_IO_PARL_FRAMESIZE_P_3MP,    //  864x1536
    ESP_CAM_IO_PARL_FRAMESIZE_QXGA,     // 2048x1536
    // 5MP Sensors
    ESP_CAM_IO_PARL_FRAMESIZE_QHD,      // 2560x1440
    ESP_CAM_IO_PARL_FRAMESIZE_WQXGA,    // 2560x1600
    ESP_CAM_IO_PARL_FRAMESIZE_P_FHD,    // 1080x1920
    ESP_CAM_IO_PARL_FRAMESIZE_QSXGA,    // 2560x1920
    ESP_CAM_IO_PARL_FRAMESIZE_5MP,      // 2592x1944
    ESP_CAM_IO_PARL_FRAMESIZE_INVALID
} esp_cam_sensor_io_parl_framesize_t;

typedef struct {
    const esp_cam_sensor_io_parl_model_t model;
    const char *name;
    const esp_cam_sensor_io_parl_sccb_addr_t sccb_address;
    const esp_cam_sensor_io_parl_pid_t pid;
    const esp_cam_sensor_io_parl_framesize_t max_size;
    const bool support_jpeg;
} esp_cam_sensor_io_parl_info_t;

typedef enum {
    ESP_CAM_IO_PARL_ASPECT_RATIO_4X3,
    ESP_CAM_IO_PARL_ASPECT_RATIO_3X2,
    ESP_CAM_IO_PARL_ASPECT_RATIO_16X10,
    ESP_CAM_IO_PARL_ASPECT_RATIO_5X3,
    ESP_CAM_IO_PARL_ASPECT_RATIO_16X9,
    ESP_CAM_IO_PARL_ASPECT_RATIO_21X9,
    ESP_CAM_IO_PARL_ASPECT_RATIO_5X4,
    ESP_CAM_IO_PARL_ASPECT_RATIO_1X1,
    ESP_CAM_IO_PARL_ASPECT_RATIO_9X16
} esp_cam_sensor_io_parl_aspect_ratio_t;

typedef enum {
    ESP_CAM_IO_PARL_GAINCEILING_2X,
    ESP_CAM_IO_PARL_GAINCEILING_4X,
    ESP_CAM_IO_PARL_GAINCEILING_8X,
    ESP_CAM_IO_PARL_GAINCEILING_16X,
    ESP_CAM_IO_PARL_GAINCEILING_32X,
    ESP_CAM_IO_PARL_GAINCEILING_64X,
    ESP_CAM_IO_PARL_GAINCEILING_128X,
} esp_cam_sensor_io_parl_gainceiling_t;

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
} esp_cam_sensor_io_parl_ratio_settings_t;

typedef struct {
    const uint16_t width;
    const uint16_t height;
    const esp_cam_sensor_io_parl_aspect_ratio_t aspect_ratio;
} esp_cam_sensor_io_parl_resolution_info_t;

// Resolution table
extern const esp_cam_sensor_io_parl_resolution_info_t esp_cam_sensor_io_parl_resolution[];
// camera sensor table
extern const esp_cam_sensor_io_parl_info_t esp_cam_sensor_io_parl_sensor[];

typedef struct {
    uint8_t MIDH;
    uint8_t MIDL;
    uint16_t PID;
    uint8_t VER;
} esp_cam_sensor_io_parl_id_t;

typedef struct {
    esp_cam_sensor_io_parl_framesize_t framesize;
    bool scale;
    bool binning;
    uint8_t quality; //0 - 63
    int8_t brightness; //-2 - 2
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
} esp_cam_sensor_io_parl_status_t;

/**
 * @brief Configuration structure for camera sensor initialization
 */
typedef struct {
    gpio_num_t pwdn_io;             /*!< GPIO pin for camera power down line */
    gpio_num_t reset_io;            /*!< GPIO pin for camera reset line */
    gpio_num_t xclk_io;             /*!< GPIO pin for camera XCLK line */

    gpio_num_t sda_io;              /*!< GPIO pin for camera SDA line */
    gpio_num_t scl_io;              /*!< GPIO pin for camera SCL line */

    uint32_t xclk_hz;               /*!< Frequency of XCLK signal, in Hz */

    esp_cam_sensor_io_parl_pixformat_t pixel_format;       /*!< Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG  */
    esp_cam_sensor_io_parl_framesize_t frame_size;         /*!< Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA  */

    ledc_timer_t ledc_timer;        /*!< LEDC timer to be used for generating XCLK  */
    ledc_channel_t ledc_channel;    /*!< LEDC channel to be used for generating XCLK  */

    int jpeg_quality;               /*!< Quality of JPEG output. 0-63 lower means higher quality  */
    int i2c_port;                   /*!< If pin_sccb_sda is -1, use the already configured I2C bus by number */
} esp_cam_sensor_io_parl_config_t;

typedef struct esp_cam_sensor_io_parl_t esp_cam_sensor_io_parl_t;

/**
 * @brief esp_cam_sensor_io_parl handle
 */
typedef struct esp_cam_sensor_io_parl_t *esp_cam_sensor_io_parl_handle_t;

typedef struct esp_cam_sensor_io_parl_t {
    esp_cam_sensor_io_parl_id_t id;  // Sensor ID.
    uint8_t  sccb_address;  // Sensor I2C slave address.
    esp_cam_sensor_io_parl_pixformat_t pixformat;
    int pixformat_bpp;
    esp_cam_sensor_io_parl_status_t status;
    int xclk_freq_hz;

    esp_cam_io_parl_handle_t dvp_interface;

    // Sensor function pointers
    int  (*init_status)         (esp_cam_sensor_io_parl_handle_t cam_sensor);
    int  (*reset)               (esp_cam_sensor_io_parl_handle_t cam_sensor); // Reset the configuration of the sensor, and return ESP_OK if reset is successful
    int  (*set_pixformat)       (esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_pixformat_t pixformat);
    int  (*set_framesize)       (esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_framesize_t framesize);
    int  (*set_contrast)        (esp_cam_sensor_io_parl_handle_t cam_sensor, int level);
    int  (*set_brightness)      (esp_cam_sensor_io_parl_handle_t cam_sensor, int level);
    int  (*set_saturation)      (esp_cam_sensor_io_parl_handle_t cam_sensor, int level);
    int  (*set_sharpness)       (esp_cam_sensor_io_parl_handle_t cam_sensor, int level);
    int  (*set_denoise)         (esp_cam_sensor_io_parl_handle_t cam_sensor, int level);
    int  (*set_gainceiling)     (esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_gainceiling_t gainceiling);
    int  (*set_quality)         (esp_cam_sensor_io_parl_handle_t cam_sensor, int quality);
    int  (*set_colorbar)        (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_whitebal)        (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_gain_ctrl)       (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_exposure_ctrl)   (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_hmirror)         (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_vflip)           (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);

    int  (*set_aec2)            (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_awb_gain)        (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_agc_gain)        (esp_cam_sensor_io_parl_handle_t cam_sensor, int gain);
    int  (*set_aec_value)       (esp_cam_sensor_io_parl_handle_t cam_sensor, int gain);

    int  (*set_special_effect)  (esp_cam_sensor_io_parl_handle_t cam_sensor, int effect);
    int  (*set_wb_mode)         (esp_cam_sensor_io_parl_handle_t cam_sensor, int mode);
    int  (*set_ae_level)        (esp_cam_sensor_io_parl_handle_t cam_sensor, int level);

    int  (*set_dcw)             (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_bpc)             (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_wpc)             (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);

    int  (*set_raw_gma)         (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);
    int  (*set_lenc)            (esp_cam_sensor_io_parl_handle_t cam_sensor, int enable);

    int  (*get_reg)             (esp_cam_sensor_io_parl_handle_t cam_sensor, int reg, int mask);
    int  (*set_reg)             (esp_cam_sensor_io_parl_handle_t cam_sensor, int reg, int mask, int value);
    int  (*set_res_raw)         (esp_cam_sensor_io_parl_handle_t cam_sensor, int startX, int startY, int endX, int endY, int offsetX, int offsetY, int totalX, int totalY, int outputX, int outputY, bool scale, bool binning);
    int  (*set_pll)             (esp_cam_sensor_io_parl_handle_t cam_sensor, int bypass, int mul, int sys, int root, int pre, int seld5, int pclken, int pclk);
    int  (*set_xclk)            (esp_cam_sensor_io_parl_handle_t cam_sensor, int timer, int xclk);
} esp_cam_sensor_io_parl_t;

/**
 * @brief Creates a new esp_cam_sensor_io_parl handle by initializing the camera driver
 *
 * This function detects and configures camera over I2C interface,
 *
 * Currently this function can only be called once and there is
 * no way to de-initialize this module.
 *
 * @param[in]  config       esp_cam_sensor_io_parl configuration
 * @param[out] ret_handle   Returned esp_cam_sensor_io_parl handle
 * @return
 *      - ESP_ERR_INVALID_STATE     Camera sensor interface has already initialized
 *      - ESP_ERR_NOT_SUPPORTED     Camera is not supported and it does not support JPEG if the current format is selected as JPEG
 *      - ESP_ERR_NOT_ALLOWED       Failed to set the camera sensor frame size
 *      - ESP_ERR_NO_MEM            Not enough memory for the esp_cam_sensor_io_parl resources
 *      - ESP_OK                    Success on allocating esp_cam_sensor_io_parl
 */
esp_err_t esp_cam_new_sensor_io_parl(const esp_cam_sensor_io_parl_config_t *config, esp_cam_sensor_io_parl_handle_t *ret_handle);

/**
 * @brief Deinitialize the camera driver
 *
 * @return
 *      - ESP_ERR_NOT_FOUND         The camera sensor interface does not exist as it wasn't initialized
 *      - ESP_OK                    Success on removing esp_cam_sensor_io_parl
 */
esp_err_t esp_cam_del_sensor_io_parl(void);

/**
 * @brief Gets the pointer to the sensor control interface if it wasn't grabbed on initialization
 *
 * @param[out] esp_cam_sensor_io_parl   Returned esp_cam_sensor_io_parl handle
 * @return
 *      - ESP_ERR_NOT_FOUND         The camera sensor interface does not exist as it wasn't initialized
 *      - ESP_OK                    Success on allocating esp_cam_sensor_io_parl handle
 */
esp_err_t esp_cam_sensor_io_parl_get_interface(esp_cam_sensor_io_parl_handle_t *esp_cam_sensor_io_parl);

/**
 * @brief Get frame resolution information for manual frame allocation
 *
 * @param[out]  out_width    Pointer to frame width
 * @param[out]  out_height   Pointer to frame height
 * @return
 *      - ESP_ERR_NOT_FOUND         The camera sensor interface does not exist as it wasn't initialized
 *      - ESP_OK                    Success on receiving the frame size
 */
esp_err_t esp_cam_sensor_io_parl_frame_info(int *out_width, int *out_height);

/**
 * @brief Connect the DVP port to the camera sensor interface
 *
 * @param[in]  esp_cam_io_parl   esp_cam_io_parl handle that was created
 * @return
 *      - ESP_ERR_NOT_FOUND         The camera sensor interface does not exist as it wasn't initialized or an empty esp_cam_io_parl handle
 *      - ESP_ERR_INVALID_STATE     DVP port has already been connected or it is not enabled
 *      - ESP_OK                    Success on attaching the DVP port
 */
esp_err_t esp_cam_sensor_io_parl_connect(esp_cam_io_parl_handle_t esp_cam_io_parl);

/**
 * @brief Disconnect the DVP port from the camera sensor interface
 *
 * @return
 *      - ESP_ERR_NOT_FOUND         The camera sensor interface does not exist as it wasn't initialized
 *      - ESP_ERR_INVALID_STATE     DVP port has already been disconnected
 *      - ESP_OK                    Success on disconnecting the DVP port
 */
esp_err_t esp_cam_sensor_io_parl_disconnect(void);

/**
 * @brief Save camera settings to non-volatile-storage (NVS)
 *
 * @param[in] key   A unique nvs key name for the camera settings
 */
esp_err_t esp_cam_sensor_io_parl_save_to_nvs(const char *key);

/**
 * @brief Load camera settings from non-volatile-storage (NVS)
 *
 * @param[in] key   A unique nvs key name for the camera settings
 */
esp_err_t esp_cam_sensor_io_parl_load_from_nvs(const char *key);

/**
 * @brief Delete camera seetings from non-volatile-storage (NVS)
 *
 * @param[in] key   A unique nvs key name for the camera settings
 */
esp_err_t esp_cam_sensor_io_parl_erase_nvs(const char *key);
#endif