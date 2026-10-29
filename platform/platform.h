/* SPDX-License-Identifier: GPL-3.0-only */
/* The whole surface between the tracker and a machine. Three backends
 * implement it: the FM-1 itself (firmware/src/firmware.c, on Felucca's
 * HAL), SDL2 (platform/sdl) and the browser (platform/web).
 *
 * Everything above this file is freestanding C: no libc, no malloc, no
 * files, no float. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#define PLAT_W 240
#define PLAT_H 240
/* Nominal. The FM-1's I2S clock really gives 44117.6 Hz (Felucca's
 * audio.c: 544 ticks of 24 MHz per frame), 0.7 cent sharp. */
#define PLAT_FS 44100

/* One RGB565 pixel as plat_blit wants it. The ST7789 takes the high
 * byte first and the blit is a DMA straight out of the caller's buffer,
 * so on the device the swap has to be done when the colour is made. */
#ifdef PLAT_FM1
#define PLAT_PX(c) ((uint16_t)((((c) & 0xFFu) << 8) | (((c) >> 8) & 0xFFu)))
#else
#define PLAT_PX(c) ((uint16_t)(c))
#endif
#define PLAT_RGB(r, g, b) \
    PLAT_PX((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3))

/* The 27 note keys, bit 0 = F3 .. bit 26 = G5 (hal/fm1_input.h). */
#define PLAT_KEYS 27

/* The 14 buttons by their printed label -- Felucca's panel.c order. */
enum {
    PB_FX, PB_SEL, PB_ENV, PB_LFO, PB_EDIT, PB_GLO, PB_HOME, PB_SAVE,
    PB_ARP, PB_SEQ, PB_PLAY, PB_REC, PB_OCTDN, PB_OCTUP, PLAT_BTNS
};

/* The 7 encoders by role. The eighth knob, MASTER, is a pot read by an
 * ADC: it is not wired to the codec, the volume is ours to apply. */
enum {
    PE_SELECT, PE_ALGO, PE_PRESET, PE_K1, PE_K2, PE_K3, PE_K4, PLAT_ENCS
};

typedef struct {
    uint32_t keys;          /* held note keys */
    uint32_t btns;          /* held buttons, bit = PB_* */
    int8_t enc[PLAT_ENCS];  /* detents since the last frame, + = clockwise */
    uint16_t master;        /* 0..1023 */
    uint32_t ms;            /* since boot */
} PlatInput;

/* w*h pixels at x,y. The buffer is the caller's again on return. */
void plat_blit(int x, int y, int w, int h, const uint16_t *px);
void plat_led_key(int key, int on); /* 0..PLAT_KEYS-1 */
void plat_led_btn(int btn, int on); /* PB_* */
/* The faint glow (~1/30 of lit) under a button's LED: what can be pressed here
 * (app/ui.h ui_led_guide). A lit LED shows lit over it. */
void plat_led_glow(int btn, int on);
/* Any LED at any of the panel's levels (hal/fm1_input.h): `led` a PB_*
 * button, or PLAT_BTNS + a key (0..PLAT_KEYS-1). The link's LED command
 * (app/remote.c): a computer showing a button. */
enum { PLAT_LED_OFF, PLAT_LED_GLOW, PLAT_LED_MID, PLAT_LED_LIT, PLAT_LED_BREATH,
       PLAT_LED_50, PLAT_LED_62, PLAT_LED_75, PLAT_LED_87, PLAT_LED_LEVELS }; /* 50..87: % of lit */
void plat_led_level(int led, int level);

/* Where songs are kept: 73 sectors of 4 KB, the 292 KB of the FM-1's
 * flash that Felucca uses for its own store (0x97000..0xE0000). Offsets
 * are from the start of that area; a backend refuses anything outside
 * it. NOR flash: programming only clears bits, a sector is erased to
 * 0xFF whole, and on the device an erase stops the audio for its 50 ms.
 * All three return 0 when done. Main loop only. */
#define PLAT_FLASH_SECTOR  4096u
#define PLAT_FLASH_SECTORS 73u
int plat_flash_read(uint32_t off, void *dst, uint32_t n);
int plat_flash_erase(uint32_t sector);
int plat_flash_prog(uint32_t off, const void *src, uint32_t n);

/* SysEx from and to the computer, for app/link.c: the bytes between F0
 * and F7, all under 0x80. One message at a time: plat_sysex_get returns
 * 0 when none waits, and the one it returned is the caller's until
 * plat_sysex_done. Only messages that start with 7D 4D 44 come through.
 * Main loop only. plat_sysex_send returns 0 when sent. */
int plat_sysex_get(const uint8_t **p, uint32_t *n);
void plat_sysex_done(void);
int plat_sysex_send(const uint8_t *p, uint32_t n);

/* How much of its time the audio interrupt takes, in percent, averaged
 * and at worst since the last call; -1 where nothing measures it (the
 * simulators). The one number nobody knows before the machine is on the
 * desk. */
int plat_audio_load(int *worst);

/* What a backend calls. app_frame runs in the main loop, about 60 times
 * a second. app_audio runs in the audio interrupt (a thread on SDL, the
 * worklet in the browser) and may preempt app_frame at any point:
 * interleaved stereo, 16 bits, no allocation, no blit. */
void app_init(void);
void app_frame(const PlatInput *in);
void app_audio(int16_t *out, int frames);
/* A .trk handed over by the backend: a file named on the command line,
 * dropped on the page. From the main loop, after app_init. On SDL the
 * audio device is locked around it. Returns false when it is not a
 * song this machine reads. */
int app_load(const char *trk, uint32_t len, const char *name);

#endif
