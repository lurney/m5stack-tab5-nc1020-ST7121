#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int i2s_num;
    int mclk_io;
    int bclk_io;
    int ws_io;
    int dout_io;
    int din_io;
    int pa_ctrl_io;
    void *i2c_handle;
    int sample_rate;
    int volume;
} audio_config_t;

#define AUDIO_CONFIG_DEFAULT() ((audio_config_t){0})

static inline int audio_init(const audio_config_t *cfg) { (void)cfg; return 0; }
static inline int audio_set_sample_rate(int rate) { (void)rate; return 0; }
static inline void audio_stop(void) {}
static inline int audio_set_volume(int pct) { (void)pct; return 0; }
static inline int audio_play_pcm(const void *data, size_t len, int sample_rate) { (void)data;(void)len;(void)sample_rate; return 0; }
static inline int audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, int volume) { (void)freq_hz;(void)duration_ms;(void)volume; return 0; }

#ifdef __cplusplus
}
#endif
