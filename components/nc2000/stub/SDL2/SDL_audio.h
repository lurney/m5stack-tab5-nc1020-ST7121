#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint32_t SDL_AudioDeviceID;
typedef void (*SDL_AudioCallback)(void *userdata, uint8_t *stream, int len);

typedef struct SDL_AudioSpec {
    int     freq;
    uint16_t format;
    uint8_t  channels;
    uint8_t  silence;
    uint16_t samples;
    uint16_t padding;
    uint32_t size;
    SDL_AudioCallback callback;
    void    *userdata;
} SDL_AudioSpec;

static inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char *name, int capture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes) { (void)name;(void)capture;(void)desired;(void)obtained;(void)allowed_changes; return 0; }
static inline void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { (void)dev;(void)pause_on; }
static inline void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
static inline void SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
static inline void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
static inline int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, uint32_t len) { (void)dev;(void)data;(void)len; return 0; }
static inline const char *SDL_GetError(void) { return ""; }
