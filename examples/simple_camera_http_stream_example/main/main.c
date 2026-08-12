#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "esp_cam_io_parl.h"

static const char *TAG = "simple_camera_http_stream";

// Camera sensor configuration. Set the pins according to your connection to the DVP camera.
#define CAM_PWDN_PIN -1   // Power down pin, set to -1 if not used
#define CAM_RESET_PIN -1  // Software reset will be performed if set to -1
#define CAM_XCLK_PIN -1   // Emulated by PWM (LEDC), set to -1 for sensors with built-in crystal oscillator
#define CAM_SDA_PIN 26
#define CAM_SCL_PIN 25

#define CAM_D0_PIN 10
#define CAM_D1_PIN 9
#define CAM_D2_PIN 8
#define CAM_D3_PIN 7
#define CAM_D4_PIN 6
#define CAM_D5_PIN 1
#define CAM_D6_PIN 0
#define CAM_D7_PIN 3
#define CAM_VSYNC_PIN -1  // Not implemented at the moment
#define CAM_HREF_PIN -1   // Can not use any additional signals on ESP32-C5/ESP32-H2
#define CAM_HSYNC_PIN -1  // Can not use any additional signals on ESP32-C5/ESP32-H2
#define CAM_PCLK_PIN 2

// Save the frame buffer in PSRAM, or set it to MALLOC_CAP_INTERNAL for targets without PSRAM support
#define FRAME_BUFFER_CAPS MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT

// Wi-Fi details
#define ESP_WIFI_SSID "camera@2.4GHz"
#define ESP_WIFI_PASS "my_camera"
#define ESP_WIFI_CHANNEL 2 // Set channel over 36 for 5GHz (ESP32-C5)
#define MAX_STA_CONN 4

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t capture_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static esp_cam_io_parl_handle_t esp_cam_dvp_handle;
static esp_cam_sensor_io_parl_handle_t esp_cam_sensor_handle;

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
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
    }
}

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif_interface = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_PASS,
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
    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*
    // Uncomment if target is ESP32-C5 for 5GHz connectivity
    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY));
    */

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d", ESP_WIFI_SSID, ESP_WIFI_PASS, ESP_WIFI_CHANNEL);

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif_interface, &ip_info);
    ESP_LOGI(TAG, "IP Address:" IPSTR, IP2STR(&ip_info.ip));
}

static esp_err_t capture_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();
    esp_cam_io_parl_trans_t frame;
    if (esp_cam_io_parl_receive(esp_cam_dvp_handle, &frame, 5000) != ESP_OK) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t frame_length = frame.length;

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    res = httpd_resp_send(req, (const char *)frame.buffer, frame.length);
    esp_cam_io_parl_free_buffer(&frame);

    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uB %ums", frame_length, (uint32_t)((fr_end - fr_start) / 1000));
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
        esp_cam_io_parl_trans_t image;
        if (esp_cam_io_parl_receive(esp_cam_dvp_handle, &image, 5000) != ESP_OK) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        } else {
            _jpg_buf_len = image.length;
            _jpg_buf = image.buffer;
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
        if (image.buffer) {
            esp_cam_io_parl_free_buffer(&image);
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
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

        ESP_EARLY_LOGI(TAG, "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time, 1000.0 / avg_frame_time);
    }
    return res;
}

static void start_camera_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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

    ESP_LOGI(TAG, "Starting capture server on port: '%d'", config.server_port);
    if (httpd_start(&capture_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(capture_httpd, &capture_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;

    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

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
        .xclk_hz = 20000000,
        .sda_io = CAM_SDA_PIN,
        .scl_io = CAM_SCL_PIN,
        .pixel_format = ESP_CAM_IO_PARL_PIXFORMAT_JPEG, // esp_cam_io_parl only supports JPEG images at the moment
        .frame_size = ESP_CAM_IO_PARL_FRAMESIZE_QVGA,
        .jpeg_quality = 12,
    };
    // DVP port configuration
    static esp_cam_io_parl_config_t esp_cam_io_parl_config = {
        .data_width = 8,
        .queue_frames = 1,
        .frame_heap_caps = FRAME_BUFFER_CAPS,
        .pclk_io = CAM_PCLK_PIN,
        .pclk_sample_edge = ESP_CAM_IO_PARL_PCLK_POS, // esp_cam_sensor_io_parl configures the camera sensor to sample the data on positive edge
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

    esp_cam_sensor_handle->set_vflip(esp_cam_sensor_handle, true); // Adjust if the image is flipped vertically
    esp_cam_sensor_handle->set_hmirror(esp_cam_sensor_handle, false); // Adjust if the image is flipped horizontally

    ESP_ERROR_CHECK(esp_cam_new_io_parl(&esp_cam_io_parl_config, &esp_cam_dvp_handle));
    ESP_ERROR_CHECK(esp_cam_io_parl_enable(esp_cam_dvp_handle, true));

    ESP_ERROR_CHECK(esp_cam_sensor_io_parl_connect(esp_cam_dvp_handle));

    wifi_init_softap();
    start_camera_server();
}