# ESP32 Parallel IO Camera Driver

[![Component Registry](https://components.espressif.com/components/haqqscripter/esp_cam_io_parl/badge.svg)](https://components.espressif.com/components/haqqscripter/esp_cam_io_parl)

## General Information

This repository provides **ESP Parallel IO Camera** component (`esp_cam_io_parl`) that utilizes Parallel IO (PARLIO) peripheral to receive image data over DVP (Digital Video Port) using ESP32 SoCs that are capable of driving image sensors.

### Supported Targets

- ESP32-C5
- ESP32-C6
- ESP32-H2
- ESP32-H4
- ESP32-P4

### Supported Sensors

| Model   | Max resolution | Color type | Output format                                                     | Lens Size | Supported |
| ------- | -------------- | ---------- | ----------------------------------------------------------------- | --------- | --------- |
| OV2640  | 1600 x 1200    | color      | YUV(422/420)/YCbCr422<br>RGB565/555<br>8-bit compressed data<br>8/10-bit Raw RGB data | 1/4"     | ⚠️ (Only on ESP32-C6 & ESP32-P4) |
| OV3660  | 2048 x 1536    | color      | raw RGB data<br/>RGB565/555/444<br/>CCIR656<br/>YCbCr422<br/>compression | 1/5"     | ✅ |
| OV5640  | 2592 x 1944    | color      | RAW RGB<br/>RGB565/555/444<br/>CCIR656<br/>YUV422/420<br/>YCbCr422<br/>compression | 1/4"     | ✅ |
| NT99141 | 1280 x 720     | color      | YCbCr 422<br/>RGB565/555/444<br/>Raw<br/>CCIR656<br/>JPEG compression | 1/4"     | ⚠️ (Only on ESP32-C6 & ESP32-P4) |

### DVP Data Width

The following table shows the circumstances for each target. OV5640 and OV3660 camera sensors have been configured to gate PCLK signals so they can transmit JPEG bytestreams.

| Target    | Max data width               | Max PCLK frequency (ideal conditions) | Sample method format  |
| --------- | ---------------------------- | ------------------------------------- | ----------------------|
| ESP32-C6  | 16 (8 with valid signals)    | 80MHz                                 | Software delimiter used for 16 data lines, suitable for JPEG bytestreams, HREF (level delimiter) or HSYNC (pulse delimiter) signals for 8 data lines. |
| ESP32-H2  | 8 (No valid signals)         | 48MHz                                 | Software delimiter used for 8 data lines, primarily for JPEG bytestreams. This target does not accept valid signals with 8 data lines. |
| ESP32-P4  | 16 (8 with valid signals)    | 80MHz                                 | Software delimiter used for 16 data lines, suitable for JPEG bytestreams, HREF (level delimiter) or HSYNC (pulse delimiter) signals for 8 data lines. |
| ESP32-C5  | 8 (No valid signals)         | 80MHz                                 | Software delimiter used for 8 data lines, primarily for JPEG bytestreams. This target does not accept valid signals with 8 data lines. |
| ESP32-H4  | 8 (No valid signals)         | 48MHz                                 | Software delimiter used for 8 data lines, primarily for JPEG bytestreams. This target does not accept valid signals with 8 data lines. |

## Important to Remember

- It is recommended to have PSRAM installed and enabled for higher resolutions. Therefore, the ESP32-H2 is not recommended for image streaming due to its lack of PSRAM support and limited internal RAM. The ESP32-C6 can handle resolutions up to SVGA (tested with Wi-Fi enabled and a streaming web server, XGA can be reached with some tweaks).
- This component currently only accepts JPEG image inputs from DVP sensors due to the limitations of the Parallel IO driver for some targets (e.g. ESP32-H2 and ESP32-C5). As a result, it uses software delimiter with PCLK gating to allow streaming JPEG image from the following image sensors (except OV2640 & NT99141).
- Currently only OV3660 and OV5640 sensors are implemented to have gated PCLK signals. OV2640 & NT99141 requires the target to have valid signals (e.g. ESP32-C6 and ESP32-P4) so it can properly interface with the following sensor. It is highly recommended to use OV5640 or OV3660 for targets with limited data width.
- This component does not utilize VSYNC signals for controlling frames at the moment, support for receiving raw data will be possible if it gets implemented. VSYNC pin will be added as a part of ETM trigger or pulse delimiter for the Parallel IO peripheral for low latency.

## Additional Notes

- You can also use this component to receive JPEG images from another target capable of sending parallel data. Typically DVP camera sensors have 8/10-bit output.

## Installation Instructions


### Using with ESP-IDF
#### Through `idf.py add-dependency`
- You can add the component through ESP Component Registry:
```c
idf.py add-dependency haqqscripter/esp_cam_io_parl
```
- Additionally, to add the component as a dependency directly from GitHub `master` branch, run:
```c
idf.py add-dependency --git "https://github.com/HaqqScripter/esp_cam_io_parl.git" esp_cam_io_parl
```
#### Download and apply it locally
- Download and extract the component file.
- Insert the component under the `components` folder in your project file.
- If possible, enable PSRAM in `menuconfig` (also set Flash and PSRAM frequencies to the maximum speed available for the following targets for optimal performance)
- Include the component in your main code:
  ```c
  #include "esp_camera_sensor.h"
  #include "esp_cam_io_parl.h"
  ```

### Using with Arduino

#### Arduino IDE

This component will be released as a library.

---

# Examples

## Simple Camera Capture
```c
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_cam_io_parl.h"
#include "esp_camera_sensor.h"

static const char *TAG = "simple_camera_capture";

// Camera Sensor Configuration. Set the pins according to your connection to the DVP camera.
#define CAM_PWDN_PIN -1   // Power down pin, set to -1 if not used
#define CAM_RESET_PIN 9   // Software reset will be performed if set to -1
#define CAM_XCLK_PIN -1   // Emulated by PWM (LEDC), set to -1 for sensors with built-in crystal oscillator
#define CAM_SDA_PIN 13
#define CAM_SCL_PIN 14

#define CAM_D0_PIN 0
#define CAM_D1_PIN 1
#define CAM_D2_PIN 2
#define CAM_D3_PIN 3
#define CAM_D4_PIN 4
#define CAM_D5_PIN 5
#define CAM_D6_PIN 10
#define CAM_D7_PIN 11
#define CAM_VSYNC_PIN -1  // Not implemented at the moment
#define CAM_HREF_PIN -1   // Can not use any additional signals on ESP32-C5/ESP32-H2
#define CAM_HSYNC_PIN -1  // Can not use any additional signals on ESP32-C5/ESP32-H2
#define CAM_PCLK_PIN 12

// Save the frame buffer in PSRAM, or set it to MALLOC_CAP_INTERNAL for targets without PSRAM support
#define FRAME_BUFFER_CAPS MALLOC_CAP_INTERNAL

static esp_cam_io_parl_handle_t esp_cam_io_parl_handle;

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

void camera_task(void *args) {
    while(true) {
        int64_t last_frame = esp_timer_get_time();
        esp_cam_io_parl_trans_t image;
        if (esp_cam_io_parl_receive(esp_cam_io_parl_handle, &image, 5000) != ESP_OK) {
            ESP_LOGE(TAG, "Camera capture failed");
        } else {
            int64_t frame_time = esp_timer_get_time() - last_frame;
            frame_time /= 1000;
            uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
            ESP_LOGI(TAG, "JPEG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(image.length), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time, 1000.0 / avg_frame_time);
            esp_cam_io_parl_free_buffer(image);
        }
    }
}

void app_main(void) {
    // Camera sensor configuration
    static esp_camera_sensor_config_t camera_sensor_config = {
        .pwdn_io = CAM_PWDN_PIN,
        .reset_io = CAM_RESET_PIN,
        .xclk_io = CAM_XCLK_PIN,
        .xclk_hz = 20000000,
        .sda_io = CAM_SDA_PIN,
        .scl_io = CAM_SCL_PIN,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG, // esp_cam_io_parl only supports JPEG images at the moment
        .frame_size = FRAMESIZE_HVGA,
        .jpeg_quality = 8,
    };
    // DVP port configuration
    static esp_cam_io_parl_config_t esp_cam_io_parl_config = {
        .data_width = 8,
        .queue_frames = 1,
        .pclk_io = CAM_PCLK_PIN,
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
            .free_clk = true,
            .allow_pd = true,
        },
    };
    esp_err_t err = esp_camera_sensor_init(&camera_sensor_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    camera_sensor_t *sensor = esp_camera_sensor_get();
    ESP_LOGI(TAG, "Camera detected! Current quality = %u", sensor->status.quality);

    sensor->set_vflip(sensor, true); // Adjust if the image is flipped vertically
    sensor->set_hmirror(sensor, false); // Adjust if the image is flipped horizontally

    ESP_ERROR_CHECK(esp_cam_new_io_parl(&esp_cam_io_parl_config, &esp_cam_io_parl_handle));
    ESP_ERROR_CHECK(esp_cam_io_parl_enable(esp_cam_io_parl_handle, true));

    image_info_t *image = esp_camera_sensor_get_image(); // Prepare the frame allocation
    ESP_ERROR_CHECK(esp_cam_io_parl_set_alloc_size(esp_cam_io_parl_handle, image->width * image->height / 4 + 2048, FRAME_BUFFER_CAPS));

    ra_filter_init(&ra_filter, 20);

    xTaskCreate(camera_task, "camera_task", 2048, NULL, 2, NULL);
}
```

## Capture & Stream Image (AP)
```c
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
#include "esp_camera_sensor.h"

static const char *TAG = "camera_stream_to_http";

// Camera Sensor Configuration. Set the pins according to your connection to the DVP camera.
#define CAM_PWDN_PIN -1   // Power down pin, set to -1 if not used
#define CAM_RESET_PIN 5   // Software reset will be performed if set to -1
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
#define FRAME_BUFFER_CAPS MALLOC_CAP_SPIRAM

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

static esp_cam_io_parl_handle_t esp_cam_io_parl_handle;

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
            .authmode = WIFI_AUTH_WPA3_PSK,
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
    if (esp_cam_io_parl_receive(esp_cam_io_parl_handle, &frame, 5000) != ESP_OK) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t frame_length = frame.length;

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    res = httpd_resp_send(req, (const char *)frame.buffer, frame.length);
    esp_cam_io_parl_free_buffer(frame);

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
        if (esp_cam_io_parl_receive(esp_cam_io_parl_handle, &image, 5000) != ESP_OK) {
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
            esp_cam_io_parl_free_buffer(image);
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

        ESP_LOGI(TAG, "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time, 1000.0 / avg_frame_time);
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
    static esp_camera_sensor_config_t camera_sensor_config = {
        .pwdn_io = CAM_PWDN_PIN,
        .reset_io = CAM_RESET_PIN,
        .xclk_io = CAM_XCLK_PIN,
        .xclk_hz = 20000000,
        .sda_io = CAM_SDA_PIN,
        .scl_io = CAM_SCL_PIN,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG, // esp_cam_io_parl only supports JPEG images at the moment
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 8,
    };
    // DVP port configuration
    static esp_cam_io_parl_config_t esp_cam_io_parl_config = {
        .data_width = 8,
        .queue_frames = 1,
        .pclk_io = CAM_PCLK_PIN,
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
            .free_clk = true,
            .allow_pd = true,
        },
    };
    esp_err_t err = esp_camera_sensor_init(&camera_sensor_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    camera_sensor_t *sensor = esp_camera_sensor_get();
    ESP_LOGI(TAG, "Camera detected! Current quality = %u", sensor->status.quality);

    sensor->set_vflip(sensor, true); // Adjust if the image is flipped vertically
    sensor->set_hmirror(sensor, false); // Adjust if the image is flipped horizontally

    ESP_ERROR_CHECK(esp_cam_new_io_parl(&esp_cam_io_parl_config, &esp_cam_io_parl_handle));
    ESP_ERROR_CHECK(esp_cam_io_parl_enable(esp_cam_io_parl_handle, true));

    image_info_t *image = esp_camera_sensor_get_image(); // Prepare the frame allocation
    ESP_ERROR_CHECK(esp_cam_io_parl_set_alloc_size(esp_cam_io_parl_handle, image->width * image->height / 4 + 2048, FRAME_BUFFER_CAPS));

    wifi_init_softap();
    start_camera_server();
}
```

---

# Kconfig configurations
You can go to `menuconfig` -> `Component config` -> `Parallel IO Camera configuration` to view the available configurations. Note that some targets may have different configurations.<br>
You can also enable the use of LP I2C for ESP32-C6, ESP32-P4 and ESP32-C5 for SCCB interface. Please check the TRM on each target for the pins of LP I2C.<br>
These are the list of default configurations for this component:
```c
CONFIG_ESP_CAM_IO_PARL_NT99141 y // Probes NT99141. Targets without valid signals will have this configuration disabled since this sensor does not support it
CONFIG_ESP_CAM_IO_PARL_OV2640 y // Probes OV2640. Targets without valid signals will have this configuration disabled since this sensor does not support it
CONFIG_ESP_CAM_IO_PARL_OV3660 y // Probes OV3660
CONFIG_ESP_CAM_IO_PARL_OV5640 y // Probes OV5640
CONFIG_ESP_CAM_IO_PARL_OV5640_AF n // Allows OV5640 with Autofocus function
CONFIG_ESP_CAM_IO_PARL_OV5640_HPM n // [Experimental] Enables high performance on OV5640 for resolutions above 1280x960, this will allow 15FPS at full resolution, while 20FPS at 2560x1440 (QHD) resolution. Works best with 24MHz XCLK

CONFIG_CAMERA_PAYLOAD_BUFFER_SIZE 0x8000 // Payload size: 32768
CONFIG_ESP_CAM_IO_PARL_SCCB_I2C_PORT0 y // Use the I2C0 port by default
CONFIG_ESP_CAM_IO_PARL_SCCB_I2C_PORT1 n // I2C1 only available on ESP32-P4 and ESP32-H2
CONFIG_ESP_CAM_IO_PARL_SCCB_LP_I2C_PORT0 n // Only available on ESP32-C6, ESP32-P4 and ESP32-C5
CONFIG_ESP_CAM_IO_PARL_SCCB_CLK_FREQ 100000 // Higher values allows for faster initialization for SCCB
```

# Measured Frame Rates
At the moment, only the OV5640 camera sensor was measured. To accquire higher frame rates on OV5640, please refer to `Experimental Features (High Peformance Mode)` section.
### OV5640
| Resolution                                         | Aspect ratio  | Max frame rate  |
| -------------------------------------------------- | ------------- | --------------- |
| 2592x1944<br>2560x1920<br>2048x1536<br>1600x1200   | 4:3           | ~10FPS          |
| 1088x1920<br>720x1280<br>864x1536                  | 9:16          | ~15FPS          |
| 2560x1600<br>1920x1200                             | 16:10         | ~12FPS          |
| 2560x1440<br>1920x1080                             | 16:9          | ~13FPS          |
| 2160x1440                                          | 3:2           | ~11FPS          |
| 1280x1024                                          | 5:4           | ~10FPS          |
| 1280x960                                           | 4:3           | ~22FPS          |
| 1280x720                                           | 16:9          | ~30FPS          |
| 1024x768 (and below with the same aspect ratio)    | 4:3           | ~29FPS          |
| 640x360                                            | 16:9          | ~38FPS          |
| 480x320                                            | 3:2           | ~32FPS          |
| 320x320  (and below with the same aspect ratio)    | 1:1           | ~29FPS          |
| 176x144                                            | 5:4           | ~28FPS          |

# Experimental Features (High Peformance Mode)
- Currently, OV5640 has settings that allow 15FPS capture at full resolution (2592x1944), and 20FPS at QHD (2560x1440) by enabling `CONFIG_ESP_CAM_IO_PARL_OV5640_HPM` and it will be visible only if `CONFIG_IDF_EXPERIMENTAL_FEATURES` configuration is enabled. This feature works best with a stable 24MHz XCLK.
- Please ensure that the bandwidth is sufficient to transmit the image (over Wi-Fi, SD Card, or another target), it is preferred that UDP transport over Wi-Fi is used. This is true for high quality JPEG images at larger resolutions as they consume large amount of file size. Additionally, higher frame rates can consume more current.

# API Reference

## `esp_camera_sensor`
ESP Camera Sensor component included in this package to interface with OV2640, OV3660 and OV5640 camera sensors. It is a modified version of `esp_camera.h` component by Espressif.
```c
#include "esp_camera_sensor.h"
```

### Data Types

#### `esp_camera_sensor_config_t`

Configuration structure for camera sensor initialization.

| Field           | Type                 | Description                                 |
| --------------- | -------------------- | ------------------------------------------- |
| `pwdn_io`       | `gpio_num_t`         | GPIO pin for camera power-down line         |
| `reset_io`      | `gpio_num_t`         | GPIO pin for camera reset line              |
| `xclk_io`       | `gpio_num_t`         | GPIO pin for camera XCLK line               |
| `sda_io`        | `gpio_num_t`         | GPIO pin for camera SDA line                |
| `scl_io`        | `gpio_num_t`         | GPIO pin for camera SCL line                |
| `xclk_hz`       | `uint32_t`           | XCLK frequency in Hz                        |
| `pixel_format`  | `camera_pixformat_t` | Pixel format (`PIXFORMAT_*`)                |
| `frame_size`    | `camera_framesize_t` | Frame size (`FRAMESIZE_*`)                  |
| `ledc_timer`    | `ledc_timer_t`       | LEDC timer for XCLK generation              |
| `ledc_channel`  | `ledc_channel_t`     | LEDC channel for XCLK generation            |
| `jpeg_quality`  | `int`                | JPEG quality (0–63, lower = higher quality) |

#### `image_info_t`

Data structure containing image properties.

| Field    | Type                 | Description            |
| -------- | -------------------- | ---------------------- |
| `width`  | `size_t`             | Image width in pixels  |
| `height` | `size_t`             | Image height in pixels |
| `format` | `camera_pixformat_t` | Pixel format           |

### Functions

#### `esp_camera_sensor_init`
```c
esp_err_t esp_camera_sensor_init(const esp_camera_sensor_config_t *config);
```

Initialize the camera driver and configure the sensor via SCCB/I2C.

**Parameters:**

* `config` — Pointer to camera configuration parameters.

**Returns:**

* `ESP_OK` — Success
* `ESP_ERR_INVALID_ARG` — Invalid parameters
* `ESP_ERR_CAMERA_NOT_DETECTED` — Sensor not detected

#### `esp_camera_sensor_deinit`

```c
esp_err_t esp_camera_sensor_deinit(void);
```

Deinitialize the camera driver.

**Returns:**

* `ESP_OK` — Success
* `ESP_ERR_INVALID_STATE` — Driver not initialized

#### `esp_camera_sensor_get_image`

```c
image_info_t *esp_camera_sensor_get_image(void);
```

Get image resolution information for frame allocation.

**Returns:**
Pointer to `image_info_t` structure.

#### `esp_camera_sensor_get`

```c
camera_sensor_t *esp_camera_sensor_get(void);
```

Get a pointer to the sensor control structure.

**Returns:**
Pointer to `camera_sensor_t` structure.

#### `esp_camera_sensor_erase_nvs`

```c
esp_err_t esp_camera_sensor_erase_nvs(const char *key);
```

Remove camera settings from NVS.

**Parameters:**

* `key` — Unique key for camera settings.

#### `esp_camera_sensor_save_to_nvs`

```c
esp_err_t esp_camera_sensor_save_to_nvs(const char *key);
```

Save camera settings to NVS.

**Parameters:**

* `key` — Unique key for camera settings.


#### `esp_camera_sensor_load_from_nvs`

```c
esp_err_t esp_camera_sensor_load_from_nvs(const char *key);
```

Load camera settings from NVS.

**Parameters:**

* `key` — Unique key for camera settings.

---

## `esp_cam_io_parl`
ESP Parallel IO Camera component to interface with the DVP port of the following camera sensor.
```c
#include "esp_cam_io_parl.h"
```
### Data Types

#### `esp_cam_io_parl_pclk_edge_t`

> **PCLK edge configuration for sampling incoming data.**

| Enumerator                 | Description                           |
| -------------------------- | ------------------------------------- |
| `ESP_CAM_IO_PARL_PCLK_NEG` | Sample PCLK data on the negative edge |
| `ESP_CAM_IO_PARL_PCLK_POS` | Sample PCLK data on the positive edge |

#### `esp_cam_io_parl_config_t`

> **Configuration structure for `esp_cam_io_parl`.**

| Field                | Type           | Description                                                 |
| -------------------- | -------------- | ----------------------------------------------------------- |
| `data_width`         | `size_t`       | DVP data width (8 or 16 bits depending on SoC capabilities) |
| `queue_frames`       | `size_t`       | Number of frames to be queued                               |
| `pclk_io`            | `gpio_num_t`   | PCLK GPIO pin                                               |
| `pclk_sample_edge`   | `esp_cam_io_parl_pclk_edge_t` | Sampling edge (`ESP_CAM_IO_PARL_PCLK_NEG` or `ESP_CAM_IO_PARL_PCLK_POS`)                              |
| `vsync_io`           | `gpio_num_t`   | VSYNC GPIO pin *(Not implemented)*                          |
| `de_io`              | `gpio_num_t`   | DE (HREF) GPIO pin, set to -1 if unused                     |
| `hsync_io`           | `gpio_num_t`   | HSYNC GPIO pin, set to -1 if unused                         |
| `data_io[]`          | `gpio_num_t`   | Data line GPIOs                                             |
| `input_type`         | `esp_cam_io_parl_input_format_t`   | Input color format (Not implemented)    |
| `flags.invert_vsync` | `bool` | Invert VSYNC *(Not implemented)*                            |
| `flags.invert_de`    | `bool` | Invert DE pin (active low)                                  |
| `flags.invert_hsync` | `bool` | Invert HSYNC pin (active on rising edge)                    |
| `flags.jpeg_en`      | `bool` | JPEG input expected *(default)*                             |
| `flags.free_clk`     | `bool` | PCLK is free-running                                        |
| `flags.allow_pd`     | `bool` | Allow power down                                            |

#### `esp_cam_io_parl_trans_t`

> **Transaction buffer structure for received frames.**

| Field    | Type       | Description             |
| -------- | ---------- | ----------------------- |
| `buffer` | `uint8_t*` | Pointer to frame buffer |
| `length` | `uint32_t` | Length of frame buffer  |

#### `esp_cam_io_parl_handle_t`

> **Handle pointer to `esp_cam_io_parl`.**

### Functions

#### `esp_cam_new_io_parl`
```c
esp_err_t esp_cam_new_io_parl(const esp_cam_io_parl_config_t *config, esp_cam_io_parl_handle_t *ret_handle)
```

Creates and initializes a new `esp_cam_io_parl` handle.

**Parameters:**

* `config` — Pointer to configuration structure.
* `ret_handle` — Output handle.

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid parameters.
* `ESP_ERR_NO_MEM` — Out of memory.
* `ESP_OK` — Success.

#### `esp_cam_del_io_parl`
```c
esp_err_t esp_cam_del_io_parl(esp_cam_io_parl_handle_t esp_cam_io_parl);
```

Deletes a `esp_cam_io_parl` handle and deinitializes resources.

**Parameters:**

* `esp_cam_io_parl` — Handle that was created with `esp_cam_new_io_parl` to delete.

**Returns:**

* `ESP_ERR_INVALID_ARG` — Handle is NULL.
* `ESP_OK` — Success.

#### `esp_cam_io_parl_set_alloc_size`
```c
esp_err_t esp_cam_io_parl_set_alloc_size(esp_cam_io_parl_handle_t esp_cam_io_parl, uint32_t alloc_size, uint32_t heap_caps);
```

Sets the allocation size for the frame buffer.

**Parameters:**

* `esp_cam_io_parl` — Handle that was created with `esp_cam_new_io_parl`
* `alloc_size` — Frame allocation size (JPEG recommended: `width * height / 4 + 2048`).
* `heap_caps` — Memory type (`MALLOC_CAP_INTERNAL` or `MALLOC_CAP_SPIRAM`).

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid arguments.
* `ESP_OK` — Success.

#### `esp_cam_io_parl_enable`
```c
esp_err_t esp_cam_io_parl_enable(esp_cam_io_parl_handle_t esp_cam_io_parl, bool reset_queue);
```

Enables frame reception.

**Parameters:**

* `esp_cam_io_parl` — Handle that was created with `esp_cam_new_io_parl`
* `reset_queue` — Reset frame queue.

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid handle.
* `ESP_ERR_INVALID_STATE` — Already enabled.
* `ESP_OK` — Success.

#### `esp_cam_io_parl_disable`
```c
esp_err_t esp_cam_io_parl_disable(esp_cam_io_parl_handle_t esp_cam_io_parl)
```

Disables frame reception.

**Parameters:**

* `esp_cam_io_parl` — Handle that was created with `esp_cam_new_io_parl`

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid handle.
* `ESP_ERR_INVALID_STATE` — Already disabled.
* `ESP_OK` — Success.

#### `esp_cam_io_parl_receive`
```c
esp_err_t esp_cam_io_parl_receive(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, int32_t timeout_ms);
```

Receives a frame from the queue.

**Parameters:**

* `esp_cam_io_parl` — Handle that was created with `esp_cam_new_io_parl`
* `frame` — the frame buffer object.
* `timeout_ms` — Timeout in milliseconds (-1 for no timeout).

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid handle.
* `ESP_ERR_TIMEOUT` — Timeout expired.
* `ESP_OK` — Success.

#### `esp_cam_io_parl_receive_from_isr`
```c
esp_err_t esp_cam_io_parl_receive_from_isr(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, bool *hp_task_woken);
```

Receives a frame from ISR context.

**Notes:**

* Should only be called in ISR.
* Callback must be non-blocking.

**Parameters:**

* `esp_cam_io_parl` — Handle that was created with `esp_cam_new_io_parl`
* `frame` — the frame buffer object.
* `hp_task_woken` — Whether the high priority task is woken.

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid handle.
* `ESP_ERR_INVALID_STATE` — Called outside ISR.
* `ESP_FAIL` — Failed to receive.
* `ESP_OK` — Success.

#### `esp_cam_io_parl_free_buffer`
```c
esp_err_t esp_cam_io_parl_free_buffer(esp_cam_io_parl_trans_t frame);
```

Frees a previously received frame buffer.

**Parameters:**

* `frame` — The frame buffer object.

**Returns:**

* `ESP_ERR_INVALID_ARG` — Invalid buffer.
* `ESP_OK` — Success.