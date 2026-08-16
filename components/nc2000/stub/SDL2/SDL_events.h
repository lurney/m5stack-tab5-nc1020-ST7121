#pragma once
#include <stdint.h>
#include "SDL_keycode.h"
#include "SDL.h"

typedef struct SDL_KeyboardEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint8_t state;
    uint8_t repeat;
    uint8_t padding2;
    uint8_t padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    char text[32];
} SDL_TextInputEvent;

typedef struct SDL_Event {
    uint32_t type;
    union {
        SDL_KeyboardEvent key;
        SDL_TextInputEvent text;
    };
} SDL_Event;

/* Event types */
#define SDL_FIRSTEVENT    0
#define SDL_QUIT          0x100
#define SDL_KEYDOWN       0x300
#define SDL_KEYUP         0x301
#define SDL_TEXTINPUT     0x303
#define SDL_TEXTEDITING   0x304
#define SDL_LASTEVENT     0xFFFF
