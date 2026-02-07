# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

set(srcs
    main.c
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
    PRIV_INCLUDE_DIRS
    REQUIRES esp_wifi
    PRIV_REQUIRES esp_psram esp_driver_gpio nvs_flash esp_timer esp_http_server esp_event
)

target_add_binary_data(${COMPONENT_LIB} data/ov2640.html.gz BINARY)
target_add_binary_data(${COMPONENT_LIB} data/ov3660.html.gz BINARY)
target_add_binary_data(${COMPONENT_LIB} data/ov5640.html.gz BINARY)