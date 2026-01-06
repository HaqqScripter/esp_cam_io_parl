#ifndef _ESP_CAM_IO_PARL_H_
#define _ESP_CAM_IO_PARL_H_

#pragma once

#include "esp_err.h"
#include "soc/gpio_num.h"
#include "hal/parlio_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/parlio_types.h"

/**
 * @brief PCLK edge in esp_cam_io_parl configuration
 */
typedef enum {
    ESP_CAM_IO_PARL_PCLK_POS, /*!< Sample PCLK data on positive edge */
    ESP_CAM_IO_PARL_PCLK_NEG, /*!< Sample PCLK data on negative edge */
} esp_cam_io_parl_pclk_edge_t;

/**
 * @brief Sampling mode for esp_cam_io_parl
 */
typedef enum {
    ESP_CAM_IO_PARL_BUFFER, /*!< Fill the buffer with the image data (RAM intensive and simple to process the image data) */
    ESP_CAM_IO_PARL_STREAM, /*!< Fills each buffer node with a part of the image data (Less flexibility and RAM usage) */
} esp_cam_io_parl_sampling_mode_t;

/**
 * @brief esp_cam_io_parl configuration
 */
typedef struct {
    size_t data_width; /*!< DVP data width. ESP32-C5 & ESP32-H2 only supports up to 8 data lines and does not support valid signals. ESP32-C6 & ESP32-P4 supports up to 16 data lines, while only up to 8 data lines usable with DE or HSYNC pins used on the following targets */
    size_t queue_frames; /*!< Number of frames to be queued */

    gpio_num_t pclk_io; /*!< PCLK GPIO pin */
    uint32_t pclk_hz; /*!< PCLK frequency, in Hz*/
    esp_cam_io_parl_pclk_edge_t pclk_sample_edge : 1; /*!< Sample the data either on negative edge, or positive edge */

    esp_cam_io_parl_sampling_mode_t sampling_mode : 1; /*!< Sample method for flexibility */

    gpio_num_t vsync_io; /*!< VSYNC GPIO pin (Not implemented) */
    gpio_num_t de_io; /*!< DE (HREF) GPIO pin, set to -1 if not used. */
    gpio_num_t hsync_io; /*!< HSYNC GPIO pin, set to -1 if not used. */

    gpio_num_t data_io[PARLIO_RX_UNIT_MAX_DATA_WIDTH]; /*!< GPIO for data lines, some targets only support up to 8 data lines */

    struct {
        uint32_t invert_vsync : 1; /*!< Invert VSYNC pin (Not implemented) */
        uint32_t invert_de : 1; /*!< Invert DE pin so it is only valid on active low */
        uint32_t invert_hsync : 1; /*!< Invert HSYNC pin so it is only valid on rising edge */

        uint32_t jpeg_en : 1; /*!< Expects JPEG input (Not implemented, used by default) */

        uint32_t free_clk : 1; /*!< Whether the PCLK is free running */
        uint32_t allow_pd : 1; /*!< Set to allow power down */
    } flags;
} esp_cam_io_parl_config_t;

/**
 * @brief Received transaction buffer from esp_cam_io_parl
 */
typedef struct {
    uint8_t *buffer; /*!< Received frame buffer */
    uint32_t length; /*!< Frame buffer length */
} esp_cam_io_parl_trans_t;

/**
 * @brief Received chunked transaction buffer from esp_cam_io_parl
 */
typedef struct {
    uint8_t buffer[10384]; /*!< Received frame buffer */
    uint32_t length; /*!< Frame buffer length */
    uint32_t total_bytes; /*!< Total bytes received */
    uint32_t start_marker : 1; /*!< Implies that the current buffer is start of image */
    uint32_t end_marker : 1; /*!< Implies that the current buffer is end of image */
    //uint32_t has_start_marker : 1; /*!< Implies that the overall buffer has start of image marker */
    //uint32_t has_end_marker : 1; /*!< Implies that the overall buffer has end of image marker */
} esp_cam_io_parl_stream_trans_t;

/**
 * @brief esp_cam_io_parl handle struct
 */
typedef struct esp_cam_io_parl_t {
    uint32_t alloc_size; /*!< Frame allocation size */
    uint32_t alloc_heap_caps; /*!< Frame allocation heap caps */
    uint32_t payload_size; /*!< DMA buffer size */
    uint8_t *payload; /*!< DMA buffer */
    uint32_t use_soft_delimiter : 1; /*!< Is using PARLIO RX software delimiter */
    esp_cam_io_parl_config_t config;
    volatile struct {
        esp_cam_io_parl_trans_t frame; /*!< Frame buffer data */
        esp_cam_io_parl_stream_trans_t *streamed_frame; /*!< Streamed frame buffer data */
        uint8_t previous_byte; /*!< Last byte captured */
        size_t index; /*!< Index of the frame */
        int state; /*!< Frame capture state */
    } info; /*!< Frame buffer info */
    QueueHandle_t queue_handle; /*!< Queue handle for receiving frames */
    QueueHandle_t queue_stream_handle; /*!< Queue handle for receiving streamed frames */
    parlio_rx_unit_handle_t rx_unit; /*!< PARLIO RX unit */
    parlio_rx_delimiter_handle_t rx_delimiter; /*!< PARLIO RX delimiter */
} esp_cam_io_parl_t;

/**
 * @brief esp_cam_io_parl handle
 */
typedef struct esp_cam_io_parl_t *esp_cam_io_parl_handle_t;

/**
 * @brief Creates a new esp_cam_io_parl handle by initializing PARLIO RX unit
 *
 * @param[in]  config   esp_cam_io_parl configuration
 * @param[out] ret_handle   Returned esp_cam_io_parl handle
 * @return
 *      - ESP_ERR_INVALID_ARG       Invalid arguments in the parameter list or the esp_cam_io_parl configuration
 *      - ESP_ERR_NO_MEM            Not enough memory for the esp_cam_io_parl resources
 *      - ESP_OK                    Success on allocating esp_cam_io_parl
 */
esp_err_t esp_cam_new_io_parl(const esp_cam_io_parl_config_t *config, esp_cam_io_parl_handle_t *ret_handle);

/**
 * @brief Delete the following esp_cam_io_parl handle and deinitialize PARLIO RX unit
 *
 * @param[in] esp_cam_io_parl   esp_cam_io_parl handle that was created by `esp_cam_new_io_parl`
 * @return
 *      - ESP_ERR_INVALID_ARG       esp_cam_io_parl is NULL
 *      - ESP_OK                    Success on deleting esp_cam_io_parl
 */
esp_err_t esp_cam_del_io_parl(esp_cam_io_parl_handle_t esp_cam_io_parl);

/**
 * @brief Set the allocation size for frame buffer
 *
 * @param[in] esp_cam_io_parl   esp_cam_io_parl handle that was created by `esp_cam_new_io_parl`
 * @param[in] alloc_size   Frame buffer allocation size. For JPEG images, recommended size is width * height / 4 + 2048
 * @param[in] heap_caps   Whether the frame is stored in Internal RAM (MALLOC_CAP_INTERNAL) or PSRAM (MALLOC_CAP_SPIRAM) if supported
 * @return
 *      - ESP_ERR_INVALID_ARG       Invalid arguments in the parameter or esp_cam_io_parl is NULL
 *      - ESP_OK                    Successfully set frame buffer allocation size
 */
esp_err_t esp_cam_io_parl_set_alloc_size(esp_cam_io_parl_handle_t esp_cam_io_parl, uint32_t alloc_size, uint32_t heap_caps);

/**
 * @brief Enable the esp_cam_io_parl handle and begin to receive frame buffers
 *
 * @param[in] esp_cam_io_parl   esp_cam_io_parl handle that was created by `esp_cam_new_io_parl`
 * @param[in] reset_queue   Reset the frame queue by the esp_cam_io_parl handle
 * @return
 *      - ESP_ERR_INVALID_ARG       esp_cam_io_parl is NULL
 *      - ESP_ERR_INVALID_STATE     Peripherals used has already been enabled
 *      - ESP_OK                    Successfully enabled peripherals used by the esp_cam_io_parl handle
 */
esp_err_t esp_cam_io_parl_enable(esp_cam_io_parl_handle_t esp_cam_io_parl, bool reset_queue);

/**
 * @brief Disable the esp_cam_io_parl handle and stop receiving frame buffers
 *
 * @param[in] esp_cam_io_parl   esp_cam_io_parl handle that was created by `esp_cam_new_io_parl`
 * @return
 *      - ESP_ERR_INVALID_ARG       esp_cam_io_parl is NULL
 *      - ESP_ERR_INVALID_STATE     Peripherals used has already been disabled
 *      - ESP_OK                    Successfully enabled hardwares used by the esp_cam_io_parl handle
 */
esp_err_t esp_cam_io_parl_disable(esp_cam_io_parl_handle_t esp_cam_io_parl);

/**
 * @brief Receive the frame buffer from the queue
 *
 * @param[in] esp_cam_io_parl   esp_cam_io_parl handle that was created by `esp_cam_new_io_parl`
 * @param[out] frame   esp_cam_io_parl_trans_t object to pass the frame buffer
 * @param[in] timeout_ms   Timeout in milliseconds to receive the frame buffer, set to -1 to disable 
 * @return
 *      - ESP_ERR_INVALID_ARG       esp_cam_io_parl is NULL
 *      - ESP_ERR_TIMEOUT           Failed to receive the frame buffer in time
 *      - ESP_OK                    Successfully received frame buffer from the queue
 */
esp_err_t esp_cam_io_parl_receive(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, int32_t timeout_ms);

/**
 * @brief Receive the frame buffer from the queue in ISR context (e.g. inside a callback function)
 * @note  This function should only be called in ISR context, and the callback function should not block.
 * @note  The frame buffer should be accessible in ISR context.
 *
 * @param[in] esp_cam_io_parl   esp_cam_io_parl handle that was created by `esp_cam_new_io_parl`
 * @param[out] frame   esp_cam_io_parl_trans_t object to pass the frame buffer
 * @param[out] hp_task_woken    Whether the high priority task is woken (Optional, set NULL if not needed)
 * @return
 *      - ESP_ERR_INVALID_ARG       esp_cam_io_parl is NULL
 *      - ESP_ERR_INVALID_STATE     Function is called in non-ISR context or invalid sampling mode
 *      - ESP_FAIL                  Failed to receive the frame buffer
 *      - ESP_OK                    Successfully received frame buffer from the queue
 */
esp_err_t esp_cam_io_parl_receive_from_isr(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, bool *hp_task_woken);

/**
 * @brief Free the frame buffer received from esp_cam_io_parl
 *
 * @param[in] frame   esp_cam_io_parl_trans_t object to be freed
 * @return
 *      - ESP_ERR_INVALID_ARG       frame buffer is NULL
 *      - ESP_OK                    Successfully freed frame buffer
 */
esp_err_t esp_cam_io_parl_free_buffer(esp_cam_io_parl_trans_t *frame);

#endif /* _ESP_CAM_IO_PARL_H_ */