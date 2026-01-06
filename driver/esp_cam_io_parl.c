#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "portmacro.h"
#include "soc/clk_tree_defs.h"
#include "time.h"

#include "driver/parlio_rx.h"
#include "hal/parlio_types.h"
#include "esp_cam_io_parl.h"

static const char *TAG = "esp_cam_io_parl";

#define ESP_CAM_IO_PARL_CHECK_ISR(condition, err) if (!(condition)) { return err; }
#define MIN_FRAME_ALLOC_SIZE 64

#ifndef CONFIG_CAMERA_PAYLOAD_BUFFER_SIZE
#define CONFIG_CAMERA_PAYLOAD_BUFFER_SIZE 0x7FFF
#endif

typedef enum {
    JPEG_IDLE,      // Idle state, search for SOI marker
    JPEG_HEADER,    // SOI marker found, now scan for SOS
    JPEG_ENTROPY,   // SOS marker found, now look for EOI
} capture_state_t;

static inline void IRAM_ATTR process_jpeg_chunk(esp_cam_io_parl_handle_t handle, const uint8_t *chunk, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        switch (handle->info.state) {
            case JPEG_IDLE: {
                // Find 0xFF using optimized memchr
                uint8_t *ptr = memchr(chunk + offset, 0xFF, len - offset);
                if (!ptr) {
                    handle->info.previous_byte = chunk[len - 1];
                    return; // No 0xFF in this chunk
                }
                
                offset = ptr - chunk;
                uint8_t next_byte = (offset + 1 < len) ? chunk[offset + 1] : 0x00;
                
                // Check if split: last chunk ended in 0xFF, current starts with 0xD8
                if (handle->info.previous_byte == 0xFF && chunk[offset] == 0xD8) {
                    // Start detected!
                    handle->info.frame.buffer = heap_caps_malloc(handle->alloc_size, handle->alloc_heap_caps);
                    handle->info.index = 0;
                    if (handle->info.frame.buffer) {
                        handle->info.frame.buffer[handle->info.index++] = 0xFF;
                        handle->info.frame.buffer[handle->info.index++] = 0xD8;
                        handle->info.state = JPEG_HEADER;
                    }
                    handle->info.previous_byte = 0x00;
                    continue; 
                }

                if (next_byte == 0xD8) {
                    handle->info.frame.buffer = heap_caps_malloc(handle->alloc_size, handle->alloc_heap_caps);
                    handle->info.index = 0;
                    if (handle->info.frame.buffer) {
                        memcpy(handle->info.frame.buffer, &chunk[offset], 2);
                        handle->info.index += 2;
                        handle->info.state = JPEG_HEADER;
                        offset += 2;
                    }
                } else {
                    offset++; // Move past current 0xFF
                }
                break;
            }

            case JPEG_HEADER:
            case JPEG_ENTROPY: {
                // Efficiently scan for the next 0xFF
                uint8_t *next_marker = memchr(chunk + offset, 0xFF, len - offset);
                size_t bytes_to_copy = next_marker ? (next_marker - (chunk + offset)) : (len - offset);

                // Bulk Copy to PSRAM
                if (handle->info.index + bytes_to_copy < handle->alloc_size) {
                    memcpy(handle->info.frame.buffer + handle->info.index, chunk + offset, bytes_to_copy);
                    handle->info.index += bytes_to_copy;
                }
                offset += bytes_to_copy;

                if (next_marker) {
                    // Check the marker byte
                    uint8_t m_type = (offset + 1 < len) ? chunk[offset + 1] : 0x00;
                    
                    if (m_type == 0xDA && handle->info.state == JPEG_HEADER) {
                        handle->info.state = JPEG_ENTROPY; // Found SOS
                    }
                    else if (m_type == 0xD9 && handle->info.state == JPEG_ENTROPY) {
                        // Found EOI! Finalize image
                        handle->info.frame.buffer[handle->info.index++] = 0xFF;
                        handle->info.frame.buffer[handle->info.index++] = 0xD9;

                        esp_cam_io_parl_trans_t frame = handle->info.frame;
                        frame.length = handle->info.index;
                        // --- RELEASE BUFFER TO APPLICATION HERE ---
                        BaseType_t hpw = pdFALSE;
                        if (!xQueueSendFromISR(handle->queue_handle, &frame, &hpw)) {
                            esp_cam_io_parl_trans_t old;
                            if (xQueueReceiveFromISR(handle->queue_handle, &old, &hpw)) {
                                esp_cam_io_parl_free_buffer(&old);
                            }
                            xQueueSendFromISR(handle->queue_handle, &frame, &hpw);
                        }
                        if (hpw) portYIELD_FROM_ISR();
                        
                        handle->info.state = JPEG_IDLE;
                        handle->info.frame.buffer = NULL; // App must free this later
                        handle->info.frame.length = 0;
                        offset += 2;
                        continue;
                    }
                    
                    // Copy the marker byte and move on
                    if (handle->info.index < handle->alloc_size) handle->info.frame.buffer[handle->info.index++] = 0xFF;
                    offset++; 
                }
                break;
            }
        }
    }
    // Handle potential split marker for the next call
    handle->info.previous_byte = chunk[len - 1];
}

static bool IRAM_ATTR on_partial_receive_callback(parlio_rx_unit_handle_t rx_unit, const parlio_rx_event_data_t *edata, void *user_data) {
    process_jpeg_chunk((esp_cam_io_parl_handle_t)user_data, edata->data, edata->recv_bytes);
    return true;
}

static esp_err_t esp_cam_destroy_io_parl(esp_cam_io_parl_handle_t esp_cam_io_parl) {
    if (esp_cam_io_parl->queue_handle) {
        vQueueDeleteWithCaps(esp_cam_io_parl->queue_handle);
    }
    if (esp_cam_io_parl->info.frame.buffer) {
        free(esp_cam_io_parl->info.frame.buffer);
    }
    if (esp_cam_io_parl->payload) {
        free(esp_cam_io_parl->payload);
    }
    if (esp_cam_io_parl->rx_delimiter) {
        parlio_del_rx_delimiter(esp_cam_io_parl->rx_delimiter);
    }
    if (esp_cam_io_parl->rx_unit) {
        parlio_del_rx_unit(esp_cam_io_parl->rx_unit);
    }
    free(esp_cam_io_parl);
    return ESP_OK;
}

esp_err_t esp_cam_new_io_parl(const esp_cam_io_parl_config_t *config, esp_cam_io_parl_handle_t *ret_handle) {
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config && ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid argument");
    ESP_RETURN_ON_FALSE(__builtin_popcount(config->data_width) == 1, ESP_ERR_INVALID_ARG, TAG, "Data line number should be the power of 2 without counting valid signal");
    esp_cam_io_parl_handle_t esp_cam_io_parl = heap_caps_calloc(1, sizeof(esp_cam_io_parl_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(esp_cam_io_parl, ESP_ERR_NO_MEM, err, TAG, "No memory for allocating rx unit");
    esp_cam_io_parl->payload_size = CONFIG_ESP_CAM_IO_PARL_PAYLOAD_SIZE;
    gpio_num_t valid_gpio = config->de_io >= 0 ? config->de_io : (config->hsync_io >= 0 ? config->hsync_io : -1);
    uint32_t pclk_freq = config->pclk_hz > 0 ? config->pclk_hz : (40 * 1000 * 1000);
    parlio_rx_unit_config_t rx_unit_config = {
        .trans_queue_depth = 16,
        .max_recv_size = MIN(CONFIG_ESP_CAM_IO_PARL_PAYLOAD_SIZE, UINT16_MAX),
        .data_width = config->data_width,
        .clk_src = PARLIO_CLK_SRC_EXTERNAL,
        .ext_clk_freq_hz = pclk_freq,
        .clk_in_gpio_num = config->pclk_io,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = valid_gpio,
        .flags = {
            .free_clk = config->flags.free_clk,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
            .allow_pd = config->flags.allow_pd,
#endif
        },
    };
    memcpy(rx_unit_config.data_gpio_nums, config->data_io, PARLIO_RX_UNIT_MAX_DATA_WIDTH * sizeof(gpio_num_t));
    ESP_GOTO_ON_ERROR(parlio_new_rx_unit(&rx_unit_config, &esp_cam_io_parl->rx_unit), err, TAG, "Failed to initialize rx unit");
    if (esp_cam_io_parl->payload) {
        free(esp_cam_io_parl->payload);
        esp_cam_io_parl->payload = NULL;
    }
    esp_cam_io_parl->config = *config;
    if (esp_cam_io_parl->config.vsync_io >= 0) {
        ESP_LOGD(TAG, "VSYNC signal can not be used, ignoring the assigned pin");
    }
    if (valid_gpio >= 0 && config->data_width <= 8 && PARLIO_RX_UNIT_MAX_DATA_WIDTH > 8) {
        if (config->de_io >= 0) {
            parlio_rx_level_delimiter_config_t rx_delimiter_config = {
                .valid_sig_line_id = PARLIO_RX_UNIT_MAX_DATA_WIDTH - 1,
                .sample_edge = config->pclk_sample_edge,
                .eof_data_len = MIN(esp_cam_io_parl->payload_size, UINT16_MAX),
                .timeout_ticks = 0,
                .flags = {
                    .active_low_en = config->flags.invert_de,
                },
            };
            ESP_GOTO_ON_ERROR(parlio_new_rx_level_delimiter(&rx_delimiter_config, &esp_cam_io_parl->rx_delimiter), err, TAG, "Failed to initialize rx level delimiter");
        }
        else if (config->hsync_io >= 0) {
            parlio_rx_pulse_delimiter_config_t rx_delimiter_config = {
                .valid_sig_line_id = PARLIO_RX_UNIT_MAX_DATA_WIDTH - 1,
                .sample_edge = config->pclk_sample_edge,
                .eof_data_len = MIN(esp_cam_io_parl->payload_size, UINT16_MAX),
                .timeout_ticks = 0,
                .flags = {
                    .start_bit_included = false,
                    .end_bit_included = false,
                    .has_end_pulse = true,
                    .pulse_invert = config->flags.invert_hsync,
                },
            };
            ESP_GOTO_ON_ERROR(parlio_new_rx_pulse_delimiter(&rx_delimiter_config, &esp_cam_io_parl->rx_delimiter), err, TAG, "Failed to initialize rx pulse delimiter");
        }
        esp_cam_io_parl->use_soft_delimiter = false;
    }
    else {
        if (valid_gpio >= 0) {
            ESP_LOGD(TAG, "Valid signal can not be used, ignoring the assigned pin");
        }
        parlio_rx_soft_delimiter_config_t rx_delimiter_config = {
            .sample_edge = config->pclk_sample_edge,
            .eof_data_len = MIN(esp_cam_io_parl->payload_size, UINT16_MAX),
            .timeout_ticks = 0,
        };
        ESP_GOTO_ON_ERROR(parlio_new_rx_soft_delimiter(&rx_delimiter_config, &esp_cam_io_parl->rx_delimiter), err, TAG, "Failed to initialize rx software delimiter");
        esp_cam_io_parl->use_soft_delimiter = true;
    }

    parlio_rx_event_callbacks_t cbs = {
        .on_partial_receive = on_partial_receive_callback,
    };
    ESP_GOTO_ON_ERROR(parlio_rx_unit_register_event_callbacks(esp_cam_io_parl->rx_unit, &cbs, esp_cam_io_parl), err, TAG, "Failed to initialize partial receive callback");

    esp_cam_io_parl->queue_handle = xQueueCreateWithCaps(config->queue_frames, sizeof(esp_cam_io_parl_trans_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(esp_cam_io_parl->queue_handle, ESP_ERR_NO_MEM, err, TAG, "No memory for transaction queue");

    esp_cam_io_parl->payload = heap_caps_malloc(esp_cam_io_parl->payload_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_GOTO_ON_FALSE(esp_cam_io_parl->payload, ESP_ERR_NO_MEM, err, TAG, "No memory for payload buffer");
    esp_cam_io_parl->alloc_heap_caps = MALLOC_CAP_INTERNAL;
    *ret_handle = esp_cam_io_parl;
    return ESP_OK;
err:
    if (esp_cam_io_parl) {
        esp_cam_destroy_io_parl(esp_cam_io_parl);
    }
    return ret;
}

esp_err_t esp_cam_del_io_parl(esp_cam_io_parl_handle_t esp_cam_io_parl) {
    ESP_RETURN_ON_FALSE(esp_cam_io_parl, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    return esp_cam_destroy_io_parl(esp_cam_io_parl);
}

esp_err_t esp_cam_io_parl_set_alloc_size(esp_cam_io_parl_handle_t esp_cam_io_parl, uint32_t alloc_size, uint32_t heap_caps) {
    ESP_RETURN_ON_FALSE(esp_cam_io_parl && alloc_size > MIN_FRAME_ALLOC_SIZE, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    if (esp_cam_io_parl->info.frame.buffer) { // To avoid misalignment
        esp_cam_io_parl->info.state = JPEG_IDLE;
        esp_cam_io_parl->info.index = 0;
        free(esp_cam_io_parl->info.frame.buffer);
        esp_cam_io_parl->info.frame.buffer = NULL;
        esp_cam_io_parl->info.frame.length = 0;
    }
    esp_cam_io_parl->alloc_size = alloc_size;
    if (heap_caps) {
        esp_cam_io_parl->alloc_heap_caps = heap_caps;
    }
    return ESP_OK;
}

esp_err_t esp_cam_io_parl_enable(esp_cam_io_parl_handle_t esp_cam_io_parl, bool reset_queue) {
    ESP_RETURN_ON_FALSE(esp_cam_io_parl, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_ERROR(parlio_rx_unit_enable(esp_cam_io_parl->rx_unit, reset_queue), TAG, "Failed to enable PARLIO RX unit");
    if (esp_cam_io_parl->use_soft_delimiter) {
        ESP_RETURN_ON_ERROR(parlio_rx_soft_delimiter_start_stop(esp_cam_io_parl->rx_unit, esp_cam_io_parl->rx_delimiter, true), TAG, "Failed to start PARLIO RX soft delimiter");
        const parlio_receive_config_t receive_config = {
            .delimiter = esp_cam_io_parl->rx_delimiter,
            .flags = {
                .partial_rx_en = true,
                .indirect_mount = true,
            },
        };
        ESP_RETURN_ON_ERROR(parlio_rx_unit_receive(esp_cam_io_parl->rx_unit, esp_cam_io_parl->payload, esp_cam_io_parl->payload_size, &receive_config), TAG, "Failed to receive from PARLIO RX");
    }
    return ESP_OK;
}

esp_err_t esp_cam_io_parl_disable(esp_cam_io_parl_handle_t esp_cam_io_parl) {
    if (esp_cam_io_parl->use_soft_delimiter) {
        ESP_RETURN_ON_ERROR(parlio_rx_soft_delimiter_start_stop(esp_cam_io_parl->rx_unit, esp_cam_io_parl->rx_delimiter, false), TAG, "Failed to stop PARLIO RX soft delimiter");
    }
    ESP_RETURN_ON_ERROR(parlio_rx_unit_disable(esp_cam_io_parl->rx_unit), TAG, "Failed to disable PARLIO RX unit");
    return ESP_OK;
}

esp_err_t esp_cam_io_parl_receive(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, int32_t timeout_ms) {
    ESP_RETURN_ON_FALSE(esp_cam_io_parl && frame, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(esp_cam_io_parl->config.sampling_mode == ESP_CAM_IO_PARL_BUFFER, ESP_ERR_INVALID_STATE, TAG, "Sampling mode is invalid");
    TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t ret = xQueueReceive(esp_cam_io_parl->queue_handle, frame, ticks);
    if (ret == pdFALSE) {
        frame->buffer = NULL;
        frame->length = 0;
    }
    return ret == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp_cam_io_parl_receive_from_isr(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, bool *hp_task_woken) {
    ESP_CAM_IO_PARL_CHECK_ISR(esp_cam_io_parl && frame, ESP_ERR_INVALID_ARG);
    ESP_CAM_IO_PARL_CHECK_ISR(xPortInIsrContext() == pdTRUE, ESP_ERR_INVALID_STATE);
    ESP_CAM_IO_PARL_CHECK_ISR(esp_cam_io_parl->config.sampling_mode == ESP_CAM_IO_PARL_BUFFER, ESP_ERR_INVALID_STATE);
    BaseType_t _hp_task_woken = 0;
    BaseType_t ret = xQueueReceiveFromISR(esp_cam_io_parl->queue_handle, frame, &_hp_task_woken);
    if (hp_task_woken) {
        *hp_task_woken = _hp_task_woken != 0;
    }
    return ret == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_cam_io_parl_free_buffer(esp_cam_io_parl_trans_t *frame) {
    ESP_RETURN_ON_FALSE(frame->buffer, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    free(frame->buffer);
    frame->buffer = NULL;
    frame->length = 0;
    return ESP_OK;
}