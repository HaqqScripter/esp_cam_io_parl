# Changelog

## v0.1.0-beta.9

### Bug Fixes
- The examples no longer depend on the old esp_cam_io_parl ESP-IDF component, fixing potential issues.
- Removed dependency `espressif/esp_jpeg` at the moment.

### Features
- Added frame grab method `esp_cam_io_parl_queue_fill_mode_t` as `fill_mode` in `esp_cam_io_parl` configuration. `ESP_CAM_IO_PARL_QUEUE_LATEST` ensures that the latest frame should always be captured and inserted in the queue. `ESP_CAM_IO_PARL_QUEUE_PRESERVE` will preserve old frames in the queue until it has been grabbed by the user.
- Added `simple_camera_capture_example` and `camera_web_server_example` component examples.
- Renamed `simple_camera_stream_example` to `simple_camera_http_stream_example`. Now `esp_wifi_remote` and `esp_hosted` were added as dependencies for targets without Wi-Fi.
- Changed minimum ESP-IDF version requirement to v5.4.
- Added more descriptions to some of the new `esp_cam_sensor_io_parl` functions.
- Readded `esp32h4` target to `idf_component.yml`.

## v0.1.0-beta.8

### Bug Fixes
- Removed `FRAMESIZE_2160X1440` camera resolution.
- Frame buffer no longer freed when calling `esp_cam_io_parl_set_alloc_size` function.
- Removed `esp32h4` target from `idf_component.yml` at the moment.

### Features
- (Breaking changes) Partially reworked the old `esp_camera_sensor` as `esp_cam_sensor_io_parl`.
- Now all heap memory allocation functions are executed on the new camera task instead on the ISR function. This significantly improves reliability of the user application.
- OV5640: High Performance Mode no longer restricted to `CONFIG_IDF_EXPERIMENTAL_FEATURES`, and implemented settings for all resolutions. Now the OV5640 can capture up to 40FPS at 1280x720 resolution, and 30FPS at 1280x960. This option will be enabled by default in future updates when proven stable.
- OV5640 & OV3660 will have its sharpness set to the lowest value and denoise set to the maximum value on initialization.

### `esp_cam_sensor_io_parl`
- Now the user can attach the DVP interface (`esp_cam_io_parl`) to the camera sensor interface. When the DVP port is attached, the camera settings will be applied depending on the DVP port configuration. This will also enable automatic frame size allocation (user can set it to manually on `menuconfig`) when changing the camera resolution. Refer to `README.md` > `API Reference` section for more info on new camera sensor component.
- Presets will be added in later versions. It is a set of pre-configured camera settings that can be accessed through the preset identifier. For example, the planned presets for OV5640 are `DVP_8bit_24Minput_JPEG_320x240_40fps`, `DVP_8bit_24Minput_JPEG_640x480_40fps`, `DVP_8bit_24Minput_JPEG_1280x720_40fps`, `DVP_8bit_24Minput_JPEG_1280x960_30fps`, `DVP_8bit_24Minput_JPEG_1600x1200_15fps`, `DVP_8bit_24Minput_JPEG_1920x1080_20fps`, `DVP_8bit_24Minput_JPEG_2048x1536_15fps`, `DVP_8bit_24Minput_JPEG_2560x1440_20fps`, `DVP_8bit_24Minput_JPEG_2592x1944_15fps`.

## v0.1.0-beta.7.1 (Hotfix)

### Bug Fixes

- Slightly adjusted PLL settings on High Performance Mode for OV5640 so the image will transmit properly.

## v0.1.0-beta.7

### Bug Fixes

- Revert enum naming scheme for `esp_cam_io_parl_pclk_edge_t`.
- Fixed `simple_camera_stream_example` main.c file.

### Features

- Added High Performance Mode (HPM) as an experimental feature for OV5640 camera sensor. It allows images to be captured at a higher frame rate (e.g. 15FPS at 5MP) for resolutions above 1280x960. See `README.md` for more details.
- `JPEG buffer overflow` warning now re-implemented.
- Added more descriptions on `README.md` file.

## v0.1.0-beta.6

### Bug Fixes

- Fixed image jittering at some resolutions on OV5640 camera sensor.
- Removed resolution `FRAMESIZE_1200X800`.
- Fixed enum naming for `esp_cam_io_parl_pclk_edge_t`.

### Features

- Added NT99141 camera sensor. At the moment, it only supports on ESP32-C6 & ESP32-P4 due to the requirement of HREF signal.
- Improved JPEG parsing data from DVP camera significantly to reduce CPU load.
- Changed component namespace to `haqqscripter`.
- Added descriptions for ESP32-H4.

## v0.1.0-beta.5

### Bug Fixes

- Fixed JPEG reads for images taken from camera sensors with lower quality.
- Removed some tags from `idf_component.yml`.

### Features

- Added `esp_camera_sensor_erase_nvs` to remove saved camera settings from NVS.
- Tweaked `esp_camera_sensor_save_to_nvs` and `esp_camera_sensor_load_from_nvs` functions.
- Added more resolutions `FRAMESIZE_2160X1440` and `FRAMESIZE_1200X800`.
- Default settings on OV5640 are now improved to push up 30fps at HD (1280x720) and ~22fps at SXGAM (1280x960).
- `allow_pd` field of Parallel IO is checked if minimum ESP-IDF version is v5.4 to prevent compiler errors on ESP-IDF v5.3.
- `esp_cam_io_parl_handle_t` now stores the configuration `esp_cam_io_parl_config_t`.
- Added README.md to `simple_camera_stream_example`.

## v0.1.0-beta.4

### Features

- Added dependency `espressif/esp_jpeg` (TJpgDec).
- Now added minimum requirement ESP-IDF version to v5.3.
- Added `simple_camera_stream_example` example.
- Added macro `ESP_CAM_IO_PARL_SUPPORTED` for compatibility check across all targets.
- Added support for ESP32-H4 target.
- Added tags in `idf_component.yml` for ease of search.

## v0.1.0-beta.3

### Features

- `esp_cam_io_parl_set_alloc_size`: Now supports retaining frame buffer allocation when `heap_caps` is set to `0`.
- Performance improvements for the OV5640 sensor at certain resolutions.

## v0.1.0-beta.1

### Features


- First pre-release version. VSYNC pin is not implemented and only JPEG images are supported.

