/*
 * Tab5 port stubs — replaces the NC2000 desktop layer (SDL window/timer, DSP
 * audio chip, debug console, UART, disassembler, NekoDriver, cmd) with no-ops
 * so the platform-independent 6502 core compiles + links on ESP-IDF.
 * Real audio/UART can be wired to the Tab5 BSP later; for now they're silent.
 */
#include <stdint.h>
#include <string>
#include "esp_timer.h"
#include "dsp/dsp.h"

/* ---- SDL shim ---- */
extern "C" uint32_t SDL_GetTicks(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
extern "C" uint64_t SDL_GetTicks64(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
extern "C" void SDL_SetWindowTitle(struct SDL_Window *w, const char *t) { (void)w; (void)t; }

#ifndef HANDYPSP
/* ---- sound / DSP backend (silent — no audio yet) ---- */
void post_cpu_run_sound_handling() {}
void reset_dsp() {}
void dsp_move(int len) { (void)len; }
void write_data_to_dsp(uint8_t a, uint8_t b) { (void)a; (void)b; }
void beeper_on_io_write(int v) { (void)v; }
bool sound_busy(void) { return false; }

/* ---- IV / UART: NOT stubbed — the real iv_uart.cpp is compiled. ----
 * History of why: the IV queue is the RTC interrupt system. A stubbed
 * peek_iv()==0 (=IV_2HZ) fired a spurious wake every cycle (sleep/wake loop,
 * never boots); a stubbed peek_iv()==IV_NONE with put_iv() a no-op swallowed
 * the 2Hz RTC tick, so the firmware slept (clk off) and NEVER woke — black
 * screen after 保存/清除flash, keys only flashing a frame via warm-reset.
 * The firmware NEEDS the real put_iv/peek_iv/RCR0/RCR1 wiring to sleep+wake. */

/* ---- console / cmd / debug (no interactive console on device) ---- */
bool console_on = false;
bool dummy_io_for_read(uint16_t addr, uint8_t &value) { (void)addr; (void)value; return false; }
bool dummy_io_for_write(uint16_t addr, uint8_t value) { (void)addr; (void)value; return false; }
bool is_nc2600_rom() { return false; }
void set_warm_reset_flag() {}
void handle_cmd(std::string str) { (void)str; }
std::string get_message() { return std::string(); }
/* MUST return NULL (not ""): cpu_run3 does `if(peek_message()) split_s(...)[0]`,
 * and split_s("") yields an empty vector — indexing [0] would deref NULL. */
char* peek_message() { return nullptr; }

/* ---- disassembler (debug only) ---- */
std::string disassemble2(uint16_t a) { (void)a; return std::string(); }
std::string disassemble_next(unsigned char* c, uint16_t a) { (void)c; (void)a; return std::string(); }
#endif // HANDYPSP
