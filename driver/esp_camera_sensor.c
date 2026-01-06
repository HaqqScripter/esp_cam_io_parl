#include "esp_camera_sensor.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sccb.h"
#include "sensor.h"
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

static const char *TAG = "esp_camera_sensor";

typedef struct {
    camera_sensor_t sensor;
    image_info_t image;
} esp_camera_sensor_state_t;

static const char *CAMERA_SENSOR_NVS_KEY = "camera_sensor";
static const char *CAMERA_PIXFORMAT_NVS_KEY = "camera_pixformat";
static esp_camera_sensor_state_t *esp_camera_sensor = NULL;

typedef struct {
    int (*detect)(int sccb_address, camera_sensor_id_t *id);
    int (*init)(camera_sensor_t *sensor);
} camera_sensor_func_t;

static const camera_sensor_func_t g_sensors[] = {
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

static esp_err_t esp_camera_sensor_probe(const esp_camera_sensor_config_t *config, camera_model_t *out_camera_model) {
    esp_err_t ret = ESP_OK;
    *out_camera_model = CAMERA_NONE;
    if (esp_camera_sensor != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_camera_sensor = (esp_camera_sensor_state_t *)calloc(1, sizeof(esp_camera_sensor_state_t));
    if (!esp_camera_sensor) {
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
        ret = sccb_use_port(config->sccb_i2c_port);
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
    esp_camera_sensor->sensor.sccb_address = sccb_address;
    esp_camera_sensor->sensor.xclk_freq_hz = config->xclk_hz;

    /**
     * Read sensor ID and then initialize sensor
     * Attention: Some sensors have the same SCCB address. Therefore, several
     * attempts may be made in the detection process
     */
    camera_sensor_id_t *id = &esp_camera_sensor->sensor.id;
    for (size_t i = 0; i < sizeof(g_sensors) / sizeof(camera_sensor_func_t); i++) {
        if (g_sensors[i].detect(sccb_address, id)) {
            camera_sensor_info_t *info = esp_camera_sensor_get_info(id);
            if (NULL != info) {
                *out_camera_model = info->model;
                ESP_LOGI(TAG, "Detected %s camera", info->name);
                g_sensors[i].init(&esp_camera_sensor->sensor);
                break;
            }
        }
    }

    if (CAMERA_NONE == *out_camera_model) { // If no supported sensors are detected
        ESP_LOGE(TAG, "Detected camera not supported.");
        ret = ESP_ERR_NOT_SUPPORTED;
        goto err;
    }

    ESP_LOGI(TAG, "Camera PID=0x%02x VER=0x%02x MIDL=0x%02x MIDH=0x%02x", id->PID, id->VER, id->MIDH, id->MIDL);

    ESP_LOGD(TAG, "Doing SW reset of sensor");
    vTaskDelay(10 / portTICK_PERIOD_MS);

    return esp_camera_sensor->sensor.reset(&esp_camera_sensor->sensor);
err:
    camera_disable_out_clock();
    return ret;
}
esp_err_t esp_camera_sensor_init(const esp_camera_sensor_config_t *config) {
    esp_err_t err;

    camera_model_t camera_model = CAMERA_NONE;
    err = esp_camera_sensor_probe(config, &camera_model);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x(%s)", err,
                 esp_err_to_name(err));
        goto fail;
    }

    camera_framesize_t frame_size = (camera_framesize_t)config->frame_size;
    camera_pixformat_t pix_format = (camera_pixformat_t)config->pixel_format;

    if (PIXFORMAT_JPEG == pix_format && (!camera_sensor[camera_model].support_jpeg)) {
        ESP_LOGE(TAG, "JPEG format is not supported on this sensor");
        err = ESP_ERR_NOT_SUPPORTED;
        goto fail;
    }

    if (frame_size > camera_sensor[camera_model].max_size) {
        ESP_LOGW(TAG, "The frame size exceeds the maximum for this sensor, it will be forced to the maximum possible value");
        frame_size = camera_sensor[camera_model].max_size;
    }

    esp_camera_sensor->sensor.status.framesize = frame_size;
    esp_camera_sensor->sensor.pixformat = pix_format;

    ESP_LOGD(TAG, "Setting frame size to %dx%d", camera_resolution[frame_size].width, camera_resolution[frame_size].height);
    if (esp_camera_sensor->sensor.set_framesize(&esp_camera_sensor->sensor, frame_size) != 0) {
        ESP_LOGE(TAG, "Failed to set frame size");
        err = ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE;
        goto fail;
    }
    esp_camera_sensor->sensor.set_pixformat(&esp_camera_sensor->sensor, pix_format);

    if (esp_camera_sensor->sensor.id.PID == OV2640_PID) {
        esp_camera_sensor->sensor.set_gainceiling(&esp_camera_sensor->sensor, GAINCEILING_2X);
        esp_camera_sensor->sensor.set_bpc(&esp_camera_sensor->sensor, false);
        esp_camera_sensor->sensor.set_wpc(&esp_camera_sensor->sensor, true);
        esp_camera_sensor->sensor.set_lenc(&esp_camera_sensor->sensor, true);
    }

    if (pix_format == PIXFORMAT_JPEG) {
        esp_camera_sensor->sensor.set_quality(&esp_camera_sensor->sensor, config->jpeg_quality);
    }
    esp_camera_sensor->sensor.init_status(&esp_camera_sensor->sensor);

    return ESP_OK;

fail:
    esp_camera_sensor_deinit();
    return err;
}

esp_err_t esp_camera_sensor_deinit(void) {
    esp_err_t ret = ESP_ERR_CAMERA_BASE;
    camera_disable_out_clock();
    if (esp_camera_sensor) {
        sccb_deinit();

        free(esp_camera_sensor);
        esp_camera_sensor = NULL;
    }

    return ret;
}

image_info_t *esp_camera_sensor_get_image(void) {
    if (esp_camera_sensor == NULL) {
        return NULL;
    }
    esp_camera_sensor->image.width = camera_resolution[esp_camera_sensor->sensor.status.framesize].width;
    esp_camera_sensor->image.height = camera_resolution[esp_camera_sensor->sensor.status.framesize].height;
    esp_camera_sensor->image.format = esp_camera_sensor->sensor.pixformat;
    return &esp_camera_sensor->image;
}

camera_sensor_t *esp_camera_sensor_get(void) {
    if (esp_camera_sensor == NULL) {
        return NULL;
    }
    return &esp_camera_sensor->sensor;
}

esp_err_t esp_camera_sensor_erase_nvs(const char *key) {
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

esp_err_t esp_camera_sensor_save_to_nvs(const char *key) {
    camera_sensor_t *s = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(s != NULL, ESP_ERR_CAMERA_NOT_DETECTED, TAG, "Camera not detected");

    esp_err_t ret;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(key, NVS_READWRITE, &handle), TAG, "Failed to access nvs");

    ret = nvs_set_blob(handle, CAMERA_SENSOR_NVS_KEY, &s->status, sizeof(camera_status_t));
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to set sensor blob");

    uint8_t pf = s->pixformat;
    ret = nvs_set_u8(handle, CAMERA_PIXFORMAT_NVS_KEY, pf);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to set pixformat");

    ret = nvs_commit(handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to save camera settings");

    nvs_close(handle);
    return ESP_OK;

err:
    nvs_close(handle);
    return ret;
}

esp_err_t esp_camera_sensor_load_from_nvs(const char *key) {
    camera_sensor_t *s = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(s != NULL, ESP_ERR_CAMERA_NOT_DETECTED, TAG, "Camera not detected");

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(key, NVS_READWRITE, &handle), TAG, "Failed to access key");

    camera_status_t st;
    size_t size = sizeof(camera_status_t);
    esp_err_t ret = nvs_get_blob(handle, CAMERA_SENSOR_NVS_KEY, &st, &size);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Error fetching sensor blob");

	s->set_ae_level(s, st.ae_level);
	s->set_aec2(s, st.aec2);
	s->set_aec_value(s, st.aec_value);
	s->set_agc_gain(s, st.agc_gain);
	s->set_awb_gain(s, st.awb_gain);
	s->set_bpc(s, st.bpc);
	s->set_brightness(s, st.brightness);
	s->set_colorbar(s, st.colorbar);
	s->set_contrast(s, st.contrast);
	s->set_dcw(s, st.dcw);
	s->set_denoise(s, st.denoise);
	s->set_exposure_ctrl(s, st.aec);
	s->set_framesize(s, st.framesize);
	s->set_gain_ctrl(s, st.agc);
	s->set_gainceiling(s, st.gainceiling);
	s->set_hmirror(s, st.hmirror);
	s->set_lenc(s, st.lenc);
	s->set_quality(s, st.quality);
	s->set_raw_gma(s, st.raw_gma);
	s->set_saturation(s, st.saturation);
	s->set_sharpness(s, st.sharpness);
	s->set_special_effect(s, st.special_effect);
	s->set_vflip(s, st.vflip);
	s->set_wb_mode(s, st.wb_mode);
	s->set_whitebal(s, st.awb);
	s->set_wpc(s, st.wpc);

    uint8_t pf;
    ret = nvs_get_u8(handle, CAMERA_PIXFORMAT_NVS_KEY, &pf);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Error fetching pixformat key");
    s->set_pixformat(s, pf);

    nvs_close(handle);
    return ESP_OK;

err:
    nvs_close(handle);
    return ret;
}