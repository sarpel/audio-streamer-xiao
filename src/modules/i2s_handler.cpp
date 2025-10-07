#include "i2s_handler.h"
#include "../config.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "I2S_HANDLER";
static uint32_t overflow_count = 0;
static uint32_t underflow_count = 0;
static i2s_chan_handle_t rx_handle = NULL;

bool i2s_handler_init(void) {
    esp_err_t ret;

    // Determine I2S data bit width from config
    i2s_data_bit_width_t data_bit_width;
    if (BITS_PER_SAMPLE == 16) {
        data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    } else if (BITS_PER_SAMPLE == 24) {
        data_bit_width = I2S_DATA_BIT_WIDTH_24BIT;
    } else if (BITS_PER_SAMPLE == 32) {
        data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    } else {
        ESP_LOGE(TAG, "Unsupported bit depth: %d", BITS_PER_SAMPLE);
        return false;
    }

    // I2S channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUF_LEN;
    chan_cfg.auto_clear = false;

    // Create I2S RX channel
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return false;
    }

    // I2S standard mode configuration for INMP441
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(data_bit_width, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCK_PIN,
            .ws = (gpio_num_t)I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)I2S_DATA_IN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Initialize I2S channel with standard mode
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return false;
    }

    // Enable the I2S channel
    ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return false;
    }

    ESP_LOGI(TAG, "I2S initialized successfully (new API)");
    ESP_LOGI(TAG, "Sample rate: %d Hz, Bits: %d, Channels: %d",
             SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS);

    return true;
}

bool i2s_handler_read(int32_t* buffer, size_t samples_to_read, size_t* bytes_read) {
    if (buffer == NULL || bytes_read == NULL) {
        ESP_LOGE(TAG, "Invalid buffer pointer");
        return false;
    }

    if (rx_handle == NULL) {
        ESP_LOGE(TAG, "I2S channel not initialized");
        return false;
    }

    size_t bytes_to_read = samples_to_read * sizeof(int32_t);
    esp_err_t ret = i2s_channel_read(rx_handle, buffer, bytes_to_read, bytes_read, portMAX_DELAY);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Check for buffer issues
    if (*bytes_read < bytes_to_read) {
        underflow_count++;
        ESP_LOGW(TAG, "I2S underflow detected (requested: %zu, got: %zu)",
                 bytes_to_read, *bytes_read);
    }

    return true;
}

void i2s_handler_deinit(void) {
    if (rx_handle != NULL) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        ESP_LOGI(TAG, "I2S channel deleted");
    }
}

void i2s_handler_get_stats(uint32_t* overflow_cnt, uint32_t* underflow_cnt) {
    if (overflow_cnt) *overflow_cnt = overflow_count;
    if (underflow_cnt) *underflow_cnt = underflow_count;
}
