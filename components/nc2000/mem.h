#pragma once
#include "comm.h"
#include "ram.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t* memmap[8];
extern uint8_t* bbs_pages[0x10];

void init_mem();

uint8_t & Peek16(uint16_t addr);
void Poke16(uint16_t addr, uint8_t & a);

#ifdef __cplusplus
}
#endif

/* C++-linkage helpers defined in mem.cpp */
uint16_t PeekW(uint16_t addr);
uint8_t  Load(uint16_t addr);
void     Store(uint16_t addr, uint8_t value);
