#include "board_7b.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Board wiring/timings: Waveshare ESP32-S3-Touch-LCD-7B examples at
 * c652c902db607f7ffb376257393cfd7657aa6428 (06_LCD and 08_Touch).
 * The 7B expander uses address 0x24 and register writes, not the 7's CH422G.
 */
static esp_lcd_panel_handle_t panel;
static i2c_master_dev_handle_t expander, touch;
static const char *TAG = "board_7b";

static void expander_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(expander, data, sizeof(data), 100));
}

void board_init(void)
{
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 8, .scl_io_num = 9,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x24,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &expander));
    /* EXIO6 panel power high, EXIO4 SD deselected, EXIO5 USB selected.
     * Keep backlight off during startup and hold touch reset low. */
    uint8_t outputs = 0xffu & ~(1u << 5) & ~(1u << 2) & ~(1u << 1);
    expander_write(0x02, 0xff);
    expander_write(0x03, outputs);
    const gpio_config_t irq_config = {
        .pin_bit_mask = 1ULL << 4, .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&irq_config));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(4, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    outputs |= 1u << 1;
    expander_write(0x03, outputs);
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_ERROR_CHECK(gpio_set_direction(4, GPIO_MODE_INPUT));
    /* INT low during reset selects the GT911's 0x5d address. */
    device.device_address = 0x5d;
    if (i2c_master_probe(bus, device.device_address, 100) == ESP_OK) {
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &touch));
        ESP_LOGI(TAG, "GT911 detected; tap to pause, hold to reseed");
    } else {
        ESP_LOGW(TAG, "GT911 absent; animation will run without touch control");
    }
    const esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 30000000, .h_res = BOARD_WIDTH, .v_res = BOARD_HEIGHT,
            .hsync_pulse_width = 162, .hsync_back_porch = 152, .hsync_front_porch = 48,
            .vsync_pulse_width = 45, .vsync_back_porch = 13, .vsync_front_porch = 3,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16, .bits_per_pixel = 16, .num_fbs = 1,
        .bounce_buffer_size_px = BOARD_WIDTH * 10,
        .dma_burst_size = 64,
        .hsync_gpio_num = 46, .vsync_gpio_num = 3, .de_gpio_num = 5,
        .pclk_gpio_num = 7, .disp_gpio_num = -1,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    /* PWM value 0 is maximum brightness in the Waveshare example. */
    expander_write(0x05, 0);
    expander_write(0x03, outputs | (1u << 2));
    ESP_LOGI(TAG, "1024x600 RGB565 LCD initialized");
}

void board_present(const uint16_t *pixels)
{
    /* The driver copies the staging image into its PSRAM framebuffer and
     * performs cache synchronization. Continuous scanout can show tearing. */
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, BOARD_WIDTH, BOARD_HEIGHT, pixels));
}

bool board_touch_report(bool *pressed)
{
    if (!touch) return false;
    const uint8_t reg[] = {0x81, 0x4e};
    uint8_t status;
    if (i2c_master_transmit_receive(touch, reg, sizeof(reg), &status, 1, 20) != ESP_OK)
        return false;
    if (!(status & 0x80)) return false;
    const uint8_t ack[] = {0x81, 0x4e, 0};
    if (i2c_master_transmit(touch, ack, sizeof(ack), 20) != ESP_OK) return false;
    if ((status & 0x0f) > 5) return false;
    *pressed = (status & 0x0f) != 0;
    return true;
}
