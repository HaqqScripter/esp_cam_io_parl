# get IDF version for comparison
set(idf_version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

set(COMPONENT_REQUIRES driver)

set(priv_include_dirs
    sensors/private_include
)
set(include_dirs
    driver/include
)

if(idf_version VERSION_GREATER_EQUAL "5.3" AND (IDF_TARGET STREQUAL "esp32c6" OR IDF_TARGET STREQUAL "esp32h2" OR IDF_TARGET STREQUAL "esp32p4" OR IDF_TARGET STREQUAL "esp32c5" OR IDF_TARGET STREQUAL "esp32h4"))
    set(srcs
        driver/esp_camera_sensor.c
        driver/esp_cam_io_parl.c
        driver/sccb.c
        driver/sensor.c
        driver/xclk.c

        sensors/ov2640.c
        sensors/ov3660.c
        sensors/ov5640.c
        sensors/nt99141.c
    )

    set(priv_requires esp_driver_ledc esp_driver_parlio esp_driver_i2c esp_timer freertos nvs_flash)

    if (idf_version VERSION_GREATER_EQUAL "6.0")
        list(APPEND priv_requires esp_driver_gpio)
    endif()
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    PRIV_INCLUDE_DIRS ${priv_include_dirs}
    REQUIRES driver
    PRIV_REQUIRES ${priv_requires}
)