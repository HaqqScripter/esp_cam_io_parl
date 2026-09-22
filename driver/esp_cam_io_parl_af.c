#include "sdkconfig.h"

#include "esp_cam_io_parl_af.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#ifndef CONFIG_ESP_CAM_IO_PARL_AF_DEFAULT_TIMEOUT_MS
#define CONFIG_ESP_CAM_IO_PARL_AF_DEFAULT_TIMEOUT_MS 2000
#endif

#if defined(CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT) && CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT

#include "esp_log.h"
static const char *TAG = "esp_cam_io_parl_af";

static uint32_t s_default_timeout_ms = CONFIG_ESP_CAM_IO_PARL_AF_DEFAULT_TIMEOUT_MS;

bool esp_cam_io_parl_af_is_supported(const esp_cam_sensor_io_parl_handle_t cam_sensor) {
    if (!cam_sensor || !cam_sensor->af_is_supported) {
        return false;
    }
    return cam_sensor->af_is_supported((esp_cam_sensor_io_parl_handle_t)cam_sensor) != 0;
}

esp_err_t esp_cam_io_parl_af_init(esp_cam_sensor_io_parl_handle_t cam_sensor, const esp_cam_io_parl_af_config_t *config) {
    if (!cam_sensor || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->timeout_ms) {
        s_default_timeout_ms = config->timeout_ms;
    }

    if (!esp_cam_io_parl_af_is_supported(cam_sensor)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!cam_sensor->af_init) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int ret = cam_sensor->af_init(cam_sensor, s_default_timeout_ms);
    if (ret < 0) {
        ESP_LOGE(TAG, "AF init failed");
        return ESP_FAIL;
    }

    return esp_cam_io_parl_af_set_mode(cam_sensor, config->mode);
}

esp_err_t esp_cam_io_parl_af_set_mode(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_io_parl_af_mode_t mode) {
    if (!cam_sensor) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_cam_io_parl_af_is_supported(cam_sensor)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!cam_sensor->af_set_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int ret = cam_sensor->af_set_mode(cam_sensor, (int)mode);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_cam_io_parl_af_trigger(esp_cam_sensor_io_parl_handle_t cam_sensor) {
    if (!cam_sensor) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_cam_io_parl_af_is_supported(cam_sensor)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!cam_sensor->af_trigger) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int ret = cam_sensor->af_trigger(cam_sensor);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_cam_io_parl_af_get_status(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_io_parl_af_status_t *out_status) {
    if (!cam_sensor || !out_status) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_cam_io_parl_af_is_supported(cam_sensor)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!cam_sensor->af_get_status) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    memset(out_status, 0, sizeof(*out_status));
    int ret = cam_sensor->af_get_status(cam_sensor, &out_status->raw, &out_status->focused, &out_status->busy);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_cam_io_parl_af_wait(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms, esp_cam_io_parl_af_status_t *out_status) {
    if (!cam_sensor || !out_status) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_cam_io_parl_af_is_supported(cam_sensor)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!timeout_ms) {
        timeout_ms = s_default_timeout_ms;
    }

    const uint64_t start = esp_timer_get_time();
    while (true) {
        esp_err_t ret = esp_cam_io_parl_af_get_status(cam_sensor, out_status);
        if (ret != ESP_OK) {
            return ret;
        }

        if (!out_status->busy) {
            return ESP_OK;
        }

        if (timeout_ms && ((esp_timer_get_time() - start) / 1000ULL) > timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t esp_cam_io_parl_af_set_manual_position(esp_cam_sensor_io_parl_handle_t cam_sensor, uint16_t position) {
    if (!cam_sensor) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_cam_io_parl_af_is_supported(cam_sensor)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!cam_sensor->af_set_manual_position) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int ret = cam_sensor->af_set_manual_position(cam_sensor, position);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

#else // CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT

bool esp_cam_io_parl_af_is_supported(const esp_cam_sensor_io_parl_handle_t cam_sensor) {
    (void)cam_sensor;
    return false;
}

esp_err_t esp_cam_io_parl_af_init(esp_cam_sensor_io_parl_handle_t cam_sensor, const esp_cam_io_parl_af_config_t *config) {
    (void)cam_sensor;
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_cam_io_parl_af_set_mode(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_io_parl_af_mode_t mode) {
    (void)cam_sensor;
    (void)mode;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_cam_io_parl_af_trigger(esp_cam_sensor_io_parl_handle_t cam_sensor) {
    (void)cam_sensor;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_cam_io_parl_af_get_status(esp_cam_sensor_io_parl_handle_t cam_sensor, esp_cam_io_parl_af_status_t *out_status) {
    (void)cam_sensor;
    (void)out_status;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_cam_io_parl_af_wait(esp_cam_sensor_io_parl_handle_t cam_sensor, uint32_t timeout_ms, esp_cam_io_parl_af_status_t *out_status) {
    (void)cam_sensor;
    (void)timeout_ms;
    (void)out_status;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_cam_io_parl_af_set_manual_position(esp_cam_sensor_io_parl_handle_t cam_sensor, uint16_t position) {
    (void)cam_sensor;
    (void)position;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif // CONFIG_ESP_CAM_IO_PARL_AF_SUPPORT