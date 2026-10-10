/* SPDX-License-Identifier: GPL-3.0-only */
/* What the screens share: the one song in memory, where it lives in
 * flash, and whether it has been touched since it was last written. */
#ifndef APP_H
#define APP_H

#include "../core/song.h"
#include "../core/store.h"
#include "../platform/platform.h"

#define APP_SONG_SLOTS (STORE_SLOTS - 1) /* the last slot remembers which song was open */

extern Song app_song;
extern char app_name[STORE_NAME + 1];
extern int app_slot;   /* its place in flash, -1 while it has none */
extern bool app_dirty; /* edited since the last save or load */

void app_mark_dirty(void);
int app_save(void);          /* 0 when written; stops the song first */
int app_open_slot(int slot); /* 0 when loaded; the song in memory is untouched otherwise */
void app_new(void);          /* an empty song, named SONG nn, not yet in flash */
/* Lines and tokens the song's file had that this machine does not carry. */
int app_not_carried(void);

/* Shared by the screens that draw the twelve buttons at their place. */
#define C_BG     GFX_RGB(0x10, 0x14, 0x1C)
#define C_TEXT   GFX_RGB(0xC8, 0xD0, 0xC8)
#define C_DIM    GFX_RGB(0x5A, 0x64, 0x70)
#define C_ACCENT GFX_RGB(0x7F, 0xD9, 0x62) /* the cursor, and what plays */
#define C_TITLE  GFX_RGB(0x62, 0xC8, 0xD9) /* where one is */
#define C_PLAY   GFX_RGB(0x24, 0x3A, 0x28)
#define C_SEL    GFX_RGB(0x6E, 0x55, 0x18)
#define C_FX     GFX_RGB(0xF0, 0xBE, 0x5A)
#define C_MUTE   GFX_RGB(0x9E, 0x55, 0x55)
#define C_BEAT   GFX_RGB(0x91, 0x9A, 0x9C)
#define C_ON_ACC GFX_RGB(0x0B, 0x14, 0x09)
#define C_WHITE  GFX_RGB(0xFF, 0xFF, 0xFF)

/* The twelve buttons in the two rows of six they sit in under the
 * knobs: position k of the map is this physical button. */
static const uint8_t APP_BTN_AT[12] = { PB_FX, PB_SCL, PB_ENV, PB_LFO, PB_EDIT, PB_GLO,
                                        PB_HOME, PB_SAVE, PB_ARP, PB_SEQ, PB_PLAY, PB_REC };
enum { BTN_MAP_Y = 215, BTN_MAP_H = 10, APP_X0 = 3, APP_INNER = 234 };

#endif
