#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "nvs_flash.h"

#include "esp_cam_io_parl.h"
#include "sdkconfig.h"

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
#include "soc/gpio_num.h"
#include "driver/gpio.h"
#endif

static const char *TAG = "camera_web_server";

// Camera sensor configuration. Set the pins according to your connection to the DVP camera.
#define CAM_PWDN_PIN -1   // Power down pin, set to -1 if not used
#define CAM_RESET_PIN -1  // Software reset will be performed if set to -1
#define CAM_XCLK_PIN -1   // Emulated by PWM (LEDC), set to -1 for sensors with built-in crystal oscillator
#define CAM_SDA_PIN 2
#define CAM_SCL_PIN 3

#define CAM_D0_PIN 0
#define CAM_D1_PIN 23
#define CAM_D2_PIN 7
#define CAM_D3_PIN 24
#define CAM_D4_PIN 8
#define CAM_D5_PIN 25
#define CAM_D6_PIN 9
#define CAM_D7_PIN 26
#define CAM_VSYNC_PIN -1  // Not implemented at the moment
#define CAM_HREF_PIN -1   // Can not use any additional signals on ESP32-C5/ESP32-H2
#define CAM_HSYNC_PIN -1  // Can not use any additional signals on ESP32-C5/ESP32-H2
#define CAM_PCLK_PIN 27

// Save the frame buffer in PSRAM, or set it to MALLOC_CAP_INTERNAL for targets without PSRAM support / MALLOC_CAP_SPIRAM
#if CONFIG_SPIRAM
#define FRAME_ALLOC_CAPS MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
#else
#define FRAME_ALLOC_CAPS MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
#endif

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_STA || CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
#define ESP_WIFI_STA_SSID CONFIG_ESP_EXAMPLE_WIFI_SSID
#define ESP_WIFI_STA_PASS CONFIG_ESP_EXAMPLE_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY 10

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
#endif

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_AP || CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
#define ESP_WIFI_AP_SSID CONFIG_ESP_EXAMPLE_WIFI_AP_SSID
#define ESP_WIFI_AP_PASS CONFIG_ESP_EXAMPLE_WIFI_AP_PASSWORD
#define MAX_STA_CONN 4
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static esp_cam_io_parl_handle_t esp_cam_dvp_handle;
static esp_cam_sensor_io_parl_handle_t esp_cam_sensor_handle;

// ov2640.html.gz
extern const uint8_t ov2640_html_gz_start[] asm("_binary_ov2640_html_gz_start");
extern const uint8_t ov2640_html_gz_end[]   asm("_binary_ov2640_html_gz_end");
// ov3660.html.gz
extern const uint8_t ov3660_html_gz_start[] asm("_binary_ov3660_html_gz_start");
extern const uint8_t ov3660_html_gz_end[]   asm("_binary_ov3660_html_gz_end");
// ov5640.html.gz
extern const uint8_t ov5640_html_gz_start[] asm("_binary_ov5640_html_gz_start");
extern const uint8_t ov5640_html_gz_end[]   asm("_binary_ov5640_html_gz_end");

typedef struct {
    size_t size;   //number of values used for filtering
    size_t index;  //current value index
    size_t count;  //value count
    int sum;
    int *values;  //array to be filled with values
} ra_filter_t;
static ra_filter_t ra_filter;
static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values) {
      return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value) {
    if (!filter->values) {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
#if CONFIG_ESP_EXAMPLE_WIFI_MODE_AP || CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
    }
#endif
#if CONFIG_ESP_EXAMPLE_WIFI_MODE_STA || CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
#endif
}

static wifi_protocols_t wifi_protocols = {
    .ghz_2g = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX,
    .ghz_5g = WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX,
};
static wifi_bandwidths_t wifi_bandwidths = {
    .ghz_2g = WIFI_BW20,
    .ghz_5g = WIFI_BW20,
};

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_STA || CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif_interface = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_STA_SSID,
            .password = ESP_WIFI_STA_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocols(WIFI_IF_STA, &wifi_protocols));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidths(WIFI_IF_STA, &wifi_bandwidths));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ESP_WIFI_STA_SSID, ESP_WIFI_STA_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ESP_WIFI_STA_SSID, ESP_WIFI_STA_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif_interface, &ip_info);
    ESP_LOGI(TAG, "IP Address:" IPSTR, IP2STR(&ip_info.ip));
}
#endif

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_AP || CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif_interface = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_AP_SSID,
            .ssid_len = strlen(ESP_WIFI_AP_SSID),
#if CONFIG_ESP_EXAMPLE_WIFI_AP_BAND_5GHZ
            .channel = 36,
#else
            .channel = 1,
#endif
            .password = ESP_WIFI_AP_PASS,
            .max_connection = MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA2_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                .required = true,
            },
#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                .protected_keep_alive = 1,
            },
#endif
        },
    };
    if (strlen(ESP_WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocols(WIFI_IF_AP, &wifi_protocols));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidths(WIFI_IF_AP, &wifi_bandwidths));
    ESP_ERROR_CHECK(esp_wifi_start());
    #if CONFIG_ESP_EXAMPLE_WIFI_AP_BAND_5GHZ
        ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY));
    #else
        ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));
    #endif
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));
    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d", ESP_WIFI_AP_SSID, ESP_WIFI_AP_PASS, wifi_config.ap.channel);
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif_interface, &ip_info);
    ESP_LOGI(TAG, "IP Address:" IPSTR, IP2STR(&ip_info.ip));
}
#endif

static esp_err_t capture_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    esp_cam_io_parl_trans_t frame;
    if (esp_cam_io_parl_receive(esp_cam_dvp_handle, &frame, 5000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch image data");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    res = httpd_resp_send(req, (const char *)frame.buffer, frame.length);

    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uB %ums", frame.length, (uint32_t)((fr_end - fr_start) / 1000));
    esp_cam_io_parl_free_buffer(&frame);

    return res;
}
static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true) {
        int64_t last_frame = esp_timer_get_time();
        esp_cam_io_parl_trans_t frame;
        if (esp_cam_io_parl_receive(esp_cam_dvp_handle, &frame, 5000) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to fetch image data");
            res = ESP_FAIL;
        } else {
            _jpg_buf_len = frame.length;
            _jpg_buf = frame.buffer;
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Send frame failed");
            break;
        }

        int64_t frame_time = esp_timer_get_time() - last_frame;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);

        esp_rom_printf("MJPG: %uB %ums (%ufps), AVG: %ums (%ufps)\n", (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000 / (uint32_t)frame_time, avg_frame_time, 1000 / avg_frame_time);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return res;
}
static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char *)malloc(buf_len);
        if (!buf) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            *obuf = buf;
            return ESP_OK;
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}
static esp_err_t cmd_handler(httpd_req_t *req) {
    char *buf = NULL;
    char variable[32];
    char value[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int val = atoi(value);
    ESP_LOGI(TAG, "%s = %d", variable, val);
    int res = 0;

    if (!strcmp(variable, "framesize")) {
        res = esp_cam_sensor_handle->set_framesize(esp_cam_sensor_handle, (esp_cam_sensor_io_parl_framesize_t)val);
    } else if (!strcmp(variable, "quality")) {
        res = esp_cam_sensor_handle->set_quality(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "contrast")) {
        res = esp_cam_sensor_handle->set_contrast(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "brightness")) {
        res = esp_cam_sensor_handle->set_brightness(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "saturation")) {
        res = esp_cam_sensor_handle->set_saturation(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "sharpness")) {
        res = esp_cam_sensor_handle->set_sharpness(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "denoise")) {
        res = esp_cam_sensor_handle->set_denoise(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "gainceiling")) {
        res = esp_cam_sensor_handle->set_gainceiling(esp_cam_sensor_handle, (esp_cam_sensor_io_parl_gainceiling_t)val);
    } else if (!strcmp(variable, "colorbar")) {
        res = esp_cam_sensor_handle->set_colorbar(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "awb")) {
        res = esp_cam_sensor_handle->set_whitebal(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "agc")) {
        res = esp_cam_sensor_handle->set_gain_ctrl(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "aec")) {
        res = esp_cam_sensor_handle->set_exposure_ctrl(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "hmirror")) {
        res = esp_cam_sensor_handle->set_hmirror(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "vflip")) {
        res = esp_cam_sensor_handle->set_vflip(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "awb_gain")) {
        res = esp_cam_sensor_handle->set_awb_gain(esp_cam_sensor_handle, val);
	} else if (!strcmp(variable, "agc_gain")) {
        res = esp_cam_sensor_handle->set_agc_gain(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "aec_value")) {
        res = esp_cam_sensor_handle->set_aec_value(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "aec2")) {
        res = esp_cam_sensor_handle->set_aec2(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "dcw")) {
        res = esp_cam_sensor_handle->set_dcw(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "bpc")) {
        res = esp_cam_sensor_handle->set_bpc(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "wpc")) {
        res = esp_cam_sensor_handle->set_wpc(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "raw_gma")) {
        res = esp_cam_sensor_handle->set_raw_gma(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "lenc")) {
        res = esp_cam_sensor_handle->set_lenc(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "special_effect")) {
        res = esp_cam_sensor_handle->set_special_effect(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "wb_mode")) {
        res = esp_cam_sensor_handle->set_wb_mode(esp_cam_sensor_handle, val);
    } else if (!strcmp(variable, "ae_level")) {
        res = esp_cam_sensor_handle->set_ae_level(esp_cam_sensor_handle, val);
    } else {
        ESP_LOGI(TAG, "Unknown command: %s", variable);
        res = -1;
    }
    if (res < 0) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}
static int print_reg(char *p, esp_cam_sensor_io_parl_handle_t cam_sensor, uint16_t reg, uint32_t mask) {
    return sprintf(p, "\"0x%x\":%u,", reg, cam_sensor->get_reg(cam_sensor, reg, mask));
}
static esp_err_t status_handler(httpd_req_t *req) {
    static char json_response[1024];
    char *p = json_response;
    *p++ = '{';

    if (esp_cam_sensor_handle->id.PID == ESP_CAM_IO_PARL_OV5640_PID || esp_cam_sensor_handle->id.PID == ESP_CAM_IO_PARL_OV3660_PID) {
        for (int reg = 0x3400; reg < 0x3406; reg += 2) {
            p += print_reg(p, esp_cam_sensor_handle, reg, 0xFFF);  //12 bit
        }
        p += print_reg(p, esp_cam_sensor_handle, 0x3406, 0xFF);

        p += print_reg(p, esp_cam_sensor_handle, 0x3500, 0xFFFF0);  //16 bit
        p += print_reg(p, esp_cam_sensor_handle, 0x3503, 0xFF);
        p += print_reg(p, esp_cam_sensor_handle, 0x350a, 0x3FF);   //10 bit
        p += print_reg(p, esp_cam_sensor_handle, 0x350c, 0xFFFF);  //16 bit

        for (int reg = 0x5480; reg <= 0x5490; reg++) {
            p += print_reg(p, esp_cam_sensor_handle, reg, 0xFF);
        }

        for (int reg = 0x5380; reg <= 0x538b; reg++) {
            p += print_reg(p, esp_cam_sensor_handle, reg, 0xFF);
        }

        for (int reg = 0x5580; reg < 0x558a; reg++) {
            p += print_reg(p, esp_cam_sensor_handle, reg, 0xFF);
        }
        p += print_reg(p, esp_cam_sensor_handle, 0x558a, 0x1FF);  //9 bit
    } else if (esp_cam_sensor_handle->id.PID == ESP_CAM_IO_PARL_OV2640_PID) {
        p += print_reg(p, esp_cam_sensor_handle, 0xd3, 0xFF);
        p += print_reg(p, esp_cam_sensor_handle, 0x111, 0xFF);
        p += print_reg(p, esp_cam_sensor_handle, 0x132, 0xFF);
    }

    p += sprintf(p, "\"xclk\":%u,", esp_cam_sensor_handle->xclk_freq_hz / 1000000);
    p += sprintf(p, "\"pixformat\":%u,", esp_cam_sensor_handle->pixformat);
    p += sprintf(p, "\"framesize\":%u,", esp_cam_sensor_handle->status.framesize);
    p += sprintf(p, "\"quality\":%u,", esp_cam_sensor_handle->status.quality);
    p += sprintf(p, "\"brightness\":%d,", esp_cam_sensor_handle->status.brightness);
    p += sprintf(p, "\"contrast\":%d,", esp_cam_sensor_handle->status.contrast);
    p += sprintf(p, "\"saturation\":%d,", esp_cam_sensor_handle->status.saturation);
    p += sprintf(p, "\"sharpness\":%d,", esp_cam_sensor_handle->status.sharpness);
    p += sprintf(p, "\"denoise\":%d,", esp_cam_sensor_handle->status.denoise);
    p += sprintf(p, "\"special_effect\":%u,", esp_cam_sensor_handle->status.special_effect);
    p += sprintf(p, "\"wb_mode\":%u,", esp_cam_sensor_handle->status.wb_mode);
    p += sprintf(p, "\"awb\":%u,", esp_cam_sensor_handle->status.awb);
    p += sprintf(p, "\"awb_gain\":%u,", esp_cam_sensor_handle->status.awb_gain);
    p += sprintf(p, "\"aec\":%u,", esp_cam_sensor_handle->status.aec);
    p += sprintf(p, "\"aec2\":%u,", esp_cam_sensor_handle->status.aec2);
    p += sprintf(p, "\"ae_level\":%d,", esp_cam_sensor_handle->status.ae_level);
    p += sprintf(p, "\"aec_value\":%u,", esp_cam_sensor_handle->status.aec_value);
    p += sprintf(p, "\"agc\":%u,", esp_cam_sensor_handle->status.agc);
    p += sprintf(p, "\"agc_gain\":%u,", esp_cam_sensor_handle->status.agc_gain);
    p += sprintf(p, "\"gainceiling\":%u,", esp_cam_sensor_handle->status.gainceiling);
    p += sprintf(p, "\"bpc\":%u,", esp_cam_sensor_handle->status.bpc);
    p += sprintf(p, "\"wpc\":%u,", esp_cam_sensor_handle->status.wpc);
    p += sprintf(p, "\"raw_gma\":%u,", esp_cam_sensor_handle->status.raw_gma);
    p += sprintf(p, "\"lenc\":%u,", esp_cam_sensor_handle->status.lenc);
    p += sprintf(p, "\"hmirror\":%u,", esp_cam_sensor_handle->status.hmirror);
    p += sprintf(p, "\"vflip\":%u,", esp_cam_sensor_handle->status.vflip);
    p += sprintf(p, "\"dcw\":%u,", esp_cam_sensor_handle->status.dcw);
    p += sprintf(p, "\"colorbar\":%u", esp_cam_sensor_handle->status.colorbar);
    p += sprintf(p, ",\"led_intensity\":%d", -1);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}
static esp_err_t xclk_handler(httpd_req_t *req) {
    char *buf = NULL;
    char _xclk[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int xclk = atoi(_xclk);
    ESP_LOGI(TAG, "Set XCLK: %d MHz", xclk);
    int res = esp_cam_sensor_handle->set_xclk(esp_cam_sensor_handle, LEDC_TIMER_0, xclk);
    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}
static esp_err_t reg_handler(httpd_req_t *req) {
    char *buf = NULL;
    char _reg[32];
    char _mask[32];
    char _val[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK
        || httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int reg = atoi(_reg);
    int mask = atoi(_mask);
    int val = atoi(_val);
    ESP_LOGI(TAG, "Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

    int res = esp_cam_sensor_handle->set_reg(esp_cam_sensor_handle, reg, mask, val);
    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req) {
    char *buf = NULL;
    char _reg[32];
    char _mask[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int reg = atoi(_reg);
    int mask = atoi(_mask);
    int res = esp_cam_sensor_handle->get_reg(esp_cam_sensor_handle, reg, mask);
    if (res < 0) {
        return httpd_resp_send_500(req);
    }
    ESP_LOGI(TAG, "Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

    char buffer[20];
    const char *val = itoa(res, buffer, 10);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, val, strlen(val));
}
static int parse_get_var(char *buf, const char *key, int def) {
    char _int[16];
    if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK) {
        return def;
    }
    return atoi(_int);
}
static esp_err_t pll_handler(httpd_req_t *req) {
    char *buf = NULL;

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }

    int bypass = parse_get_var(buf, "bypass", 0);
    int mul = parse_get_var(buf, "mul", 0);
    int sys = parse_get_var(buf, "sys", 0);
    int root = parse_get_var(buf, "root", 0);
    int pre = parse_get_var(buf, "pre", 0);
    int seld5 = parse_get_var(buf, "seld5", 0);
    int pclken = parse_get_var(buf, "pclken", 0);
    int pclk = parse_get_var(buf, "pclk", 0);
    free(buf);

    ESP_LOGI(TAG, "Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
    int res = esp_cam_sensor_handle->set_pll(esp_cam_sensor_handle, bypass, mul, sys, root, pre, seld5, pclken, pclk);
    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}
static esp_err_t win_handler(httpd_req_t *req) {
    char *buf = NULL;

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }

    int startX = parse_get_var(buf, "sx", 0);
    int startY = parse_get_var(buf, "sy", 0);
    int endX = parse_get_var(buf, "ex", 0);
    int endY = parse_get_var(buf, "ey", 0);
    int offsetX = parse_get_var(buf, "offx", 0);
    int offsetY = parse_get_var(buf, "offy", 0);
    int totalX = parse_get_var(buf, "tx", 0);
    int totalY = parse_get_var(buf, "ty", 0);  // codespell:ignore totaly
    int outputX = parse_get_var(buf, "ox", 0);
    int outputY = parse_get_var(buf, "oy", 0);
    bool scale = parse_get_var(buf, "scale", 0) == 1;
    bool binning = parse_get_var(buf, "binning", 0) == 1;
    free(buf);

    ESP_LOGI(
        TAG,
        "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
        totalX, totalY, outputX, outputY, scale, binning  // codespell:ignore totaly
    );

    int res = esp_cam_sensor_handle->set_res_raw(esp_cam_sensor_handle, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);  // codespell:ignore totaly
    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    if (esp_cam_sensor_handle != NULL) {
        if (esp_cam_sensor_handle->id.PID == ESP_CAM_IO_PARL_OV3660_PID) {
            return httpd_resp_send(req, (const char *)ov3660_html_gz_start, (size_t)(ov3660_html_gz_end - ov3660_html_gz_start));
        } else if (esp_cam_sensor_handle->id.PID == ESP_CAM_IO_PARL_OV5640_PID) {
            return httpd_resp_send(req, (const char *)ov5640_html_gz_start, (size_t)(ov5640_html_gz_end - ov5640_html_gz_start));
        } else {
            return httpd_resp_send(req, (const char *)ov2640_html_gz_start, (size_t)(ov2640_html_gz_end - ov2640_html_gz_start));
        }
    } else {
        ESP_LOGE(TAG, "Camera sensor not found");
        return httpd_resp_send_500(req);
    }
}
static void start_camera_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t xclk_uri = {
        .uri = "/xclk",
        .method = HTTP_GET,
        .handler = xclk_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t reg_uri = {
        .uri = "/reg",
        .method = HTTP_GET,
        .handler = reg_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t greg_uri = {
        .uri = "/greg",
        .method = HTTP_GET,
        .handler = greg_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t pll_uri = {
        .uri = "/pll",
        .method = HTTP_GET,
        .handler = pll_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t win_uri = {
        .uri = "/resolution",
        .method = HTTP_GET,
        .handler = win_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    
    ra_filter_init(&ra_filter, 20);

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &xclk_uri);
        httpd_register_uri_handler(camera_httpd, &reg_uri);
        httpd_register_uri_handler(camera_httpd, &greg_uri);
        httpd_register_uri_handler(camera_httpd, &pll_uri);
        httpd_register_uri_handler(camera_httpd, &win_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;

    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
static void init_boot_gpio(void) {
    ESP_ERROR_CHECK(gpio_input_enable((gpio_num_t)CONFIG_ESP_EXAMPLE_WIFI_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)CONFIG_ESP_EXAMPLE_WIFI_GPIO, GPIO_MODE_INPUT));
}
#endif

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Camera sensor configuration
    static esp_cam_sensor_io_parl_config_t esp_cam_sensor_io_parl_config = {
        .pwdn_io = CAM_PWDN_PIN,
        .reset_io = CAM_RESET_PIN,
        .xclk_io = CAM_XCLK_PIN,
        .sda_io = CAM_SDA_PIN,
        .scl_io = CAM_SCL_PIN,
        .xclk_hz = 20000000,
        .pixel_format = ESP_CAM_IO_PARL_PIXFORMAT_JPEG, // esp_cam_io_parl only supports JPEG images at the moment
        .frame_size = ESP_CAM_IO_PARL_FRAMESIZE_QVGA,
        .jpeg_quality = 12,
    };
    // DVP port configuration
    static esp_cam_io_parl_config_t esp_cam_io_parl_config = {
        .data_width = 8,
        .pclk_io = CAM_PCLK_PIN,
        .queue_frames = 1,
        .fill_mode = ESP_CAM_IO_PARL_QUEUE_LATEST,
        .frame_heap_caps = FRAME_ALLOC_CAPS,
        .de_io = CAM_HREF_PIN,
        .hsync_io = CAM_HSYNC_PIN,
        .vsync_io = CAM_VSYNC_PIN, // Not implemented
        .data_io = {
            CAM_D0_PIN,
            CAM_D1_PIN,
            CAM_D2_PIN,
            CAM_D3_PIN,
            CAM_D4_PIN,
            CAM_D5_PIN,
            CAM_D6_PIN,
            CAM_D7_PIN,
        },
        .flags = {
            .allow_pd = true,
        },
    };
    esp_err_t err = esp_cam_new_sensor_io_parl(&esp_cam_sensor_io_parl_config, &esp_cam_sensor_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    ESP_LOGI(TAG, "Camera detected! Current quality = %u", esp_cam_sensor_handle->status.quality);

    if (esp_cam_sensor_handle->id.PID == ESP_CAM_IO_PARL_OV5640_PID) {
        esp_cam_sensor_handle->set_vflip(esp_cam_sensor_handle, true);
    }

    ESP_ERROR_CHECK(esp_cam_new_io_parl(&esp_cam_io_parl_config, &esp_cam_dvp_handle));
    ESP_ERROR_CHECK(esp_cam_io_parl_enable(esp_cam_dvp_handle, true));

    // Connect the DVP port to the camera sensor component
    ESP_ERROR_CHECK(esp_cam_sensor_io_parl_connect(esp_cam_dvp_handle));

#if CONFIG_ESP_EXAMPLE_WIFI_MODE_GPIO
    init_boot_gpio();
    if (gpio_get_level((gpio_num_t)CONFIG_ESP_EXAMPLE_WIFI_GPIO)) {
        wifi_init_softap();
    }
    else {
        wifi_init_sta();
    }
#elif CONFIG_ESP_EXAMPLE_WIFI_MODE_STA
    wifi_init_sta();
#elif CONFIG_ESP_EXAMPLE_WIFI_MODE_AP
    wifi_init_softap();
#endif

    start_camera_server();
}