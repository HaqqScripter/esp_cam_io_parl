#include "esp_cam_sensor_io_parl.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sccb.h"
#include "sdkconfig.h"
#include "time.h"
#include "xclk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_ESP_CAM_IO_PARL_OV2640
#include "ov2640.h"
#endif
#if CONFIG_ESP_CAM_IO_PARL_OV3660
#include "ov3660.h"
#endif
#if CONFIG_ESP_CAM_IO_PARL_OV5640
#include "ov5640.h"
#endif
#if CONFIG_ESP_CAM_IO_PARL_NT99141
#include "nt99141.h"
#endif

static const char *TAG = "esp_cam_sensor_io_parl";

const esp_cam_sensor_io_parl_info_t esp_cam_sensor_io_parl_sensor[ESP_CAM_IO_PARL_MODEL_MAX] = {
    // The sequence must be consistent with camera_model_t
    {ESP_CAM_IO_PARL_OV2640, "OV2640", ESP_CAM_IO_PARL_OV2640_SCCB_ADDR, ESP_CAM_IO_PARL_OV2640_PID, ESP_CAM_IO_PARL_FRAMESIZE_UXGA, true},
    {ESP_CAM_IO_PARL_OV3660, "OV3660", ESP_CAM_IO_PARL_OV3660_SCCB_ADDR, ESP_CAM_IO_PARL_OV3660_PID, ESP_CAM_IO_PARL_FRAMESIZE_QXGA, true},
    {ESP_CAM_IO_PARL_OV5640, "OV5640", ESP_CAM_IO_PARL_OV5640_SCCB_ADDR, ESP_CAM_IO_PARL_OV5640_PID, ESP_CAM_IO_PARL_FRAMESIZE_5MP, true},
    {ESP_CAM_IO_PARL_NT99141, "NT99141", ESP_CAM_IO_PARL_NT99141_SCCB_ADDR, ESP_CAM_IO_PARL_NT99141_PID, ESP_CAM_IO_PARL_FRAMESIZE_HD, true},
};

const esp_cam_sensor_io_parl_resolution_info_t esp_cam_sensor_io_parl_resolution[ESP_CAM_IO_PARL_FRAMESIZE_INVALID] = {
    {   96,   96, ESP_CAM_IO_PARL_ASPECT_RATIO_1X1   }, /* 96x96 */
    {  128,  128, ESP_CAM_IO_PARL_ASPECT_RATIO_1X1   }, /* 128x128 */
    {  160,  120, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* QQVGA */
    {  176,  144, ESP_CAM_IO_PARL_ASPECT_RATIO_5X4   }, /* QCIF  */
    {  240,  176, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* HQVGA */
    {  240,  240, ESP_CAM_IO_PARL_ASPECT_RATIO_1X1   }, /* 240x240 */
    {  320,  240, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* QVGA  */
    {  320,  320, ESP_CAM_IO_PARL_ASPECT_RATIO_1X1   }, /* 320x320 */
    {  400,  296, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* CIF   */
    {  480,  320, ESP_CAM_IO_PARL_ASPECT_RATIO_3X2   }, /* HVGA  */
    {  640,  360, ESP_CAM_IO_PARL_ASPECT_RATIO_16X9  }, /* 640x360 */
    {  640,  480, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* VGA   */
    {  800,  600, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* SVGA  */
    { 1024,  768, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* XGA   */
    { 1280,  720, ESP_CAM_IO_PARL_ASPECT_RATIO_16X9  }, /* HD    */
    { 1280,  960, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* SXGAM */
    { 1280, 1024, ESP_CAM_IO_PARL_ASPECT_RATIO_5X4   }, /* SXGA  */
    { 1600, 1200, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* UXGA  */
    // 3MP Sensors
    { 1920, 1080, ESP_CAM_IO_PARL_ASPECT_RATIO_16X9  }, /* FHD   */
    { 1920, 1200, ESP_CAM_IO_PARL_ASPECT_RATIO_16X10 }, /* WUXGA  */
    {  720, 1280, ESP_CAM_IO_PARL_ASPECT_RATIO_9X16  }, /* Portrait HD   */
    {  864, 1536, ESP_CAM_IO_PARL_ASPECT_RATIO_9X16  }, /* Portrait 3MP   */
    { 2048, 1536, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* QXGA  */
    // 5MP Sensors
    { 2560, 1440, ESP_CAM_IO_PARL_ASPECT_RATIO_16X9  }, /* QHD    */
    { 2560, 1600, ESP_CAM_IO_PARL_ASPECT_RATIO_16X10 }, /* WQXGA  */
    { 1088, 1920, ESP_CAM_IO_PARL_ASPECT_RATIO_9X16  }, /* Portrait FHD   */
    { 2560, 1920, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* QSXGA  */
    { 2592, 1944, ESP_CAM_IO_PARL_ASPECT_RATIO_4X3   }, /* 5MP */
};

esp_cam_sensor_io_parl_info_t *esp_cam_sensor_io_parl_get_info(esp_cam_sensor_io_parl_id_t *id) {
    for (int i = 0; i < ESP_CAM_IO_PARL_MODEL_MAX; i++) {
        if (id->PID == esp_cam_sensor_io_parl_sensor[i].pid) {
            return (esp_cam_sensor_io_parl_info_t *)&esp_cam_sensor_io_parl_sensor[i];
        }
    }
    return NULL;
}

static const char *ESP_CAM_SENSOR_IO_PARL_NVS_KEY = "esp_cam_sensor_io_parl";
static const char *ESP_CAM_SENSOR_IO_PARL_PIXFORMAT_NVS_KEY = "esp_cam_sensor_io_parl_pixformat";

static esp_cam_sensor_io_parl_handle_t esp_cam_sensor_io_parl_interface = NULL;

typedef struct {
    int (*detect)(int sccb_address, esp_cam_sensor_io_parl_id_t *id);
    int (*init)(esp_cam_sensor_io_parl_handle_t cam_sensor);
} esp_cam_sensor_io_parl_func_t;

static const esp_cam_sensor_io_parl_func_t g_sensors[] = {
#if CONFIG_ESP_CAM_IO_PARL_OV2640
    {ov2640_detect, ov2640_init},
#endif
#if CONFIG_ESP_CAM_IO_PARL_OV3660
    {ov3660_detect, ov3660_init},
#endif
#if CONFIG_ESP_CAM_IO_PARL_OV5640
    {ov5640_detect, ov5640_init},
#endif
#if CONFIG_ESP_CAM_IO_PARL_NT99141
    {nt99141_detect, nt99141_init},
#endif
};

typedef struct esp_cam_sensor_io_parl_controlled_t {
    int (*set_pixformat) (esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_pixformat_t pixformat);
    int (*set_framesize) (esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_framesize_t framesize);
    int (*set_res_raw) (esp_cam_sensor_io_parl_handle_t cam_sensor, int startX, int startY, int endX, int endY, int offsetX, int offsetY, int totalX, int totalY, int outputX, int outputY, bool scale, bool binning);
} esp_cam_sensor_io_parl_controlled_t;

static esp_cam_sensor_io_parl_controlled_t *esp_cam_sensor_io_parl_controlled_interface = NULL;

static int set_framesize_io_parl_interface(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_sensor_io_parl_framesize_t framesize) {
    if (esp_cam_sensor_io_parl_controlled_interface && cam_sensor->dvp_interface) {
        esp_cam_sensor_io_parl_resolution_info_t resolution = esp_cam_sensor_io_parl_resolution[framesize];
        uint32_t alloc_size = cam_sensor->pixformat != ESP_CAM_IO_PARL_PIXFORMAT_JPEG ? resolution.width * resolution.height * cam_sensor->pixformat_bpp : (resolution.width * resolution.height * CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_MUL / CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_DIV + CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_PADDING);
        esp_cam_io_parl_set_alloc_size(cam_sensor->dvp_interface, alloc_size, cam_sensor->dvp_interface->alloc_heap_caps);

        return esp_cam_sensor_io_parl_controlled_interface->set_framesize(cam_sensor, framesize);
    }
    return cam_sensor->set_framesize(cam_sensor, framesize);
}

static int set_res_raw_io_parl_interface(esp_cam_sensor_io_parl_handle_t cam_sensor, int startX, int startY, int endX, int endY, int offsetX, int offsetY, int totalX, int totalY, int outputX, int outputY, bool scale, bool binning) {
    if (esp_cam_sensor_io_parl_controlled_interface && cam_sensor->dvp_interface) {
        uint32_t alloc_size = cam_sensor->pixformat != ESP_CAM_IO_PARL_PIXFORMAT_JPEG ? outputX * outputY * cam_sensor->pixformat_bpp : (outputX * outputY * CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_MUL / CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_DIV + CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_PADDING);
        esp_cam_io_parl_set_alloc_size(cam_sensor->dvp_interface, alloc_size, cam_sensor->dvp_interface->alloc_heap_caps);

        return esp_cam_sensor_io_parl_controlled_interface->set_res_raw(cam_sensor, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
    }
    return cam_sensor->set_res_raw(cam_sensor, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
}

static esp_err_t esp_cam_sensor_io_parl_probe(const esp_cam_sensor_io_parl_config_t *config, esp_cam_sensor_io_parl_model_t *out_camera_model) {
    esp_err_t ret = ESP_OK;
    *out_camera_model = ESP_CAM_IO_PARL_MODEL_NONE;

    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface == NULL, ESP_ERR_INVALID_STATE, TAG, "Only one camera sensor interface allowed. Please delete the current interface to use a new one");

    esp_cam_sensor_io_parl_handle_t esp_cam_sensor_io_parl = heap_caps_calloc(1, sizeof(esp_cam_sensor_io_parl_t), MALLOC_CAP_DEFAULT);
    if (!esp_cam_sensor_io_parl) {
        return ESP_ERR_NO_MEM;
    }

    if (config->xclk_io >= 0) {
        ESP_LOGD(TAG, "Enabling XCLK output");
        camera_enable_out_clock(config);
    }

    if (config->sda_io != -1) {
        ESP_LOGD(TAG, "Initializing SCCB");
        ret = sccb_init(config->sda_io, config->scl_io);
    } else {
        ESP_LOGD(TAG, "Using existing I2C port");
        ret = sccb_use_port(config->i2c_port);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SCCB init error");
        goto err;
    }

    if (config->pwdn_io >= 0) {
        ESP_LOGD(TAG, "Resetting camera by power down line");
        gpio_config_t conf = {0};
        conf.pin_bit_mask = 1LL << config->pwdn_io;
        conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&conf);

        // carefull, logic is inverted compared to reset pin
        gpio_set_level(config->pwdn_io, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        gpio_set_level(config->pwdn_io, 0);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (config->reset_io >= 0) {
        ESP_LOGD(TAG, "Resetting camera");
        gpio_config_t conf = {0};
        conf.pin_bit_mask = 1LL << config->reset_io;
        conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&conf);

        gpio_set_level(config->reset_io, 0);
        vTaskDelay(10 / portTICK_PERIOD_MS);
        gpio_set_level(config->reset_io, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(TAG, "Searching for camera address");
    vTaskDelay(10 / portTICK_PERIOD_MS);

    uint8_t sccb_address = sccb_probe();
    if (sccb_address == 0) {
        ret = ESP_ERR_NOT_FOUND;
        goto err;
    }

    ESP_LOGI(TAG, "Detected camera at address=0x%02x", sccb_address);
    esp_cam_sensor_io_parl->sccb_address = sccb_address;
    esp_cam_sensor_io_parl->xclk_freq_hz = config->xclk_hz;

    /**
     * Read sensor ID and then initialize sensor
     * Attention: Some sensors have the same SCCB address. Therefore, several
     * attempts may be made in the detection process
     */
    esp_cam_sensor_io_parl_id_t *id = &esp_cam_sensor_io_parl->id;
    for (size_t i = 0; i < sizeof(g_sensors) / sizeof(esp_cam_sensor_io_parl_func_t); i++) {
        if (g_sensors[i].detect(sccb_address, id)) {
            esp_cam_sensor_io_parl_info_t *info = esp_cam_sensor_io_parl_get_info(id);
            if (NULL != info) {
                *out_camera_model = info->model;
                ESP_LOGI(TAG, "Detected %s camera", info->name);
                g_sensors[i].init(esp_cam_sensor_io_parl);
                break;
            }
        }
    }

    if (ESP_CAM_IO_PARL_MODEL_NONE == *out_camera_model) { // If no supported sensors are detected
        ESP_LOGE(TAG, "Detected camera not supported.");
        ret = ESP_ERR_NOT_SUPPORTED;
        goto err;
    }

    ESP_LOGI(TAG, "Camera PID=0x%02x VER=0x%02x MIDL=0x%02x MIDH=0x%02x", id->PID, id->VER, id->MIDH, id->MIDL);

    ESP_LOGD(TAG, "Doing SW reset of sensor");
    vTaskDelay(10 / portTICK_PERIOD_MS);

    esp_cam_sensor_io_parl_interface = esp_cam_sensor_io_parl;

    return esp_cam_sensor_io_parl_interface->reset(esp_cam_sensor_io_parl);
err:
    camera_disable_out_clock();
    return ret;
}

esp_err_t esp_cam_new_sensor_io_parl(const esp_cam_sensor_io_parl_config_t *config, esp_cam_sensor_io_parl_handle_t *ret_handle) {
    esp_err_t err;

    esp_cam_sensor_io_parl_model_t camera_model = ESP_CAM_IO_PARL_MODEL_NONE;
    err = esp_cam_sensor_io_parl_probe(config, &camera_model);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x(%s)", err, esp_err_to_name(err));
        goto fail;
    }

    esp_cam_sensor_io_parl_framesize_t frame_size = (esp_cam_sensor_io_parl_framesize_t)config->frame_size;
    esp_cam_sensor_io_parl_pixformat_t pix_format = (esp_cam_sensor_io_parl_pixformat_t)config->pixel_format;

    if (ESP_CAM_IO_PARL_PIXFORMAT_JPEG == pix_format && (!esp_cam_sensor_io_parl_sensor[camera_model].support_jpeg)) {
        ESP_LOGE(TAG, "JPEG format is not supported on this sensor");
        err = ESP_ERR_NOT_SUPPORTED;
        goto fail;
    }

    if (frame_size > esp_cam_sensor_io_parl_sensor[camera_model].max_size) {
        ESP_LOGW(TAG, "The frame size exceeds the maximum for this sensor, it will be forced to the maximum possible value");
        frame_size = esp_cam_sensor_io_parl_sensor[camera_model].max_size;
    }

    esp_cam_sensor_io_parl_interface->status.framesize = frame_size;
    esp_cam_sensor_io_parl_interface->pixformat = pix_format;

    ESP_LOGD(TAG, "Setting frame size to %dx%d", esp_cam_sensor_io_parl_resolution[frame_size].width, esp_cam_sensor_io_parl_resolution[frame_size].height);
    if (esp_cam_sensor_io_parl_interface->set_framesize(esp_cam_sensor_io_parl_interface, frame_size) != 0) {
        ESP_LOGE(TAG, "Failed to set frame size");
        err = ESP_ERR_NOT_ALLOWED;
        goto fail;
    }

#if CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_AUTO
    esp_cam_sensor_io_parl_controlled_interface = heap_caps_calloc(1, sizeof(esp_cam_sensor_io_parl_controlled_t), MALLOC_CAP_DEFAULT);
    if (!esp_cam_sensor_io_parl_controlled_interface) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    esp_cam_sensor_io_parl_controlled_interface->set_framesize = esp_cam_sensor_io_parl_interface->set_framesize;
    esp_cam_sensor_io_parl_controlled_interface->set_res_raw = esp_cam_sensor_io_parl_interface->set_res_raw;

    esp_cam_sensor_io_parl_interface->set_framesize = set_framesize_io_parl_interface;
    esp_cam_sensor_io_parl_interface->set_res_raw = set_res_raw_io_parl_interface;
#endif

    esp_cam_sensor_io_parl_interface->set_pixformat(esp_cam_sensor_io_parl_interface, pix_format);

    switch (esp_cam_sensor_io_parl_interface->pixformat) {
        case ESP_CAM_IO_PARL_PIXFORMAT_RGB444:
        case ESP_CAM_IO_PARL_PIXFORMAT_RGB555:
        case ESP_CAM_IO_PARL_PIXFORMAT_YUV422:
        case ESP_CAM_IO_PARL_PIXFORMAT_RGB565: {
            esp_cam_sensor_io_parl_interface->pixformat_bpp = 2;
            break;
	    }
        case ESP_CAM_IO_PARL_PIXFORMAT_RGB888: {
            esp_cam_sensor_io_parl_interface->pixformat_bpp = 3;
            break;
        }
        default:
            esp_cam_sensor_io_parl_interface->pixformat_bpp = 1;
            break;
    }

    if (esp_cam_sensor_io_parl_interface->id.PID == ESP_CAM_IO_PARL_OV2640_PID) {
        esp_cam_sensor_io_parl_interface->set_gainceiling(esp_cam_sensor_io_parl_interface, ESP_CAM_IO_PARL_GAINCEILING_2X);
        esp_cam_sensor_io_parl_interface->set_bpc(esp_cam_sensor_io_parl_interface, false);
        esp_cam_sensor_io_parl_interface->set_wpc(esp_cam_sensor_io_parl_interface, true);
        esp_cam_sensor_io_parl_interface->set_lenc(esp_cam_sensor_io_parl_interface, true);
    }

    if (pix_format == ESP_CAM_IO_PARL_PIXFORMAT_JPEG) {
        esp_cam_sensor_io_parl_interface->set_quality(esp_cam_sensor_io_parl_interface, config->jpeg_quality);
    }
    esp_cam_sensor_io_parl_interface->init_status(esp_cam_sensor_io_parl_interface);

    *ret_handle = esp_cam_sensor_io_parl_interface;
    return ESP_OK;

fail:
    esp_cam_del_sensor_io_parl();
    return err;
}

esp_err_t esp_cam_del_sensor_io_parl(void) {
    esp_err_t ret = ESP_OK;
    camera_disable_out_clock();
    if (esp_cam_sensor_io_parl_interface) {
        sccb_deinit();

        heap_caps_free(esp_cam_sensor_io_parl_interface);
        esp_cam_sensor_io_parl_interface = NULL;
    }
    if (esp_cam_sensor_io_parl_controlled_interface) {
        heap_caps_free(esp_cam_sensor_io_parl_controlled_interface);
    }

    return ret;
}

esp_err_t esp_cam_sensor_io_parl_get_interface(esp_cam_sensor_io_parl_handle_t *esp_cam_sensor_io_parl) {
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface != NULL, ESP_ERR_NOT_FOUND, TAG, "Camera not detected");
    *esp_cam_sensor_io_parl = esp_cam_sensor_io_parl_interface;
    return ESP_OK;
}

esp_err_t esp_cam_sensor_io_parl_frame_info(int *out_width, int *out_height) {
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface != NULL, ESP_ERR_NOT_FOUND, TAG, "Camera not detected");
    esp_cam_sensor_io_parl_resolution_info_t resolution = esp_cam_sensor_io_parl_resolution[esp_cam_sensor_io_parl_interface->status.framesize];
    *out_width = resolution.width;
    *out_height = resolution.height;
    return ESP_OK;
}

esp_err_t esp_cam_sensor_io_parl_connect(esp_cam_io_parl_handle_t esp_cam_io_parl) {
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface != NULL, ESP_ERR_NOT_FOUND, TAG, "Camera not detected");
    ESP_RETURN_ON_FALSE(esp_cam_io_parl != NULL, ESP_ERR_NOT_FOUND, TAG, "Invalid DVP port");
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface->dvp_interface == NULL, ESP_ERR_INVALID_STATE, TAG, "DVP port already connected");
    ESP_RETURN_ON_FALSE(esp_cam_io_parl->cam_task_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "DVP port is not enabled");

    esp_cam_sensor_io_parl_interface->dvp_interface = esp_cam_io_parl;

    esp_cam_sensor_io_parl_info_t *cam_info = esp_cam_sensor_io_parl_get_info(&esp_cam_sensor_io_parl_interface->id);
    if (esp_cam_sensor_io_parl_interface->dvp_interface->config.vsync_io < 0 && cam_info->support_jpeg) {
        if (esp_cam_sensor_io_parl_interface->pixformat != ESP_CAM_IO_PARL_PIXFORMAT_JPEG) {
            ESP_EARLY_LOGW(TAG, "DVP port insufficient for RAW image formats on %s, setting the image format to JPEG if supported", cam_info->name);
            esp_cam_sensor_io_parl_interface->set_pixformat(esp_cam_sensor_io_parl_interface, ESP_CAM_IO_PARL_PIXFORMAT_JPEG);
        }
    }
    else {
        ESP_EARLY_LOGE(TAG, "DVP port is not supported for this image sensor as it doesn't output JPEG image format");
    }

#if CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_AUTO
    esp_cam_sensor_io_parl_resolution_info_t resolution = esp_cam_sensor_io_parl_resolution[esp_cam_sensor_io_parl_interface->status.framesize];
    uint32_t alloc_size = esp_cam_sensor_io_parl_interface->pixformat != ESP_CAM_IO_PARL_PIXFORMAT_JPEG ? resolution.width * resolution.height * esp_cam_sensor_io_parl_interface->pixformat_bpp : (resolution.width * resolution.height * CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_MUL / CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_DIV + CONFIG_ESP_CAM_IO_PARL_FRAME_SIZE_PADDING);
    esp_cam_io_parl_set_alloc_size(esp_cam_sensor_io_parl_interface->dvp_interface, alloc_size, esp_cam_sensor_io_parl_interface->dvp_interface->alloc_heap_caps);
#endif

    ESP_EARLY_LOGI(TAG, "Attached DVP port to camera sensor");
    return ESP_OK;
}

esp_err_t esp_cam_sensor_io_parl_disconnect(void) {
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface != NULL, ESP_ERR_NOT_FOUND, TAG, "Camera not detected");
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface->dvp_interface != NULL, ESP_ERR_INVALID_STATE, TAG, "DVP port already disconnected");
    esp_cam_sensor_io_parl_interface->dvp_interface = NULL;
    return ESP_OK;
};

esp_err_t esp_cam_sensor_io_parl_save_to_nvs(const char *key) {
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface != NULL, ESP_ERR_NOT_FOUND, TAG, "Camera not detected");

    esp_err_t ret;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(key, NVS_READWRITE, &handle), TAG, "Failed to access nvs");

    ret = nvs_set_blob(handle, ESP_CAM_SENSOR_IO_PARL_NVS_KEY, &esp_cam_sensor_io_parl_interface->status, sizeof(esp_cam_sensor_io_parl_status_t));
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to set sensor blob");

    uint8_t pf = esp_cam_sensor_io_parl_interface->pixformat;
    ret = nvs_set_u8(handle, ESP_CAM_SENSOR_IO_PARL_PIXFORMAT_NVS_KEY, pf);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to set pixformat");

    ret = nvs_commit(handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to save camera settings");

    nvs_close(handle);
    return ESP_OK;

err:
    nvs_close(handle);
    return ret;
}

esp_err_t esp_cam_sensor_io_parl_load_from_nvs(const char *key) {
    ESP_RETURN_ON_FALSE(esp_cam_sensor_io_parl_interface != NULL, ESP_ERR_NOT_FOUND, TAG, "Camera not detected");

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(key, NVS_READWRITE, &handle), TAG, "Failed to access key");

    esp_cam_sensor_io_parl_status_t st;
    size_t size = sizeof(esp_cam_sensor_io_parl_status_t);
    esp_err_t ret = nvs_get_blob(handle, ESP_CAM_SENSOR_IO_PARL_NVS_KEY, &st, &size);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Error fetching sensor blob");

	esp_cam_sensor_io_parl_interface->set_ae_level(esp_cam_sensor_io_parl_interface, st.ae_level);
	esp_cam_sensor_io_parl_interface->set_aec2(esp_cam_sensor_io_parl_interface, st.aec2);
	esp_cam_sensor_io_parl_interface->set_aec_value(esp_cam_sensor_io_parl_interface, st.aec_value);
	esp_cam_sensor_io_parl_interface->set_agc_gain(esp_cam_sensor_io_parl_interface, st.agc_gain);
	esp_cam_sensor_io_parl_interface->set_awb_gain(esp_cam_sensor_io_parl_interface, st.awb_gain);
	esp_cam_sensor_io_parl_interface->set_bpc(esp_cam_sensor_io_parl_interface, st.bpc);
	esp_cam_sensor_io_parl_interface->set_brightness(esp_cam_sensor_io_parl_interface, st.brightness);
	esp_cam_sensor_io_parl_interface->set_colorbar(esp_cam_sensor_io_parl_interface, st.colorbar);
	esp_cam_sensor_io_parl_interface->set_contrast(esp_cam_sensor_io_parl_interface, st.contrast);
	esp_cam_sensor_io_parl_interface->set_dcw(esp_cam_sensor_io_parl_interface, st.dcw);
	esp_cam_sensor_io_parl_interface->set_denoise(esp_cam_sensor_io_parl_interface, st.denoise);
	esp_cam_sensor_io_parl_interface->set_exposure_ctrl(esp_cam_sensor_io_parl_interface, st.aec);
	esp_cam_sensor_io_parl_interface->set_framesize(esp_cam_sensor_io_parl_interface, st.framesize);
	esp_cam_sensor_io_parl_interface->set_gain_ctrl(esp_cam_sensor_io_parl_interface, st.agc);
	esp_cam_sensor_io_parl_interface->set_gainceiling(esp_cam_sensor_io_parl_interface, st.gainceiling);
	esp_cam_sensor_io_parl_interface->set_hmirror(esp_cam_sensor_io_parl_interface, st.hmirror);
	esp_cam_sensor_io_parl_interface->set_lenc(esp_cam_sensor_io_parl_interface, st.lenc);
	esp_cam_sensor_io_parl_interface->set_quality(esp_cam_sensor_io_parl_interface, st.quality);
	esp_cam_sensor_io_parl_interface->set_raw_gma(esp_cam_sensor_io_parl_interface, st.raw_gma);
	esp_cam_sensor_io_parl_interface->set_saturation(esp_cam_sensor_io_parl_interface, st.saturation);
	esp_cam_sensor_io_parl_interface->set_sharpness(esp_cam_sensor_io_parl_interface, st.sharpness);
	esp_cam_sensor_io_parl_interface->set_special_effect(esp_cam_sensor_io_parl_interface, st.special_effect);
	esp_cam_sensor_io_parl_interface->set_vflip(esp_cam_sensor_io_parl_interface, st.vflip);
	esp_cam_sensor_io_parl_interface->set_wb_mode(esp_cam_sensor_io_parl_interface, st.wb_mode);
	esp_cam_sensor_io_parl_interface->set_whitebal(esp_cam_sensor_io_parl_interface, st.awb);
	esp_cam_sensor_io_parl_interface->set_wpc(esp_cam_sensor_io_parl_interface, st.wpc);

    uint8_t pf;
    ret = nvs_get_u8(handle, ESP_CAM_SENSOR_IO_PARL_PIXFORMAT_NVS_KEY, &pf);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Error fetching pixformat key");
    esp_cam_sensor_io_parl_interface->set_pixformat(esp_cam_sensor_io_parl_interface, pf);

    nvs_close(handle);
    return ESP_OK;

err:
    nvs_close(handle);
    return ret;
}

esp_err_t esp_cam_sensor_io_parl_erase_nvs(const char *key) {
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(key, NVS_READWRITE, &handle), TAG, "Failed to access nvs");

    esp_err_t ret = nvs_erase_key(handle, key);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to erase nvs");

    ret = nvs_commit(handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to commit changes");

    nvs_close(handle);
    return ESP_OK;

err:
    nvs_close(handle);
    return ret;
}