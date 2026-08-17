/*
 * nc2000_run.c — full-screen NC2000 / NC1020 (wangyu wqx) front-end for the
 * M5Stack Tab5 (ESP32-P4). Adapted from the tab-nc1020 glue: the panel layout,
 * PPA scale+rotate pipeline, hardware-keyboard + touch input and the two UI
 * modes are identical (the wqx LCD is the same 160x80 panel and SetKey uses the
 * same key codes). The differences are: (1) it drives the wangyu core through
 * the nc2000_api.h shim, (2) it renders 2bpp grayscale as well as 1bpp, and
 * (3) it is SD-only (ROM/NOR/NAND/state all live next to `base` on the card).
 */
#include "nc2000_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"

#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_cache.h"
#include "ppa_engine.h"
#include "nc_cjk.h"
#include "kbd_hw.h"
#include "nc2000_api.h"

/* 8x16 ASCII font from the odroid component (linked in), for key labels. */
extern const uint8_t vpad_font8[95][16];

static const char *TAG = "NC2000_RUN";

#define NC2K_LCD_W      160
#define NC2K_LCD_H      80
#define FRAME_RATE        30
#define FRAME_INTERVAL_MS (1000 / FRAME_RATE)

/* 2bpp grey needs 160*80*2/8 = 3200 bytes; 1bpp uses the first 1600. */
static uint8_t  lcd_buf[NC2K_LCD_W * NC2K_LCD_H / 8 * 2];
static uint16_t *rgb565_buf = NULL;

/* 4-level grayscale palette (value 0=off/white .. 3=on/black), rgb565. */
static const uint16_t GREY4[4] = { 0xFFFF, 0xAD55, 0x52AA, 0x0000 };

#define KEY_SIZE 0x40
static bool kb_state[KEY_SIZE] = {0};   /* last key level we pushed        */
static bool hw_down[KEY_SIZE]  = {0};   /* hardware-keyboard held state    */
static volatile bool nc2k_quit_flag = false;

static nc2k_mode_t s_mode = NC2K_MODE_NC2000;
static char s_base[256];                /* base path on SD (no suffix)     */

/* ─── panel / canvas geometry ────────────────────────────────────────────────
 * Physical ST7123 panel is 720x1280 (portrait). We compose on a 1280x720
 * landscape canvas and rotate it 90° onto the panel. Forward map: landscape
 * (lx,ly) -> panel (px=ly, py=PORT_H-1-lx). Touch inverse: lx = PORT_H-1-py,
 * ly = px. */
#define PORT_W   720
#define PORT_H   1280
#define LAND_W   1280
#define LAND_H   720
#define UI_ROT   90

/* ─── two UI modes ───────────────────────────────────────────────────────── */
#define UI_HW    0
#define UI_VIRT  1
static int s_ui_mode = UI_HW;
static volatile bool s_mode_dirty = false;

/* HW mode: LCD 1280x640 @ (0,0) scale 8x; strip 1280x80 @ (0,640). */
#define HW_LCD_SCALE  8.0f
#define HW_LCD_W      1280
#define HW_LCD_H      640
#define HW_FN_Y       640
#define HW_FN_H       80
#define HW_FN_COLS    16
#define HW_FN_KW      (LAND_W / HW_FN_COLS)   /* 80 */

/* VIRT mode: 10x8 grid (KW 128, RH 90); LCD spans cols 2..7, rows 0..3. */
#define V_COLS    10
#define V_KW      128
#define V_RH      90
#define V_KBD_Y   (4 * V_RH)                  /* 360 */
#define V_LCD_SX  4.5f
#define V_LCD_SY  4.8f
#define V_LCD_W   768
#define V_LCD_H   360
#define V_LCD_LX  (2 * V_KW)                  /* 256 */
#define V_LCD_LY  0

#define KEY_PAD   3

/* Synthetic (on-screen-only) codes. */
#define KEY_EXIT    0xFE   /* 切换 — leave the emulator       */
#define KEY_SAVE    0xFD   /* 保存 — save state now            */
#define KEY_FFON    0xFC   /* 加速 — fast-forward on           */
#define KEY_FFOFF   0xFB   /* 还原 — normal speed              */
#define KEY_KTOGGLE 0xFA   /* 软键/全屏 — switch keyboard mode */
#define KEY_NONE    0xFF

typedef struct { uint8_t code; const char *label; } nkey_t;

/* HW-mode function strip (16 keys; last one toggles to the virtual keyboard). */
static const nkey_t FN[HW_FN_COLS] = {
    {0x0b, "英汉"}, {0x0c, "名片"}, {0x0d, "计算"}, {0x0a, "行程"},
    {0x09, "测验"}, {0x08, "其他"}, {0x0e, "网络"}, {0x0f, "电源"},
    {0x10, "F1"},   {0x11, "F2"},   {0x12, "F3"},   {0x13, "F4"},
    {0x38, "帮助"}, {0x37, "上页"}, {0x1e, "下页"},
    {KEY_KTOGGLE, "软键"},
};

/* VIRT-mode 10x8 face. */
#define N  {KEY_NONE, ""}
static const nkey_t KB[8][V_COLS] = {
  { {0x0f,"电源"},{0x0b,"英汉"}, N,N,N,N,N,N, {KEY_SAVE,"保存"},{KEY_KTOGGLE,"全屏"} },
  { {0x0c,"名片"},{0x0d,"计算"}, N,N,N,N,N,N, {KEY_FFON,"加速"},{KEY_FFOFF,"还原"} },
  { {0x0a,"行程"},{0x09,"测验"}, N,N,N,N,N,N, {0x10,"F1"},{0x11,"F2"} },
  { {0x08,"其他"},{0x0e,"网络"}, N,N,N,N,N,N, {0x12,"F3"},{0x13,"F4"} },
  { {0x20,"Q"},{0x21,"W"},{0x22,"E"},{0x23,"R"},{0x24,"T 7"},{0x25,"Y 8"},{0x26,"U 9"},{0x27,"I"},{0x18,"O"},{0x1c,"P"} },
  { {0x28,"A"},{0x29,"S"},{0x2a,"D"},{0x2b,"F"},{0x2c,"G 4"},{0x2d,"H 5"},{0x2e,"J 6"},{0x2f,"K"},{0x19,"L"},{0x1d,"Enter"} },
  { {0x30,"Z"},{0x31,"X"},{0x32,"C"},{0x33,"V"},{0x34,"B 1"},{0x35,"N 2"},{0x36,"M 3"},{0x37,"PgUp"},{0x1a,""},{0x1e,"PgDn"} },
  { {0x38,"Help"},{0x39,"Shift"},{0x3a,"Caps"},{0x3b,"Esc"},{0x3c,"0"},{0x3d,"."},{0x3e,"="},{0x3f,""},{0x1b,""},{0x1f,""} },
};

/* Detachable M5Tab5 hardware keyboard: (row,col) -> wqx/NC1020 key code (the
 * codes match key.cpp map_key). 0xFF = no NC1020 equivalent. Physical layout
 * from M5Tab5-Keyboard-UserDemo m5tab5_keyboard_def.h (5 rows x 14 cols):
 *  R0: Esc 1 2 3 4 5 6 7 8 9 0 - + Del
 *  R1: ` ! @ # $ % ^ & * ( ) [ ] '\'   (symbol row — no NC1020 keys)
 *  R2: Tab Q W E R T Y U I O P ; ' BkSp
 *  R3: Sym Aa A S D F G H J K L Up _ Enter
 *  R4: Ctrl Alt Z X C V B N M . Left Down Right Space
 * NC1020 shares some codes (letter/digit, backspace/left, space/=). */
#define X 0xFF
static const uint8_t HWMAP[5][14] = {
  /* R0 */ { 0x3b,0x34,0x35,0x36,0x2c,0x2d,0x2e,0x24,0x25,0x26,0x3c, X,  0x3e,0x3f },
  /* R1 */ {  X,  X,  X,  X,  X,  X,  X,  X,  X,  X,  X,  X,  X,  X },
  /* R2 */ { 0x38,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x18,0x1c,0x15,0x14,0x3f },
  /* R3 */ { 0x3a,0x39,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x19,0x1a, X, 0x1d },
  /* R4 */ {  X,  X, 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x3d,0x3f,0x1b,0x1f,0x3e },
};
#undef X

static uint16_t *s_lcd = NULL;   /* PPA output for the LCD (max 640x1280)  */
static volatile bool s_kbd_exit = false;
static volatile bool s_save_req = false;
static volatile bool s_fast     = false;

#define KC_BG    0x0000
#define KC_KEY   0x4a49
#define KC_APP   0x82A6
#define KC_FN    0x6B4D
#define KC_SAVE  0x05E0
#define KC_FF    0xC340
#define KC_EXIT  0x8410
#define KC_TGL   0x051F
#define KC_TXT   0xFFFF
#define KC_LCDBG 0xFFFF

static uint16_t key_color(uint8_t code)
{
    switch (code) {
        case KEY_SAVE:    return KC_SAVE;
        case KEY_EXIT:    return KC_EXIT;
        case KEY_KTOGGLE: return KC_TGL;
        case KEY_FFON:
        case KEY_FFOFF:   return KC_FF;
    }
    if (code >= 0x08 && code <= 0x0f) return KC_APP;
    if (code >= 0x10 && code <= 0x13) return KC_FN;
    return KC_KEY;
}

/* ─── brightness overlay ─────────────────────────────────────────────── */
static int s_brightness = 100;          /* current brightness 0-100        */
static int s_swipe_origin_y = -1;       /* landscape Y where swipe started */
static int64_t s_brightness_last_change = 0; /* esp_timer_get_time() of last change */

#define BRIGHTNESS_OVERLAY_HIDE_MS  5000
#define BRIGHTNESS_STEP             5
#define BRIGHTNESS_SWIPE_THRESHOLD  20   /* pixels of vertical travel to trigger */

/* Draw the brightness overlay into s_lcd (the PPA output buffer, HW_LCD_W×HW_LCD_H).
 * Drawn AFTER PPA transform so coordinates are in final display space.
 * HW  mode: PPA outputs 640×640 → overlay at (620,10) is within PPA crop
 * VIRT mode: PPA outputs 768×360 → overlay at (700,10) is within PPA crop
 * nc2000 LCD HW  : x 0-639, y 0-639 → overlay briefly covers corner pixels (acceptable)
 * nc2000 LCD VIRT: x 0-767, y 0-359 → overlay briefly covers corner pixels
 * Background: black; text: white; drawn with bounds check into s_lcd. */
static void draw_brightness_overlay(void)
{
    if (s_brightness_last_change == 0) return;
    int64_t now = esp_timer_get_time();
    if (now - s_brightness_last_change > BRIGHTNESS_OVERLAY_HIDE_MS * 1000) {
        s_brightness_last_change = 0;
        return;
    }

    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d%%", s_brightness);
    if (len <= 0) return;

    if (!s_lcd) return;

    /* overlay origin in PPA output (landscape) coords */
    int ox = (s_ui_mode == UI_VIRT) ? 700 : 620;
    int oy = 10;

    /* black background (len*8+6 wide, 18 tall) */
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < len * 8 + 6; i++) {
            int px = ox + i, py = oy + j;
            if (px >= 0 && px < HW_LCD_W && py >= 0 && py < HW_LCD_H)
                s_lcd[py * HW_LCD_W + px] = 0x0000;
        }
    /* white text */
    for (int j = 0; j < 16; j++) {
        for (int k = 0; k < len; k++) {
            char ch = buf[k];
            if (ch < 0x20 || ch > 0x7e) continue;
            const uint8_t *g = vpad_font8[ch - 0x20];
            for (int row = 0; row < 16; row++)
                for (int col = 0; col < 8; col++)
                    if (g[row] & (0x80 >> col)) {
                        int px = ox + k * 8 + col + 3;
                        int py = oy + row + 1;
                        if (px >= 0 && px < HW_LCD_W && py >= 0 && py < HW_LCD_H)
                            s_lcd[py * HW_LCD_W + px] = 0xFFFF;
                    }
        }
    }
}

/* Apply brightness change and refresh overlay timer. */
static void apply_brightness_delta(int delta)
{
    int new_b = s_brightness + delta;
    if (new_b < 5)  new_b = 5;
    if (new_b > 100) new_b = 100;
    if (new_b == s_brightness) return;
    s_brightness = new_b;
    bsp_display_brightness_set(s_brightness);
    s_brightness_last_change = esp_timer_get_time();
    ESP_LOGI(TAG, "Brightness: %d%%", s_brightness);
}

/* ─── drawing into a persistent portrait panel buffer ────────────────────── */
static uint16_t *s_panel = NULL;

static inline void upx(int lx, int ly, uint16_t c)
{
    if (!s_panel || lx < 0 || lx >= LAND_W || ly < 0 || ly >= LAND_H) return;
    s_panel[((PORT_H - 1) - lx) * PORT_W + ly] = c;
}
static void ufill(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            upx(x + i, y + j, c);
}
static void uchar(int x, int y, char ch, int s, uint16_t c)
{
    if (ch < 0x20 || ch > 0x7e) return;
    const uint8_t *g = vpad_font8[ch - 0x20];
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 8; col++)
            if (g[row] & (0x80 >> col))
                ufill(x + col * s, y + row * s, s, s, c);
}
static void ucjk(int x, int y, uint32_t cp, uint16_t c)
{
    const uint8_t *g = nc_cjk_glyph(cp);
    if (!g) return;
    const int bpr = (NC_CJK_W + 7) / 8;
    for (int row = 0; row < NC_CJK_H; row++)
        for (int col = 0; col < NC_CJK_W; col++)
            if (g[row * bpr + (col >> 3)] & (0x80 >> (col & 7)))
                upx(x + col, y + row, c);
}
static void uarrow(int cx, int cy, int dir, uint16_t c)
{
    int r = 16;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            bool in = false;
            switch (dir) {
                case 0: in = (dy <= 0) && (abs(dx) <= (r + dy)); break;
                case 1: in = (dy >= 0) && (abs(dx) <= (r - dy)); break;
                case 2: in = (dx <= 0) && (abs(dy) <= (r + dx)); break;
                case 3: in = (dx >= 0) && (abs(dy) <= (r - dx)); break;
            }
            if (in) upx(cx + dx, cy + dy, c);
        }
}
static void udraw_label(int kx, int ky, int kw, int kh, const nkey_t *k)
{
    int cx = kx + kw / 2, cy = ky + kh / 2;
    if (k->code == 0x1a) { uarrow(cx, cy, 0, KC_TXT); return; }
    if (k->code == 0x1b) { uarrow(cx, cy, 1, KC_TXT); return; }
    if (k->code == 0x3f) { uarrow(cx, cy, 2, KC_TXT); return; }
    if (k->code == 0x1f) { uarrow(cx, cy, 3, KC_TXT); return; }

    const char *s = k->label;
    if (!s || !s[0]) return;

    if ((unsigned char)s[0] >= 0x80) {
        int n = 0; const char *p = s;
        while (*p) { if (((unsigned char)*p & 0xc0) != 0x80) n++; p++; }
        int w = n * NC_CJK_W;
        int x = cx - w / 2, y = cy - NC_CJK_H / 2;
        p = s;
        while (*p) {
            uint32_t cp = ((unsigned char)p[0] & 0x0f) << 12 |
                          ((unsigned char)p[1] & 0x3f) << 6 |
                          ((unsigned char)p[2] & 0x3f);
            ucjk(x, y, cp, KC_TXT);
            x += NC_CJK_W; p += 3;
        }
    } else {
        int len = (int)strlen(s);
        int sc = (len * 8 * 3 <= kw - 8) ? 3 : (len * 8 * 2 <= kw - 8 ? 2 : 1);
        int w = len * 8 * sc;
        int x = cx - w / 2, y = cy - 8 * sc;
        for (const char *q = s; *q; q++) { uchar(x, y, *q, sc, KC_TXT); x += 8 * sc; }
    }
}

static void panel_push(void)
{
    esp_lcd_panel_handle_t panel = bsp_get_panel_handle();
    if (!panel || !s_panel) return;
    size_t sz = (size_t)PORT_W * PORT_H * 2;
    esp_cache_msync(s_panel, sz, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, PORT_W, PORT_H, s_panel);
    vTaskDelay(pdMS_TO_TICKS(25));
}
static void panel_black(void)
{
    if (s_panel) memset(s_panel, 0, (size_t)PORT_W * PORT_H * 2);
}

static void draw_loading(void)
{
    panel_black();
    static const uint32_t cps[] = { 0x8F7D, 0x5165, 0x4E2D };  /* 载 入 中 */
    int n = 3, gap = 8;
    int w = n * NC_CJK_W + (n - 1) * gap;
    int x = LAND_W / 2 - w / 2, y = LAND_H / 2 - NC_CJK_H / 2;
    for (int i = 0; i < n; i++) { ucjk(x, y, cps[i], KC_TXT); x += NC_CJK_W + gap; }
    panel_push();
}

/* ─── chrome (static keys) per mode ─────────────────────────────────────── */
static void draw_chrome_hw(void)
{
    panel_black();
    for (int c = 0; c < HW_FN_COLS; c++) {
        const nkey_t *k = &FN[c];
        int x = c * HW_FN_KW;
        ufill(x + KEY_PAD, HW_FN_Y + KEY_PAD, HW_FN_KW - 2 * KEY_PAD,
              HW_FN_H - 2 * KEY_PAD, key_color(k->code));
        udraw_label(x, HW_FN_Y, HW_FN_KW, HW_FN_H, k);
    }
    panel_push();
}

static void draw_chrome_virt(void)
{
    panel_black();
    for (int r = 0; r < 8; r++) {
        int y = (r < 4 ? r * V_RH : V_KBD_Y + (r - 4) * V_RH);
        for (int c = 0; c < V_COLS; c++) {
            const nkey_t *k = &KB[r][c];
            if (k->code == KEY_NONE) continue;
            int x = c * V_KW;
            ufill(x + KEY_PAD, y + KEY_PAD, V_KW - 2 * KEY_PAD, V_RH - 2 * KEY_PAD,
                  key_color(k->code));
            udraw_label(x, y, V_KW, V_RH, k);
        }
    }
    ufill(2 * V_KW, 0, 6 * V_KW, 4 * V_RH, KC_LCDBG);
    panel_push();
}

static void draw_chrome(void)
{
    if (s_ui_mode == UI_VIRT) draw_chrome_virt();
    else                      draw_chrome_hw();
    ESP_LOGI(TAG, "chrome drawn (%s)", s_ui_mode == UI_VIRT ? "virtual" : "hardware");
}

/* ─── per-frame LCD ─────────────────────────────────────────────────────────
 * Convert the 160x80 wqx LCD (1bpp B/W or 2bpp 4-level grey) to rgb565, then
 * PPA hardware scale+rotate (90°) into s_lcd and draw that sub-rect. */
static void nc2k_present_lcd(void)
{
    int fmt = nc2k_copy_lcd(lcd_buf);
    if (fmt == 0) return;

    /* Diagnostic: every ~2s, log fmt + a checksum of the LCD bytes so we can
     * tell whether the screen content is actually changing (NC1020 running) or
     * frozen on one image (stuck). Also count non-blank bytes. */
    {
        static int64_t last = 0; static uint32_t prevsum = 0;
        int64_t now = esp_timer_get_time();
        if (now - last > 2000000) {
            last = now;
            uint32_t sum = 0; int nonblank = 0;
            int n = (fmt == 2) ? 3200 : 1600;
            for (int i = 0; i < n; i++) { sum += lcd_buf[i]; if (lcd_buf[i] != 0 && lcd_buf[i] != 0xff) nonblank++; }
            ESP_LOGW(TAG, "LCD fmt=%d sum=%u(prev=%u %s) nonblank=%d/%d",
                     fmt, sum, prevsum, sum == prevsum ? "FROZEN" : "changing", nonblank, n);
            prevsum = sum;
        }
    }

    if (fmt == 2) {                          /* 2bpp grey: 4 px/byte */
        for (int i = 0; i < NC2K_LCD_W * NC2K_LCD_H / 4; i++) {
            uint8_t b = lcd_buf[i];
            for (int j = 0; j < 4; j++)
                rgb565_buf[i * 4 + j] = GREY4[(b >> (6 - j * 2)) & 0x03];
        }
    } else {                                 /* 1bpp B/W */
        for (int y = 0; y < NC2K_LCD_H; y++)
            for (int x = 0; x < NC2K_LCD_W; x++) {
                int idx = y * NC2K_LCD_W + x;
                int bit = (lcd_buf[idx >> 3] >> (7 - (idx & 7))) & 1;
                rgb565_buf[idx] = bit ? 0x0000 : 0xFFFF;
            }
    }

    esp_lcd_panel_handle_t panel = bsp_get_panel_handle();
    if (!panel) return;

    float sx, sy;
    if (s_ui_mode == UI_VIRT) { sx = V_LCD_SX;     sy = V_LCD_SY;     }
    else                      { sx = HW_LCD_SCALE; sy = HW_LCD_SCALE; }
    uint32_t ow = 0, oh = 0;
    if (ppa_rotate_scale_rgb565_to(rgb565_buf, NC2K_LCD_W, NC2K_LCD_H, UI_ROT,
                                   sx, sy, s_lcd, (size_t)HW_LCD_W * HW_LCD_H * 2,
                                   &ow, &oh, false) != ESP_OK)
        return;

    int px0, py0;
    if (s_ui_mode == UI_VIRT) { px0 = V_LCD_LY; py0 = PORT_H - (V_LCD_LX + V_LCD_W); }
    else                      { px0 = 0;        py0 = 0; }

    /* brightness overlay — drawn into nc2000 LCD buffer BEFORE PPA transform.
     * Placed in PPA padding area: HW mode x>640 (PPA crops to 640); VIRT mode
     * y>320 (PPA crops output to 320-high in landscape).  Both are invisible to
     * the nc2000 LCD content and survive PPA into the right panel location. */
    draw_brightness_overlay();

    esp_lcd_panel_draw_bitmap(panel, px0, py0, px0 + ow, py0 + oh, s_lcd);
}

/* ─── input ─────────────────────────────────────────────────────────────── */
static void kbmode_save(void)
{
    char path[300];
    snprintf(path, sizeof(path), "%s.kbmode", s_base);
    FILE *f = fopen(path, "wb");
    if (f) { fputc(s_ui_mode, f); fclose(f); }
}

static void nc2k_poll_input(void)
{
    bool want[KEY_SIZE] = {0};
    static bool exit_prev = false, save_prev = false, tgl_prev = false;
    bool exit_now = false, save_now = false, tgl_now = false;

    /* hardware keyboard: edge events -> persistent hw_down[] */
    kbd_hw_event_t evs[16];
    int n = kbd_hw_poll(evs, 16);
    for (int i = 0; i < n; i++) {
        if (evs[i].row >= 5 || evs[i].col >= 14) continue;
        uint8_t code = HWMAP[evs[i].row][evs[i].col];
        if (code != KEY_NONE && code < KEY_SIZE)
            hw_down[code] = evs[i].pressed;
    }
    for (int i = 0; i < KEY_SIZE; i++)
        if (hw_down[i]) want[i] = true;

    /* touch — hit-test the active layout + brightness swipe */
    uint16_t xs[8], ys[8];
    int t = bsp_touch_read_points(xs, ys, 8);
    for (int i = 0; i < t; i++) {
        int lx = (PORT_H - 1) - (int)ys[i];
        int ly = (int)xs[i];
        if (lx < 0 || lx >= LAND_W || ly < 0 || ly >= LAND_H) continue;

        /* ── brightness swipe: vertical motion inside the nc2000 LCD area ── */
        bool in_screen = false;
        if (s_ui_mode == UI_VIRT) {
            if (lx >= V_LCD_LX && lx < V_LCD_LX + V_LCD_W && ly < V_KBD_Y)
                in_screen = true;
        } else {
            if (ly < HW_FN_Y)
                in_screen = true;
        }
        if (in_screen) {
            if (s_swipe_origin_y < 0) {
                s_swipe_origin_y = ly;        /* first point of this stroke */
            } else {
                int dy = s_swipe_origin_y - ly;  /* +up / -down in landscape */
                if (dy > BRIGHTNESS_SWIPE_THRESHOLD) {
                    apply_brightness_delta(+BRIGHTNESS_STEP);
                    s_swipe_origin_y = ly;
                } else if (dy < -BRIGHTNESS_SWIPE_THRESHOLD) {
                    apply_brightness_delta(-BRIGHTNESS_STEP);
                    s_swipe_origin_y = ly;
                }
            }
            continue;   /* don't hit-test keys while in the screen area */
        }
        /* touch ended outside the screen area → reset swipe */
        if (s_swipe_origin_y >= 0 && t == 0) { /* cleared below after loop */}

        uint8_t code = KEY_NONE;
        if (s_ui_mode == UI_VIRT) {
            int c = lx / V_KW, row;
            if (c < 0 || c >= V_COLS) continue;
            if (ly < V_KBD_Y) row = ly / V_RH;
            else if (ly < V_KBD_Y + 4 * V_RH) row = 4 + (ly - V_KBD_Y) / V_RH;
            else continue;
            code = KB[row][c].code;
        } else {
            if (ly < HW_FN_Y || ly >= HW_FN_Y + HW_FN_H) continue;
            int c = lx / HW_FN_KW;
            if (c < 0 || c >= HW_FN_COLS) continue;
            code = FN[c].code;
        }
        switch (code) {
            case KEY_NONE:    break;
            case KEY_EXIT:    exit_now = true; break;
            case KEY_SAVE:    save_now = true; break;
            case KEY_KTOGGLE: tgl_now  = true; break;
            case KEY_FFON:    s_fast = true;  break;
            case KEY_FFOFF:   s_fast = false; break;
            default:          if (code < KEY_SIZE) want[code] = true; break;
        }
    }
    if (t == 0) s_swipe_origin_y = -1;  /* no touch → reset swipe */
    if (exit_now && !exit_prev) s_kbd_exit = true;
    if (save_now && !save_prev) s_save_req = true;
    if (tgl_now  && !tgl_prev)  { s_ui_mode ^= 1; s_mode_dirty = true; kbmode_save(); }
    exit_prev = exit_now; save_prev = save_now; tgl_prev = tgl_now;

    /* USB gamepad fallback */
    odroid_gamepad_state gp;
    odroid_input_gamepad_read(&gp);
    static const struct { int btn; uint8_t code; } gmap[] = {
        { ODROID_INPUT_UP,    0x1a }, { ODROID_INPUT_DOWN,  0x1b },
        { ODROID_INPUT_LEFT,  0x3f }, { ODROID_INPUT_RIGHT, 0x1f },
        { ODROID_INPUT_A,     0x1d }, { ODROID_INPUT_B,     0x3b },
        { ODROID_INPUT_START, 0x38 }, { ODROID_INPUT_SELECT,0x0f },
    };
    for (size_t m = 0; m < sizeof(gmap) / sizeof(gmap[0]); m++)
        if (gp.values[gmap[m].btn]) want[gmap[m].code] = true;

    for (int i = 0; i < KEY_SIZE; i++) {
        if (want[i] && !kb_state[i]) { ESP_LOGW(TAG, "KEY %02x DOWN", i); nc2k_set_key((uint8_t)i, true); }
        else if (!want[i] && kb_state[i]) { ESP_LOGW(TAG, "KEY %02x UP", i); nc2k_set_key((uint8_t)i, false); }
        kb_state[i] = want[i];
    }
}

/* Draw a single-line ASCII string at landscape (x,y), scale sc. */
static void ustr(int x, int y, const char *s, int sc, uint16_t c)
{
    for (const char *q = s; *q; q++) { uchar(x, y, *q, sc, c); x += 8 * sc; }
}

void nc2000_run(const char *base, nc2k_mode_t mode)
{
    if (!base || !base[0]) { ESP_LOGE(TAG, "no base path"); return; }
    snprintf(s_base, sizeof(s_base), "%s", base);
    s_mode = mode;
    ESP_LOGI(TAG, "Starting wqx emulator, base=%s mode=%d", s_base, (int)mode);

    rgb565_buf = (uint16_t *)heap_caps_malloc(
        NC2K_LCD_W * NC2K_LCD_H * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lcd = (uint16_t *)heap_caps_aligned_calloc(
        64, 1, (size_t)HW_LCD_W * HW_LCD_H * 2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    /* Render straight into the DSI panel's own framebuffer. esp_lcd_panel_draw_bitmap
     * then takes the "buffer is the framebuffer" fast path (cache write-back only,
     * no DMA/CPU copy) — the copy path faults on this panel. */
    {
        esp_lcd_panel_handle_t p0 = bsp_get_panel_handle();
        void *fb0 = NULL;
        if (p0) esp_lcd_dpi_panel_get_frame_buffer(p0, 1, &fb0);
        s_panel = (uint16_t *)fb0;
    }
    if (!rgb565_buf || !s_lcd || !s_panel) {
        ESP_LOGE(TAG, "Failed to allocate display buffers (panel fb=%p)", s_panel);
        return;
    }

    ili9341_init();
    odroid_audio_init(16000);
    odroid_input_xy_menu_disable = true;

    draw_loading();
    vTaskDelay(pdMS_TO_TICKS(150));
    bsp_display_brightness_set(0);

    bool hw_kbd = kbd_hw_init();
    if (hw_kbd)
        ESP_LOGI(TAG, "Hardware keyboard attached");
    else
        ESP_LOGW(TAG, "No hardware keyboard — use the on-screen keys / gamepad");

    /* Default UI mode: with the detachable keyboard attached, use HW mode (big
     * 8x LCD + a function strip). Without it, default to the full on-screen
     * keyboard (VIRT) so the device is usable by touch alone. A saved .kbmode
     * (the user's last 软键/全屏 choice) overrides this. */
    s_ui_mode = hw_kbd ? UI_HW : UI_VIRT;
    {
        char path[300];
        snprintf(path, sizeof(path), "%s.kbmode", s_base);
        FILE *mf = fopen(path, "rb");
        if (mf) { int c = fgetc(mf); if (c == UI_HW || c == UI_VIRT) s_ui_mode = c; fclose(mf); }
    }

    ESP_LOGI(TAG, "Configuring wqx core...");
    nc2k_configure(mode, s_base);
    ESP_LOGI(TAG, "Loading wqx data (rom/nor/nand)...");
    if (!nc2k_load()) {
        ESP_LOGE(TAG, "Required data file(s) missing for %s — aborting", s_base);
        bsp_display_brightness_set(100);
        /* show a brief on-screen notice, then return so the app can re-browse */
        panel_black();
        ustr(40, LAND_H / 2 - 12, "MISSING ROM/NOR/NAND FILE", 3, 0xF800);
        panel_push();
        vTaskDelay(pdMS_TO_TICKS(2500));
        odroid_audio_terminate();
        odroid_input_xy_menu_disable = false;
        kbd_hw_deinit();
        if (rgb565_buf) { heap_caps_free(rgb565_buf); rgb565_buf = NULL; }
        if (s_lcd) { heap_caps_free(s_lcd); s_lcd = NULL; }
        s_panel = NULL;
        return;
    }
    ESP_LOGI(TAG, "wqx loaded, entering main loop");

    nc2k_quit_flag = false;
    s_kbd_exit = s_save_req = s_fast = false;
    memset(kb_state, 0, sizeof(kb_state));
    memset(hw_down,  0, sizeof(hw_down));

    draw_chrome();
    nc2k_present_lcd();
    bsp_display_brightness_set(100);

    int frame_no = 0;
    int64_t fps_timer = esp_timer_get_time();
    odroid_gamepad_state gp_prev;
    odroid_input_gamepad_read(&gp_prev);

    while (!nc2k_quit_flag) {
        int64_t tick = esp_timer_get_time();

        nc2k_run_slice(FRAME_INTERVAL_MS, s_fast);
        nc2k_poll_input();
        if (s_mode_dirty) { s_mode_dirty = false; draw_chrome(); }
        nc2k_present_lcd();

        if (s_save_req) {
            s_save_req = false;
            bsp_display_brightness_set(0);
            nc2k_save();
            bsp_display_brightness_set(100);
            ESP_LOGI(TAG, "state saved (保存)");
        }

        odroid_gamepad_state gp;
        odroid_input_gamepad_read(&gp);
        if (s_kbd_exit ||
            (gp.values[ODROID_INPUT_MENU] && !gp_prev.values[ODROID_INPUT_MENU])) {
            nc2k_quit_flag = true;
            break;
        }
        gp_prev = gp;

        if (s_fast) {
            vTaskDelay(1);
        } else {
            int64_t elapsed_ms = (esp_timer_get_time() - tick) / 1000;
            int64_t delay_ms = FRAME_INTERVAL_MS - elapsed_ms;
            if (delay_ms > 0) vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_ms));
        }

        frame_no++;
        int64_t now = esp_timer_get_time();
        if (now - fps_timer > 2000000) {
            ESP_LOGI(TAG, "FPS=%d", (int)(frame_no * 1000000LL / (now - fps_timer)));
            frame_no = 0; fps_timer = now;
        }
    }

    ESP_LOGI(TAG, "Saving wqx state...");
    bsp_display_brightness_set(0);
    nc2k_save();

    odroid_audio_terminate();
    odroid_input_xy_menu_disable = false;
    kbd_hw_deinit();

    if (rgb565_buf) { heap_caps_free(rgb565_buf); rgb565_buf = NULL; }
    if (s_lcd) { heap_caps_free(s_lcd); s_lcd = NULL; }
    s_panel = NULL;   /* owned by the DSI panel — do not free */

    ESP_LOGI(TAG, "wqx emulator shutdown complete");
}
