#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_cam_io_parl.h"

static const char *TAG = "simple_camera_capture";

// Camera sensor configuration. Set the pins according to your connection to the DVP camera.
#define CAM_PWDN_PIN -1   // Power down pin, set to -1 if not used
#define CAM_RESET_PIN -1  // Software reset will be performed if set to -1
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
#define CAM_HREF_PIN -1   // Can not use any additional signals on ESP32-C5/H2/H4
#define CAM_HSYNC_PIN -1  // Can not use any additional signals on ESP32-C5/H2/H4
#define CAM_PCLK_PIN 12

// Save the frame buffer in PSRAM, or set it to MALLOC_CAP_INTERNAL for targets without PSRAM support
#define FRAME_BUFFER_CAPS MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT

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

void camera_task(void *args) {
    while(true) {
        int64_t last_frame = esp_timer_get_time();
        esp_cam_io_parl_trans_t image;
        if (esp_cam_io_parl_receive(esp_cam_dvp_handle, &image, 5000) != ESP_OK) {
            ESP_LOGE(TAG, "Camera capture failed");
        } else {
            int64_t frame_time = esp_timer_get_time() - last_frame;
            frame_time /= 1000;
            uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
            ESP_EARLY_LOGI(TAG, "JPEG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(image.length), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time, 1000.0 / avg_frame_time);
            esp_cam_io_parl_free_buffer(&image);
        }
    }
}

void app_main(void) {
    // Camera sensor configuration
    static esp_cam_sensor_io_parl_config_t esp_cam_sensor_io_parl_config = {
        .pwdn_io = CAM_PWDN_PIN,
        .reset_io = CAM_RESET_PIN,
        .xclk_io = CAM_XCLK_PIN,
        .xclk_hz = 20000000,
        .sda_io = CAM_SDA_PIN,
        .scl_io = CAM_SCL_PIN,
        .pixel_format = ESP_CAM_IO_PARL_PIXFORMAT_JPEG, // esp_cam_io_parl only supports JPEG images at the moment
        .frame_size = ESP_CAM_IO_PARL_FRAMESIZE_HVGA,
        .jpeg_quality = 8,
    };
    // DVP port configuration
    static esp_cam_io_parl_config_t esp_cam_io_parl_config = {
        .data_width = 8,
        .queue_frames = 1,
        .frame_heap_caps = FRAME_BUFFER_CAPS,
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
            .allow_pd = true,
        },
    };
    esp_err_t err = esp_cam_new_sensor_io_parl(&esp_cam_sensor_io_parl_config, &esp_cam_sensor_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    ESP_LOGI(TAG, "Camera detected! Current quality = %u", esp_cam_sensor_handle->status.quality);

    ESP_ERROR_CHECK(esp_cam_new_io_parl(&esp_cam_io_parl_config, &esp_cam_dvp_handle));
    ESP_ERROR_CHECK(esp_cam_io_parl_enable(esp_cam_dvp_handle, true));

    ESP_ERROR_CHECK(esp_cam_sensor_io_parl_connect(esp_cam_dvp_handle)); // Attach the DVP port

    ra_filter_init(&ra_filter, 20);

    xTaskCreate(camera_task, "camera_task", 2048, NULL, 2, NULL);
}