#pragma once
/* Minimal SDL2 shim for the ESP32 / M5Stack Tab5 port */
#include <stdint.h>
#include <stddef.h>
#include "SDL_keycode.h"
#include "SDL_keyboard.h"
#include "SDL_events.h"
#include "SDL_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window / Renderer / Texture stubs */
typedef struct SDL_Window    SDL_Window;
typedef struct SDL_Renderer  SDL_Renderer;
typedef struct SDL_Texture   SDL_Texture;
typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

/* Init flags */
#define SDL_INIT_VIDEO        0x00000020
#define SDL_INIT_AUDIO        0x00000010
#define SDL_INIT_TIMER        0x00000001
#define SDL_INIT_EVERYTHING   0xFFFF

/* Init / Quit */
int  SDL_Init(uint32_t flags);
void SDL_Quit(void);

/* Hint */
#define SDL_HINT_RENDER_DRIVER "SDL_RENDER_DRIVER"
void SDL_SetHint(const char *name, const char *value);

/* Window */
SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags);
void SDL_DestroyWindow(SDL_Window *window);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);

/* Renderer */
#define SDL_RENDERER_SOFTWARE 0x00000001
SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, uint32_t flags);
void SDL_RenderPresent(SDL_Renderer *renderer);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect);

/* Texture */
#define SDL_PIXELFORMAT_RGBA 0
#define SDL_PIXELFORMAT_RGBA8888 0
#define SDL_TEXTUREACCESS_STREAMING 1
SDL_Texture* SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);

/* Events (covered by SDL_events.h) */
int SDL_PollEvent(SDL_Event *event);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);

/* Delay / Ticks */
void SDL_Delay(uint32_t ms);
uint32_t SDL_GetTicks(void);
uint64_t SDL_GetTicks64(void);

/* Thread priority (covered by SDL_events.h) */
#define SDL_THREAD_PRIORITY_LOW            0
#define SDL_THREAD_PRIORITY_NORMAL         1
#define SDL_THREAD_PRIORITY_HIGH           2
#define SDL_THREAD_PRIORITY_TIME_CRITICAL  3
int SDL_SetThreadPriority(int priority);

/* Audio types and stubs (implemented in SDL_audio.h as static inline) */
typedef int16_t  Sint16;
typedef uint8_t  Uint8;
typedef uint32_t Uint32;
typedef struct { int x, y; } SDL_Point;
void SDL_memset(void *dst, int c, size_t n);
#define AUDIO_S16LSB 0x8010

#ifdef __cplusplus
}
#endif
