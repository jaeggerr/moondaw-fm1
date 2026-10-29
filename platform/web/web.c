/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 in the browser. The principle is Felucca's web/emu/
 * felucca_web.c (Leo Kuroshita, GPL-3.0; itself after X0X's x0x_web.c,
 * charlesvestal/fm1-x0x): the whole thing runs inside the AudioWorklet
 * and the audio it is asked for IS the device clock -- every 128 frames
 * is one I2S half, and the main loop gets a frame each 15 ms of that
 * time, as firmware.c gives it. Nothing here is copied from that file:
 * Felucca simulates its HAL under its own firmware, this implements
 * platform.h directly.
 *
 * No libc: built freestanding (zig cc) or with emcc --no-entry. */
#include "../platform.h"
#include "../../app/splash.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WEB_EXPORT __attribute__((visibility("default")))
/* The compiler turns loops into these whether or not there is a libc. */
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
#endif

#define HALF_FRAMES 128
#define FRAME_MS 15u

static uint16_t fb[PLAT_W * PLAT_H];
static uint32_t draws; /* bumped by every blit: the page redraws on change */
static uint32_t led_keys, led_btns;
static PlatInput in;
static int32_t enc_acc[PLAT_ENCS];
static int16_t pcm[HALF_FRAMES * 2];
static uint32_t clock_frames, last_frame_ms;

void plat_blit(int x, int y, int w, int h, const uint16_t *px)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (x + i >= 0 && x + i < PLAT_W && y + j >= 0 && y + j < PLAT_H)
                fb[(y + j) * PLAT_W + x + i] = px[j * w + i];
    draws++;
}

/* The flash, in the module's memory. The page keeps a copy in the
 * browser and hands it back before web_boot(); web_flash_writes() tells
 * it when to take a new one. */
static uint8_t nor[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];
static uint32_t nor_writes;

int plat_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof(nor) || n > sizeof(nor) - off)
        return -1;
    for (uint32_t i = 0; i < n; i++)
        ((uint8_t *)dst)[i] = nor[off + i];
    return 0;
}

const uint8_t *plat_flash_map(void) { return nor; }

int plat_flash_erase(uint32_t sector)
{
    if (sector >= PLAT_FLASH_SECTORS)
        return -1;
    for (uint32_t i = 0; i < PLAT_FLASH_SECTOR; i++)
        nor[sector * PLAT_FLASH_SECTOR + i] = 0xFF;
    nor_writes++;
    return 0;
}

int plat_flash_prog(uint32_t off, const void *src, uint32_t n)
{
    if (off > sizeof(nor) || n > sizeof(nor) - off)
        return -1;
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    nor_writes++;
    return 0;
}

WEB_EXPORT uint8_t *web_flash(void) { return nor; }
WEB_EXPORT uint32_t web_flash_size(void) { return sizeof(nor); }
WEB_EXPORT uint32_t web_flash_writes(void) { return nor_writes; }
/* Before web_boot(), and before the page writes its copy in: erased. */
WEB_EXPORT void web_flash_erase(void)
{
    for (uint32_t i = 0; i < sizeof(nor); i++)
        nor[i] = 0xFF;
}

/* No cable in a browser tab: songs are dropped on the page instead. */
int plat_sysex_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
void plat_sysex_done(void) {}
int plat_sysex_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }

int plat_audio_load(int *worst)
{
    *worst = -1;
    return -1;
}

void plat_led_key(int key, int on)
{
    led_keys = on ? led_keys | 1u << key : led_keys & ~(1u << key);
}

void plat_led_btn(int btn, int on)
{
    led_btns = on ? led_btns | 1u << btn : led_btns & ~(1u << btn);
}

void plat_led_glow(int btn, int on) { (void)btn, (void)on; } /* not drawn here */
void plat_led_level(int led, int level) { (void)led, (void)level; }

static void run_frame(void)
{
    for (int e = 0; e < PLAT_ENCS; e++) {
        int32_t s = enc_acc[e];
        in.enc[e] = (int8_t)(s > 127 ? 127 : s < -127 ? -127 : s);
        enc_acc[e] = 0;
    }
    app_frame(&in);
}

WEB_EXPORT void web_boot(void)
{
    in.master = 700;
    splash_show(); /* a frame long here: the FM-1 is the one that waits on its panel */
    app_init();
    run_frame();
}

/* One half buffer, and whatever main-loop frames fell due while it
 * played. Returns HALF_FRAMES interleaved stereo frames. */
WEB_EXPORT const int16_t *web_half(void)
{
    app_audio(pcm, HALF_FRAMES);
    clock_frames += HALF_FRAMES;
    in.ms = (uint32_t)((uint64_t)clock_frames * 1000u / PLAT_FS);
    if (in.ms - last_frame_ms >= FRAME_MS) {
        last_frame_ms = in.ms;
        run_frame();
    }
    return pcm;
}

/* A song from the page: it writes the text into web_song_buf(), a short
 * name into web_song_name(), then calls web_song_load(length). */
static char song_buf[1 << 18], song_name[20];
WEB_EXPORT char *web_song_buf(void) { return song_buf; }
WEB_EXPORT uint32_t web_song_cap(void) { return sizeof(song_buf); }
WEB_EXPORT char *web_song_name(void) { return song_name; }
WEB_EXPORT int web_song_load(uint32_t len)
{
    song_name[sizeof(song_name) - 1] = '\0';
    return len <= sizeof(song_buf) ? app_load(song_buf, len, song_name) : 0;
}

WEB_EXPORT void web_keys(uint32_t held) { in.keys = held; }
WEB_EXPORT void web_btns(uint32_t held) { in.btns = held; }
WEB_EXPORT void web_enc(uint32_t role, int32_t steps)
{
    if (role < PLAT_ENCS)
        enc_acc[role] += steps;
}
WEB_EXPORT void web_master(uint32_t v) { in.master = (uint16_t)(v > 1023u ? 1023u : v); }

WEB_EXPORT const uint16_t *web_fb(void) { return fb; }
WEB_EXPORT uint32_t web_draws(void) { return draws; }
WEB_EXPORT uint32_t web_led_keys(void) { return led_keys; }
WEB_EXPORT uint32_t web_led_btns(void) { return led_btns; }
WEB_EXPORT uint32_t web_sample_rate(void) { return PLAT_FS; }
