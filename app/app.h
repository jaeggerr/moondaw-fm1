/* SPDX-License-Identifier: GPL-3.0-only */
/* What the screens share: the one song in memory, where it lives in
 * flash, and whether it has been touched since it was last written. */
#ifndef APP_H
#define APP_H

#include "../core/song.h"
#include "../core/store.h"
#include "../platform/platform.h"
#include "theme.h"

#define APP_SONG_SLOTS 31 /* then slot 31 remembers which song was open, and 32 is the sample bank */

extern Song app_song;
extern char app_name[STORE_NAME + 1];
extern int app_slot;   /* its place in flash, -1 while it has none */
extern bool app_dirty; /* edited since the last save or load */
/* Whether there is a song in memory at all. The root holds none (30/10):
 * reaching it puts the song down, so nothing unsaved can be carried
 * around unseen -- which is how a flash once took his work with it. */
extern bool app_loaded;

void app_mark_dirty(void);
/* SETTINGS changed: into the flash, beside which song was open. */
void app_keep_settings(void);
int app_save(void);          /* 0 when written; stops the song first */
int app_open_slot(int slot); /* 0 when loaded; the song in memory is untouched otherwise */
void app_new(void);          /* an empty song, named SONG nn, not yet in flash */
/* The song put down: the player stopped and the memory cleared. The name
 * and the slot stay, as what CONTINUE would pick up again. */
void app_unload(void);
/* Lines and tokens the song's file had that this machine does not carry. */
int app_not_carried(void);
/* Where the hand is: HOME, SONG, SECTION, PATTERN, INSTRUMENT, MIXER, PROJECT, CANVAS. */
const char *app_screen(void);
/* The computer's CANVAS in front, over whatever was there (GLO goes back). */
void app_show_canvas(void);
/* The audio interrupt's share of its time, read once a frame: now, the
 * worst block of the last frame, and the worst since the computer's
 * STATUS set it back to 0. */
extern int app_cpu, app_block, app_worst;


/* The twelve buttons in the two rows of six they sit in under the
 * knobs: position k of the map is this physical button. */
static const uint8_t APP_BTN_AT[12] = { PB_FX, PB_SEL, PB_ENV, PB_LFO, PB_EDIT, PB_GLO,
                                        PB_HOME, PB_SAVE, PB_ARP, PB_SEQ, PB_PLAY, PB_REC };
enum { BTN_MAP_Y = 215, BTN_MAP_H = 10, APP_X0 = 3, APP_INNER = 234 };

#endif
