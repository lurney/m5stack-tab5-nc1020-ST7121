#pragma once
#include "SDL_keycode.h"

typedef struct SDL_Keysym {
    SDL_Keycode sym;
    uint16_t mod;
    uint16_t unused;
} SDL_Keysym;
