/* SPDX-License-Identifier: GPL-3.0-only */
/* The PATTERN screen, after proto/pattern.html: one block of one track,
 * nine slots a step. The left hand moves (SELECT the step, ALGORITHM the
 * field, PRESETS the block), the right hand writes (KNOB 1-4 are the
 * four cards under the bar), the keys write notes. */
#ifndef PATTERN_H
#define PATTERN_H

#include "../core/song.h"
#include "../platform/platform.h"

/* False when the phrase plays nothing on that track: there is no block
 * to open. */
bool pattern_open(Song *s, int entry, int track);
/* The app's: writes the song to flash, 0 when done. */
int app_save(void);
/* One frame. False once the hand asked to leave (HOME). */
bool pattern_frame(const PlatInput *in);

#endif
