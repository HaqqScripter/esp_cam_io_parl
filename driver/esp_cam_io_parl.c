#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
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

#define MIN_STREAM_CHUNK_SIZE 32768
#define MAX_STREAM_CHUNK_SIZE 65535

#ifndef CONFIG_CAMERA_PAYLOAD_BUFFER_SIZE
#define CONFIG_CAMERA_PAYLOAD_BUFFER_SIZE 0x8000 // 32768
#endif

static bool IRAM_ATTR esp_cam_io_parl_received_partial_data(parlio_rx_unit_handle_t rx_unit, const parlio_rx_event_data_t *edata, void *user_data) {
    esp_cam_io_parl_handle_t esp_cam_io_parl = (esp_cam_io_parl_handle_t)user_data;
    const uint8_t *chunk = edata->data;
    size_t chunk_len = edata->recv_bytes;
    size_t offset = 0;
    BaseType_t _hp_task_woken = pdFALSE;
    while (offset < chunk_len) {
        switch (esp_cam_io_parl->info.state) {
            case ESP_CAM_IO_PARL_JPEG_IDLE: {
                uint8_t *ptr = memchr(chunk + offset, 0xFF, chunk_len - offset);
                if (!ptr) {
                    esp_cam_io_parl->info.previous_byte = chunk[chunk_len - 1];
                    return true;
                }
                offset = ptr - chunk;
                bool is_split = (esp_cam_io_parl->info.previous_byte == 0xFF && chunk[offset] == 0xD8);
                bool is_normal = (offset + 1 < chunk_len && chunk[offset + 1] == 0xD8);
                if (is_split || is_normal) {
                    if (xQueueReceiveFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_PENDING], &esp_cam_io_parl->info.frame, &_hp_task_woken)) {
                        esp_cam_io_parl->info.index = 0;
                        esp_cam_io_parl->info.frame.buffer[esp_cam_io_parl->info.index++] = 0xFF;
                        esp_cam_io_parl->info.frame.buffer[esp_cam_io_parl->info.index++] = 0xD8;
                        esp_cam_io_parl->info.state = ESP_CAM_IO_PARL_JPEG_HEADER;
                    }
                    offset += (is_normal) ? 2 : 1;
                    esp_cam_io_parl->info.previous_byte = 0xFF;
                    continue;
                }
                offset++;
                break;
            }
            case ESP_CAM_IO_PARL_JPEG_HEADER:
            case ESP_CAM_IO_PARL_JPEG_ENTROPY: {
                uint8_t *next_marker = memchr(chunk + offset, 0xFF, chunk_len - offset);
                size_t bytes_to_copy = next_marker ? (next_marker - (chunk + offset)) : (chunk_len - offset);
                // Bulk Copy to frame buffer
                if (esp_cam_io_parl->info.index + bytes_to_copy < esp_cam_io_parl->info.frame.length) {
                    memcpy(esp_cam_io_parl->info.frame.buffer + esp_cam_io_parl->info.index, chunk + offset, bytes_to_copy);
                    esp_cam_io_parl->info.index += bytes_to_copy;
                }
                else {
                    goto jpeg_overflow;
                }
                offset += bytes_to_copy;
                if (next_marker) {
                    uint8_t m_type = (offset + 1 < chunk_len) ? chunk[offset + 1] : 0x00;
                    if (m_type == 0xDA && esp_cam_io_parl->info.state == ESP_CAM_IO_PARL_JPEG_HEADER) {
                        esp_cam_io_parl->info.state = ESP_CAM_IO_PARL_JPEG_ENTROPY;
                    }
                    else if (m_type == 0xD8 && esp_cam_io_parl->info.state == ESP_CAM_IO_PARL_JPEG_ENTROPY) {
                        // Deformed JPEG image, handle it
                        goto err;
                    }
                    else if (m_type == 0xD9 && esp_cam_io_parl->info.state == ESP_CAM_IO_PARL_JPEG_ENTROPY) {
                        esp_cam_io_parl->info.frame.buffer[esp_cam_io_parl->info.index++] = 0xFF;
                        esp_cam_io_parl->info.frame.buffer[esp_cam_io_parl->info.index++] = 0xD9;
                        esp_cam_io_parl_trans_t frame = esp_cam_io_parl->info.frame;
                        frame.length = esp_cam_io_parl->info.index;
                        // Release frame buffer to queue
                        BaseType_t hpw = pdFALSE;
                        if (!xQueueSendFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_READY], &frame, &hpw)) {
                            if (esp_cam_io_parl->config.fill_mode == ESP_CAM_IO_PARL_QUEUE_LATEST) {
                                esp_cam_io_parl_trans_t old;
                                if (xQueueReceiveFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_READY], &old, &hpw)) {
                                    xQueueSendFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_FAIL], &old, &hpw);
                                }
                                xQueueSendFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_READY], &frame, &hpw);
                            }
                            else {
                                xQueueSendFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_FAIL], &frame, &hpw);
                            }
                        }
                        esp_cam_io_parl->info.state = ESP_CAM_IO_PARL_JPEG_IDLE;
                        esp_cam_io_parl->info.frame.buffer = NULL;
                        esp_cam_io_parl->info.frame.length = 0;
                        offset += 2;
                        continue;
                    }
                    if (esp_cam_io_parl->info.index < esp_cam_io_parl->info.frame.length) {
                        esp_cam_io_parl->info.frame.buffer[esp_cam_io_parl->info.index++] = 0xFF;
                    }
                    else {
                        goto jpeg_overflow;
                    }
                    offset++; 
                }
                break;
            }
        }
    }
    esp_cam_io_parl->info.previous_byte = chunk[chunk_len - 1];
    if (_hp_task_woken) {
        portYIELD_FROM_ISR();
    }
    return true;

jpeg_overflow:
    ESP_EARLY_LOGW(TAG, "JPEG buffer overflow");
    goto err;
err:
    esp_cam_io_parl->info.state = ESP_CAM_IO_PARL_JPEG_IDLE;
    if (!xQueueSendFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_FAIL], &esp_cam_io_parl->info.frame, &_hp_task_woken)) {
        // Do nothing at the moment
    }
    if (_hp_task_woken) {
        portYIELD_FROM_ISR();
    }
    return true;
}

static void esp_cam_io_parl_task(void *user_data) {
    esp_cam_io_parl_handle_t esp_cam_io_parl = (esp_cam_io_parl_handle_t)user_data;
    while (true) {
        esp_cam_io_parl_trans_t frame;
        if (xQueueReceive(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_FAIL], &frame, 0)) {
            heap_caps_free(frame.buffer);
	    }
        frame.length = esp_cam_io_parl->alloc_size;
        frame.buffer = heap_caps_malloc(frame.length, esp_cam_io_parl->alloc_heap_caps);
        if (frame.buffer) {
            if (!xQueueSend(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_PENDING], &frame, 0)) {
                heap_caps_free(frame.buffer);
            }
        }
        vTaskDelay(1);
    }
}

static esp_err_t esp_cam_destroy_io_parl(esp_cam_io_parl_handle_t esp_cam_io_parl) {
    for (int i = 0; i < ESP_CAM_IO_PARL_QUEUE_MAX; i++) {
        if (esp_cam_io_parl->queue_handle[i]) {
            vQueueDeleteWithCaps(esp_cam_io_parl->queue_handle[i]);
        }
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
    heap_caps_free(esp_cam_io_parl);
    return ESP_OK;
}

esp_err_t esp_cam_new_io_parl(const esp_cam_io_parl_config_t *config, esp_cam_io_parl_handle_t *ret_handle) {
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config && ret_handle, ESP_ERR_INVALID_ARG, TAG, "Invalid argument");
    ESP_RETURN_ON_FALSE(__builtin_popcount(config->data_width) == 1, ESP_ERR_INVALID_ARG, TAG, "Data line number should be the power of 2 without counting valid signal");
    esp_cam_io_parl_handle_t esp_cam_io_parl = heap_caps_calloc(1, sizeof(esp_cam_io_parl_t), MALLOC_CAP_INTERNAL);
    ESP_GOTO_ON_FALSE(esp_cam_io_parl, ESP_ERR_NO_MEM, err, TAG, "No memory for allocating rx unit");
    esp_cam_io_parl->payload_size = CONFIG_ESP_CAM_IO_PARL_PAYLOAD_SIZE;
    gpio_num_t valid_gpio = config->de_io >= 0 ? config->de_io : (config->hsync_io >= 0 ? config->hsync_io : -1);
    uint32_t pclk_freq = config->pclk_hz > 0 ? config->pclk_hz : (80 * 1000 * 1000);
    parlio_rx_unit_config_t rx_unit_config = {
        .trans_queue_depth = 4,
        .max_recv_size = UINT16_MAX,
        .data_width = config->data_width,
        .clk_src = PARLIO_CLK_SRC_EXTERNAL,
        .ext_clk_freq_hz = pclk_freq,
        .exp_clk_freq_hz = pclk_freq,
        .clk_in_gpio_num = config->pclk_io,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = valid_gpio,
        .flags = {
            .free_clk = false,
            .allow_pd = config->flags.allow_pd,
        },
    };
    memcpy(rx_unit_config.data_gpio_nums, config->data_io, PARLIO_RX_UNIT_MAX_DATA_WIDTH * sizeof(gpio_num_t));
    ESP_GOTO_ON_ERROR(parlio_new_rx_unit(&rx_unit_config, &esp_cam_io_parl->rx_unit), err, TAG, "Failed to initialize rx unit");
    if (esp_cam_io_parl->payload) {
        heap_caps_free(esp_cam_io_parl->payload);
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
#if ESP_CAM_IO_PARL_EDGE_FIX
                .sample_edge = config->pclk_sample_edge,
#else
                .sample_edge = (config->pclk_sample_edge == PARLIO_SAMPLE_EDGE_POS) ? PARLIO_SAMPLE_EDGE_NEG : PARLIO_SAMPLE_EDGE_POS,
#endif
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
#if ESP_CAM_IO_PARL_EDGE_FIX
                .sample_edge = config->pclk_sample_edge,
#else
                .sample_edge = (config->pclk_sample_edge == PARLIO_SAMPLE_EDGE_POS) ? PARLIO_SAMPLE_EDGE_NEG : PARLIO_SAMPLE_EDGE_POS,
#endif
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
#if ESP_CAM_IO_PARL_EDGE_FIX
            .sample_edge = config->pclk_sample_edge,
#else
            .sample_edge = (config->pclk_sample_edge == PARLIO_SAMPLE_EDGE_POS) ? PARLIO_SAMPLE_EDGE_NEG : PARLIO_SAMPLE_EDGE_POS,
#endif
            .eof_data_len = MIN(esp_cam_io_parl->payload_size, UINT16_MAX),
            .timeout_ticks = 0,
        };
        ESP_GOTO_ON_ERROR(parlio_new_rx_soft_delimiter(&rx_delimiter_config, &esp_cam_io_parl->rx_delimiter), err, TAG, "Failed to initialize rx software delimiter");
        esp_cam_io_parl->use_soft_delimiter = true;
    }

    parlio_rx_event_callbacks_t cbs = {
        .on_partial_receive = esp_cam_io_parl_received_partial_data,
    };
    ESP_GOTO_ON_ERROR(parlio_rx_unit_register_event_callbacks(esp_cam_io_parl->rx_unit, &cbs, esp_cam_io_parl), err, TAG, "Failed to initialize partial receive callback");

    for (int i = 0; i < ESP_CAM_IO_PARL_QUEUE_MAX; i++) {
        int queue_length = (i == ESP_CAM_IO_PARL_QUEUE_FAIL ? MAX(config->queue_frames, 2) : config->queue_frames); // Force minimum queue length to 2 for error queue to prevent heap leaks
        esp_cam_io_parl->queue_handle[i] = xQueueCreateWithCaps(queue_length, sizeof(esp_cam_io_parl_trans_t), MALLOC_CAP_DEFAULT);
        ESP_GOTO_ON_FALSE(esp_cam_io_parl->queue_handle[i], ESP_ERR_NO_MEM, err, TAG, "No memory for queue");
    }

    esp_cam_io_parl->payload = heap_caps_calloc(1, esp_cam_io_parl->payload_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    ESP_GOTO_ON_FALSE(esp_cam_io_parl->payload, ESP_ERR_NO_MEM, err, TAG, "No memory for payload buffer");

    esp_cam_io_parl->alloc_heap_caps = config->frame_heap_caps ? config->frame_heap_caps : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

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
    //esp_cam_io_parl->info.state = ESP_CAM_IO_PARL_JPEG_IDLE;
    //ESP_RETURN_ON_ERROR(parlio_rx_soft_delimiter_start_stop(esp_cam_io_parl->rx_unit, esp_cam_io_parl->rx_delimiter, false), TAG, "Failed to start PARLIO RX soft delimiter");
    //ESP_LOGI(TAG, "CPU cycles for parlio_rx_soft_delimiter_start_stop: %u", end);
    esp_cam_io_parl->alloc_size = alloc_size;
    if (heap_caps) {
        esp_cam_io_parl->alloc_heap_caps = heap_caps;
    }
    //ESP_RETURN_ON_ERROR(parlio_rx_soft_delimiter_start_stop(esp_cam_io_parl->rx_unit, esp_cam_io_parl->rx_delimiter, true), TAG, "Failed to start PARLIO RX soft delimiter");
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
                .indirect_mount = false,
            },
        };
        ESP_RETURN_ON_ERROR(parlio_rx_unit_receive(esp_cam_io_parl->rx_unit, esp_cam_io_parl->payload, esp_cam_io_parl->payload_size, &receive_config), TAG, "Failed to receive from PARLIO RX");
    }
    ESP_RETURN_ON_FALSE(xTaskCreateWithCaps(esp_cam_io_parl_task, "esp_cam_io_parl_task", 4096, esp_cam_io_parl, 2, &esp_cam_io_parl->cam_task_handle, MALLOC_CAP_DEFAULT), ESP_ERR_NO_MEM, TAG, "Failed to allocate camera task");
    return ESP_OK;
}

esp_err_t esp_cam_io_parl_disable(esp_cam_io_parl_handle_t esp_cam_io_parl) {
    if (esp_cam_io_parl->cam_task_handle) {
        vTaskDeleteWithCaps(esp_cam_io_parl->cam_task_handle);
    }
    if (esp_cam_io_parl->use_soft_delimiter) {
        ESP_RETURN_ON_ERROR(parlio_rx_soft_delimiter_start_stop(esp_cam_io_parl->rx_unit, esp_cam_io_parl->rx_delimiter, false), TAG, "Failed to stop PARLIO RX soft delimiter");
    }
    ESP_RETURN_ON_ERROR(parlio_rx_unit_disable(esp_cam_io_parl->rx_unit), TAG, "Failed to disable PARLIO RX unit");
    return ESP_OK;
}

esp_err_t esp_cam_io_parl_receive(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, int32_t timeout_ms) {
    ESP_RETURN_ON_FALSE(esp_cam_io_parl && frame, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    //ESP_RETURN_ON_FALSE(esp_cam_io_parl->config.sampling_mode == ESP_CAM_IO_PARL_BUFFER, ESP_ERR_INVALID_STATE, TAG, "Sampling mode is invalid");
    TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t ret = xQueueReceive(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_READY], frame, ticks);
    if (ret == pdFALSE) {
        frame->buffer = NULL;
        frame->length = 0;
    }
    return ret == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp_cam_io_parl_receive_from_isr(esp_cam_io_parl_handle_t esp_cam_io_parl, esp_cam_io_parl_trans_t *frame, bool *hp_task_woken) {
    ESP_CAM_IO_PARL_CHECK_ISR(esp_cam_io_parl && frame, ESP_ERR_INVALID_ARG);
    ESP_CAM_IO_PARL_CHECK_ISR(xPortInIsrContext() == pdTRUE, ESP_ERR_INVALID_STATE);
    //ESP_CAM_IO_PARL_CHECK_ISR(esp_cam_io_parl->config.sampling_mode == ESP_CAM_IO_PARL_BUFFER, ESP_ERR_INVALID_STATE);
    BaseType_t _hp_task_woken = 0;
    BaseType_t ret = xQueueReceiveFromISR(esp_cam_io_parl->queue_handle[ESP_CAM_IO_PARL_QUEUE_READY], frame, &_hp_task_woken);
    if (hp_task_woken) {
        *hp_task_woken = _hp_task_woken != 0;
    }
    return ret == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_cam_io_parl_free_buffer(esp_cam_io_parl_trans_t *frame) {
    ESP_RETURN_ON_FALSE(frame->buffer, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    heap_caps_free(frame->buffer);
    frame->buffer = NULL;
    frame->length = 0;
    return ESP_OK;
}