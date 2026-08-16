#pragma once
#include <stdint.h>

class Dsp {
public:
    int dspMode = 0;
    // Audio callback: set by sound.cpp init_audio()
    void (*callback)(int) = nullptr;
    void reset() { dspMode = 0; }
    int  write(int value = 0, int data_low = 0) { (void)value; (void)data_low; return 0; }
};

/* DSP log level stub */
inline void set_dsp_log_level(int level) { (void)level; }
