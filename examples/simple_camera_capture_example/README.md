| Supported Targets | ESP32-C5 | ESP32-C6 | ESP32-H2 | ESP32-H4 | ESP32-P4 |
| ----------------- | -------- | -------- | -------- | -------- | -------- |


# DVP Camera capture example

## Overview

This example demonstrates how to use the `esp_cam_parl_io` component to capture DVP camera sensor signals and send it via HTTP server. This example will auto-detect camera sensors via the `esp_cam_sensor_io_parl` header and capture camera sensor signals via DVP interface. This example also showcase the frame rate that can be achieved with the respective camera sensors.

## Usage

The subsections below give only absolutely necessary information. For full steps to configure ESP-IDF and use it to build and run projects, see [ESP-IDF Getting Started](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#get-started).

### Hardware Required

- ESP32-C5/H2/H4 with OV3660, OV5640 camera sensor
- ESP32-C6/ESP32-P4 with OV2640, NT99141, OV3660 or OV5640

You can also connect camera sensors from other vendors to the ESP chip, you can find corresponding camera or from [ESP Component Registry](https://components.espressif.com), or design your own customized drivers.

```


                                   GND
                ┌────────────────────────────────────────────────┐
                │                                                │
                │                                                │
                │                                                │
                │                                                │
                │                                                │
                │                                                │
                │                                                │
                │                                                │
                │                                ┌───────────────┴────────────────────────────────┐
                │                                │                                                │
                │                                │                                                │
                │                                │                                                │
    ┌───────────┴─────────┐                      │                                                │
    │                     │                      │                                                │
    │                     │                      │                                                │
    │                     │       XCLK           │                  ESP_CHIP                      │
    │     DVP Camera      ├──────────────────────┤                                                │
    │                     │                      │                                                │
    │                     │       D0~7           │                                                │
    │                     ├──────────────────────┤                                                │
    │                     │                      │                                                │
    │                     │       PCLK           │                                                │
    │                     ├──────────────────────┤                                                │
    │                     │                      │                                                │
    │                     │       VSYNC          │                                                │
    │                     ├──────────────────────┤                                                │
    │                     │                      │                                                │
    │                     │      DE (HREF)       │                                                │
    │                     ├──────────────────────┤                                                │
    │                     │                      │                                                │
    └───────┬──┬──────────┘                      │                                                │
            │  │           I2C SCL               │                                                │
            │  └─────────────────────────────────┤                                                │
            │              I2C SDA               │                                                │
            └────────────────────────────────────┤                                                │
                                                 └────────────────────────────────────────────────┘
```
- For ESP32-C5/H2/H4, it must be connected to OV3660 or OV5640 and discard the HREF pin as it wouldn't be used due to the Parallel IO (PARLIO) peripheral limitation.

### Set Chip Target

First of all, your target must be supported by both:

- **By your ESP-IDF version**: For the full list of supported targets, run:
  ```
  idf.py --list-targets
  ```
- **By this example**: For the full list of supported targets,  refer to the supported targets table at the top of this README.

After you make sure that your target is supported, go to your example project directory and [set the chip target](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/tools/idf-py.html#select-the-target-chip-set-target):

```
idf.py set-target <target>
```

For example, to set ESP32-C6 as the chip target, run:

```
idf.py set-target esp32c6
```


### Configure the Project

For information about Kconfig options, see [Project Configuration](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/kconfig.html) > _Name of relevant section(s)_.

To conveniently check or modify Kconfig options for this example in a project configuration menu, run:

```
idf.py menuconfig
```

Available options for the I2C (SCCB) camera sensor port can be seen in ``menuconfig`` > ``Component config`` > ``Parallel IO Camera configuration``.


### Build and Flash

Execute the following command to build the project, flash it to your development board, and run the monitor tool to view the serial output:

```
idf.py build flash monitor
```

This command can be reduced to `idf.py flash monitor`.

If the above command fails, check the log on the serial monitor which usually provides information on the possible cause of the issue.

To exit the serial monitor, use `Ctrl` + `]`.


## Example Output

If you see the following console output, your example should be running correctly (the camera sensor varies):

```
I (1054) esp_camera_sensor: Detected OV5640 camera
I (1054) esp_camera_sensor: Camera PID=0x5640 VER=0x00 MIDL=0x00 MIDH=0x00
...
I (3805) parallel_io_camera: Camera detected! Current quality = 8
I (4206) main_task: Returned from app_main()
```


## Reference

- Link to the ESP-IDF feature's API reference, is attached in the README.md of the component [ESP-IDF: Parallel IO Camera Driver](https://components.espressif.com/components/haqqihaziq/esp_cam_io_parl)
- [ESP-IDF Getting Started](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#get-started)
- [Project Configuration](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/kconfig.html) (Kconfig Options)
