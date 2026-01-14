# Changelog

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