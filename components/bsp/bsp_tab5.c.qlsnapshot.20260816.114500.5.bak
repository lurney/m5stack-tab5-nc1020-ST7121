#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "bsp/esp_lcd_st7123.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_tab5";
#include "driver/i2c_master.h"
static i2c_master_bus_handle_t s_sys_i2c = NULL;
static esp_lcd_touch_handle_t  s_tp = NULL;

static esp_codec_dev_handle_t s_play_dev = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;
static const audio_codec_data_if_t *s_i2s_data_if = NULL;

#define I2C_DEV_ADDR_PI4IOE1      0x43
#define I2C_DEV_ADDR_PI4IOE2      0x44
#define I2C_MASTER_TIMEOUT_MS     50

static i2c_master_dev_handle_t i2c_dev_handle_pi4ioe1;
static i2c_master_dev_handle_t i2c_dev_handle_pi4ioe2;

#define PI4IO_REG_CHIP_RESET      0x01
#define PI4IO_REG_IO_DIR          0x03
#define PI4IO_REG_OUT_SET         0x05
#define PI4IO_REG_OUT_H_IM        0x07
#define PI4IO_REG_IN_DEF_STA      0x09
#define PI4IO_REG_PULL_EN         0x0B
#define PI4IO_REG_PULL_SEL        0x0D
#define PI4IO_REG_IN_STA          0x0F
#define PI4IO_REG_INT_MASK        0x11

static void bsp_io_expander_pi4ioe_init(i2c_master_bus_handle_t bus_handle)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1]  = {0};

    i2c_device_config_t dev_cfg1 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_DEV_ADDR_PI4IOE1,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg1, &i2c_dev_handle_pi4ioe1));

    write_buf[0] = PI4IO_REG_CHIP_RESET;
    write_buf[1] = 0xFF;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_CHIP_RESET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_IO_DIR;
    write_buf[1] = 0b01111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_OUT_H_IM;
    write_buf[1] = 0b00000000;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_PULL_SEL;
    write_buf[1] = 0b01111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_PULL_EN;
    write_buf[1] = 0b01111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = 0b01110110;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    i2c_device_config_t dev_cfg2 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_DEV_ADDR_PI4IOE2,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg2, &i2c_dev_handle_pi4ioe2));

    write_buf[0] = PI4IO_REG_CHIP_RESET;
    write_buf[1] = 0xFF;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_CHIP_RESET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_IO_DIR;
    write_buf[1] = 0b10111001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_OUT_H_IM;
    write_buf[1] = 0b00000110;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_PULL_SEL;
    write_buf[1] = 0b10111001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_PULL_EN;
    write_buf[1] = 0b11111001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_IN_DEF_STA;
    write_buf[1] = 0b01000000;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_INT_MASK;
    write_buf[1] = 0b10111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = 0b00001001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
}

static void bsp_reset_tp(void)
{
    gpio_reset_pin(GPIO_NUM_23);
    uint8_t read_buf[1]  = {0};
    uint8_t write_buf[2] = {0};

    write_buf[0] = PI4IO_REG_IN_STA;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0] & ~((1 << 4) | (1 << 5));
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(100));
    write_buf[1] = read_buf[0] | ((1 << 4) | (1 << 5));
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* Diagnostic only: probe the touch controller on the SYS I2C bus to infer
 * which panel variant is fitted. ST7123/ST7121 TDDI answers at 0x55; the
 * other Tab5 variant uses a GT911 (0x5D/0x14) paired with an ILI9881C panel,
 * for which this ST7123-only build will NOT light up. */
static void bsp_probe_panel_type(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, 0x55, 50) == ESP_OK) {
        ESP_LOGI(TAG, "PANEL PROBE: touch @0x55 present -> ST7123/ST7121 TDDI (matches this build)");
    } else if (i2c_master_probe(bus, 0x5D, 50) == ESP_OK ||
               i2c_master_probe(bus, 0x14, 50) == ESP_OK) {
        ESP_LOGE(TAG, "PANEL PROBE: GT911 touch present (0x5D/0x14) -> hardware is ILI9881C, NOT ST7123!");
        ESP_LOGE(TAG, "PANEL PROBE: this ST7123-only build will stay DARK on this panel.");
    } else {
        ESP_LOGW(TAG, "PANEL PROBE: no known touch ctrl (0x55/0x5D/0x14) -> check I2C wiring / power / TP_RST");
    }
}

static esp_err_t bsp_enable_dsi_phy_power(void)
{
    static esp_ldo_channel_handle_t phy_chan = NULL;
    if (phy_chan) return ESP_OK;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = 3,
        .voltage_mv = 2500,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_chan), TAG, "LDO");
    ESP_LOGI(TAG, "DSI PHY powered on (LDO CH3 2500mV)");
    return ESP_OK;
}

#define BSP_LCD_BACKLIGHT       GPIO_NUM_22
#define BSP_LCD_BK_LIGHT_ON     1
#define BSP_LCD_BK_LIGHT_OFF    0
#define BSP_LCD_LEDC_TIMER      LEDC_TIMER_0
#define BSP_LCD_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BSP_LCD_LEDC_CHANNEL    LEDC_CHANNEL_1
#define BSP_LCD_LEDC_DUTY_RES   LEDC_TIMER_12_BIT
#define BSP_LCD_LEDC_FREQUENCY  5000
#define BSP_LCD_BRIGHTNESS_MAX  100

esp_err_t bsp_display_brightness_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = BSP_LCD_LEDC_MODE,
        .duty_resolution = BSP_LCD_LEDC_DUTY_RES,
        .timer_num       = BSP_LCD_LEDC_TIMER,
        .freq_hz         = BSP_LCD_LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer");

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = BSP_LCD_BACKLIGHT,
        .speed_mode = BSP_LCD_LEDC_MODE,
        .channel    = BSP_LCD_LEDC_CHANNEL,
        .timer_sel  = BSP_LCD_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc channel");
    ESP_RETURN_ON_ERROR(ledc_set_duty(BSP_LCD_LEDC_MODE, BSP_LCD_LEDC_CHANNEL, 0), TAG, "ledc duty");
    return ledc_update_duty(BSP_LCD_LEDC_MODE, BSP_LCD_LEDC_CHANNEL);
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) brightness_percent = 100;
    if (brightness_percent < 0) brightness_percent = 0;
    uint32_t duty = (brightness_percent * ((1 << BSP_LCD_LEDC_DUTY_RES) - 1)) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BSP_LCD_LEDC_MODE, BSP_LCD_LEDC_CHANNEL, duty), TAG, "ledc set");
    return ledc_update_duty(BSP_LCD_LEDC_MODE, BSP_LCD_LEDC_CHANNEL);
}

static const st7123_lcd_init_cmd_t st7123_vendor_specific_init_default[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF},
     40, 0},
    {0xA6, (uint8_t[]){0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24,
                       0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00,
                       0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E,
                       0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00},
     55, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44},
     60, 0},
    {0xAC, (uint8_t[]){0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C,
                       0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12,
                       0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01},
     44, 0},
    {0xAD, (uint8_t[]){0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},
     25, 0},
    {0xAE, (uint8_t[]){0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00}, 7, 0},
    {0xB2, (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20, 0xFD, 0x20, 0xC0, 0x00}, 17, 0},
    {0xE8, (uint8_t[]){0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 15, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00,
                       0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45,
                       0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77},
     36, 0},
    {0xEA, (uint8_t[]){0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00, 0x00, 0x73, 0x73}, 0x04, 0},
    {0xBF, (uint8_t[]){0xA6, 0XAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 100},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 100},
};

static esp_lcd_dsi_bus_handle_t s_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t s_disp_io = NULL;
static esp_lcd_panel_handle_t s_disp_panel = NULL;

esp_lcd_panel_handle_t bsp_get_panel_handle(void)
{
    return s_disp_panel;
}

esp_err_t bsp_display_init(void)
{
    ESP_LOGI(TAG, "=== BSP Display Init (UserDemo code) ===");

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "backlight");

    /* Diagnostic: light the backlight BEFORE DSI/panel init. If the screen is
     * fully dark from here on, the fault is power/LDO/backlight; if it lights
     * (white/grey) but shows no image, the fault is panel init or the draw
     * path. (bsp_display_brightness_set(100) is also called again at the end.) */
    bsp_display_brightness_set(100);

    ESP_RETURN_ON_ERROR(bsp_enable_dsi_phy_power(), TAG, "DSI PHY");

    /* MIPI DSI bus: 2 lanes, 965 Mbps */
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id             = 0,
        .num_data_lanes     = 2,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 965,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &s_dsi_bus), TAG, "DSI bus");

    /* DBI IO */
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_disp_io), TAG, "panel IO");

    /* DPI config */
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 70,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 1,
        .video_timing = {
            .h_size            = 720,
            .v_size            = 1280,
            .hsync_pulse_width = 2,
            .hsync_back_porch  = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 2,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 220,
        },
        .flags = { .use_dma2d = true },
    };

    st7123_vendor_config_t vendor_config = {
        .init_cmds      = st7123_vendor_specific_init_default,
        .init_cmds_size = sizeof(st7123_vendor_specific_init_default) / sizeof(st7123_vendor_specific_init_default[0]),
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num   = 2,
        },
    };

    const esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
        .vendor_config  = &vendor_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7123(s_disp_io, &lcd_dev_config, &s_disp_panel), TAG, "new panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_disp_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_disp_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_disp_panel, true), TAG, "display on");

    /* Turn on backlight to 100% */
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(100), TAG, "brightness");

    ESP_LOGI(TAG, "Display init complete (720x1280, 70MHz DPI, 965Mbps DSI)");
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_get_sys_i2c(void)
{
    return s_sys_i2c;
}

/* ST7123 is a TDDI panel — touch shares the SYS I2C bus and only works after
 * the LCD is initialized. We poll over I2C (no INT pin) to keep it simple. */
esp_err_t bsp_touch_init(void)
{
    if (s_tp) return ESP_OK;
    if (!s_sys_i2c) return ESP_ERR_INVALID_STATE;

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_sys_i2c, &io_cfg, &tp_io), TAG, "tp io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = 720,
        .y_max        = 1280,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = { .reset = 0, .interrupt = 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_st7123(tp_io, &tp_cfg, &s_tp), TAG, "new tp");
    ESP_LOGI(TAG, "ST7123 touch initialized (poll mode)");
    return ESP_OK;
}

bool bsp_touch_read(uint16_t *x, uint16_t *y)
{
    if (!s_tp) return false;
    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) return false;
    uint16_t tx[1] = {0}, ty[1] = {0}, str[1] = {0};
    uint8_t cnt = 0;
    bool pressed = esp_lcd_touch_get_coordinates(s_tp, tx, ty, str, &cnt, 1);
    if (pressed && cnt > 0) {
        if (x) *x = tx[0];
        if (y) *y = ty[0];
        return true;
    }
    return false;
}

int bsp_touch_read_points(uint16_t *xs, uint16_t *ys, int max_pts)
{
    if (!s_tp || max_pts <= 0) return 0;
    esp_err_t rd = esp_lcd_touch_read_data(s_tp);
    if (rd != ESP_OK) {
        static int64_t last_warn = 0;
        int64_t now = esp_timer_get_time();
        if (now - last_warn > 1000000) {
            last_warn = now;
            ESP_LOGW(TAG, "touch read_data failed: %s", esp_err_to_name(rd));
        }
        return 0;
    }
    uint16_t tx[8] = {0}, ty[8] = {0}, str[8] = {0};
    uint8_t cnt = 0;
    int cap = max_pts > 8 ? 8 : max_pts;
    esp_lcd_touch_get_coordinates(s_tp, tx, ty, str, &cnt, cap);
    int n = cnt > cap ? cap : cnt;
    for (int i = 0; i < n; i++) {
        if (xs) xs[i] = tx[i];
        if (ys) ys[i] = ty[i];
    }
    return n;
}

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "=== BSP Board Init ===");

    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_31,
        .scl_io_num = GPIO_NUM_32,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = 1 },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &s_sys_i2c));
    ESP_LOGI(TAG, "SYS I2C init (SDA=31, SCL=32)");

    ESP_LOGI(TAG, "IO expander init (PI4IOE5V6408)...");
    bsp_io_expander_pi4ioe_init(s_sys_i2c);

    ESP_LOGI(TAG, "LCD+TP reset pulse...");
    bsp_reset_tp();

    bsp_probe_panel_type(s_sys_i2c);

    ESP_LOGI(TAG, "Display init...");
    ESP_ERROR_CHECK(bsp_display_init());

    ESP_LOGI(TAG, "Touch init...");
    esp_err_t terr = bsp_touch_init();
    if (terr != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (0x%x) — browser navigation will not work", terr);
    }

    ESP_LOGI(TAG, "Audio init (UserDemo ES8388)...");
    esp_err_t aerr = bsp_audio_codec_init();
    if (aerr != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed (0x%x)", aerr);
    } else {
        /* Play test beep through UserDemo codec */
        vTaskDelay(pdMS_TO_TICKS(200));
        int16_t beep[256];
        for (int i = 0; i < 128; i++) {
            int16_t v = (i < 64) ? 12000 : -12000;
            beep[i*2] = v; beep[i*2+1] = v;
        }
        esp_codec_dev_write(s_play_dev, beep, sizeof(beep));
        ESP_LOGI(TAG, "Test beep written");
    }

    ESP_LOGI(TAG, "=== Board Init Complete ===");
    return ESP_OK;
}

/* ─── Audio: UserDemo's ES8388 speaker init ───────────────────── */
esp_err_t bsp_audio_codec_init(void)
{
    if (s_play_dev) return ESP_OK;
    if (!s_sys_i2c) return ESP_ERR_INVALID_STATE;

    /* I2S: TX + RX */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx), TAG, "audio i2s_new");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = 30, .bclk = 27, .ws = 29, .dout = 26, .din = 28,
                      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "audio tx_std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "audio tx_en");

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = { .sample_rate_hz = 48000, .clk_src = I2S_CLK_SRC_DEFAULT,
                     .mclk_multiple = I2S_MCLK_MULTIPLE_256, .bclk_div = 8 },
        .slot_cfg = { .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                      .slot_mode = I2S_SLOT_MODE_STEREO,
                      .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
                      .ws_width = I2S_TDM_AUTO_WS_WIDTH, .ws_pol = false, .bit_shift = true,
                      .left_align = false, .big_endian = false, .bit_order_lsb = false,
                      .skip_mask = false, .total_slot = I2S_TDM_AUTO_SLOT_NUM },
        .gpio_cfg = { .mclk = 30, .bclk = 27, .ws = 29, .dout = 26, .din = 28,
                      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(s_i2s_rx, &tdm_cfg), TAG, "audio rx_tdm");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "audio rx_en");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = 1, .tx_handle = s_i2s_tx, .rx_handle = s_i2s_rx,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_i2s_data_if) { ESP_LOGE(TAG, "i2s_data_if failed"); return ESP_FAIL; }

    audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0, .addr = ES8388_CODEC_DEFAULT_ADDR, .bus_handle = s_sys_i2c,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) { ESP_LOGE(TAG, "i2c_ctrl_if failed"); return ESP_FAIL; }

    esp_codec_dev_hw_gain_t gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 };

    es8388_codec_cfg_t es_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .master_mode = false,
        .ctrl_if = ctrl_if, .pa_pin = -1, .hw_gain = gain,
    };
    const audio_codec_if_t *es_dev = es8388_codec_new(&es_cfg);
    if (!es_dev) { ESP_LOGE(TAG, "es8388_codec_new failed"); return ESP_FAIL; }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = es_dev, .data_if = s_i2s_data_if,
    };
    s_play_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_play_dev) { ESP_LOGE(TAG, "esp_codec_dev_new failed"); return ESP_FAIL; }

    esp_codec_dev_close(s_play_dev);
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 2, .sample_rate = 48000 };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_play_dev, &fs), TAG, "codec_open");

    esp_codec_dev_set_out_mute(s_play_dev, false);
    esp_codec_dev_set_out_vol(s_play_dev, 80);
    ESP_LOGI(TAG, "ES8388 speaker ready (48kHz, I2S1)");
    return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_get_play_dev(void)
{
    return s_play_dev;
}
