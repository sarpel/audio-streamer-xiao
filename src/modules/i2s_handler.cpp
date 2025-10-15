#include "i2s_handler.h"
#include "../config.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2S_HANDLER";
static uint32_t overflow_count = 0;
static uint32_t underflow_count = 0;
static i2s_chan_handle_t rx_chan = NULL;

bool i2s_handler_init(void)
{
    esp_err_t ret;

    // I2S channel configuration (master, RX only)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return false;
    }

    // Clock configuration: 16 kHz, no MCLK
    i2s_std_clk_config_t clk_cfg = {
        .sample_rate_hz = I2S_SAMPLE_RATE,
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256};

    // Slot: Philips standard, mono-left, 32-bit slot, 24-bit data
    i2s_std_slot_config_t slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_24BIT,
        .slot_bit_width = (i2s_slot_bit_width_t)I2S_SLOT_BIT_WIDTH,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .slot_mask = I2S_STD_SLOT_LEFT,
        .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
        .ws_pol = false,
        .bit_shift = true,
        .left_align = true,
        .big_endian = false,
        .bit_order_lsb = false};

    // GPIO configuration
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)I2S_BCLK_GPIO,
        .ws = (gpio_num_t)I2S_WS_GPIO,
        .dout = I2S_GPIO_UNUSED,
        .din = (gpio_num_t)I2S_SD_GPIO,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };

    // Combined standard configuration
    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg};

    // Initialize I2S channel with standard mode
    ret = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
        return false;
    }

    // Enable the I2S channel
    ret = i2s_channel_enable(rx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
        return false;
    }

    ESP_LOGI(TAG, "I2S initialized successfully (Philips standard, 32-bit slot, 24-bit data, mono-left)");
    ESP_LOGI(TAG, "Sample rate: %d Hz, BCLK: GPIO%d, WS: GPIO%d, SD: GPIO%d",
             I2S_SAMPLE_RATE, I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_SD_GPIO);

    return true;
}

bool i2s_handler_read(int32_t *buffer, size_t samples_to_read, size_t *bytes_read)
{
    if (buffer == NULL || bytes_read == NULL)
    {
        ESP_LOGE(TAG, "Invalid buffer pointer");
        return false;
    }

    if (rx_chan == NULL)
    {
        ESP_LOGE(TAG, "I2S channel not initialized");
        return false;
    }

    size_t bytes_to_read = samples_to_read * sizeof(int32_t);
    esp_err_t ret = i2s_channel_read(rx_chan, buffer, bytes_to_read, bytes_read, portMAX_DELAY);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Check for buffer issues
    if (*bytes_read < bytes_to_read)
    {
        underflow_count++;
        ESP_LOGW(TAG, "I2S underflow detected (requested: %zu, got: %zu)",
                 bytes_to_read, *bytes_read);

        // ✅ Add recovery for persistent underflows
        if (underflow_count > I2S_UNDERFLOW_THRESHOLD)
        {
            ESP_LOGE(TAG, "Too many I2S underflows (%lu), may need reinit", underflow_count);
            // Signal main.cpp to reinitialize I2S
            return false; // Trigger failure handling in i2s_reader_task
        }
    }

    return true;
}

// New function: Read 24-bit samples and convert to 16-bit
size_t i2s_read_16(int16_t *out, size_t samples)
{
    if (out == NULL)
    {
        ESP_LOGE(TAG, "Invalid output buffer");
        return 0;
    }

    if (rx_chan == NULL)
    {
        ESP_LOGE(TAG, "I2S channel not initialized");
        return 0;
    }

    const size_t bytes_to_read = samples * sizeof(int32_t);
    static int32_t tmp[I2S_READ_SAMPLES];
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(rx_chan, tmp, bytes_to_read, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return 0;
    }

    // Convert 24-bit (in 32-bit container) to 16-bit by shifting right 8 bits
    size_t n = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < n; ++i)
    {
        out[i] = (int16_t)(tmp[i] >> 8);
    }

    return n;
}

void i2s_handler_deinit(void)
{
    if (rx_chan != NULL)
    {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
        ESP_LOGI(TAG, "I2S channel deleted");
    }
}

void i2s_handler_get_stats(uint32_t *overflow_cnt, uint32_t *underflow_cnt)
{
    if (overflow_cnt)
        *overflow_cnt = overflow_count;
    if (underflow_cnt)
        *underflow_cnt = underflow_count;
}
