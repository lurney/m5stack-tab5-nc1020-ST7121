#pragma once

#include "bsp/config.h"
#include "bsp/display.h"
#include "bsp/esp_lcd_st7123.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_codec_dev.h"

esp_err_t bsp_board_init(void);

esp_lcd_panel_handle_t bsp_get_panel_handle(void);

/* The shared SYS I2C bus (touch, IO expander, ES8388 codec all live on it). */
i2c_master_bus_handle_t bsp_get_sys_i2c(void);

/* Initialize the ST7123 TDDI touch (called by bsp_board_init after the LCD). */
esp_err_t bsp_touch_init(void);

/* Poll one touch point. Returns true if pressed, with raw panel coords
 * (x in [0,720), y in [0,1280)) written to the x and y out-params. */
bool bsp_touch_read(uint16_t *x, uint16_t *y);

/* Poll up to max_pts simultaneous touch points (multi-touch, for the virtual
 * gamepad). Writes raw panel coords into xs[]/ys[]; returns the point count. */
int bsp_touch_read_points(uint16_t *xs, uint16_t *ys, int max_pts);

/* UserDemo-style ES8388 audio init */
esp_err_t bsp_audio_codec_init(void);

/* Return the playback device handle for audio output */
esp_codec_dev_handle_t bsp_audio_get_play_dev(void);

#ifdef __cplusplus
}
#endif
