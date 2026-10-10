/* SPDX-License-Identifier: GPL-3.0-only */
/* The PHRASE screen, after proto/phrase.html: the eight tracks of one
 * phrase side by side, one row a step, the cell is the step's first
 * note. What SONG does not show (the notes) and PATTERN does not (the
 * other tracks). */
#ifndef PHRASE_H
#define PHRASE_H

#include <stdbool.h>

#include "../platform/platform.h"

void phrase_open(int entry, int track);
/* Back from PATTERN or MIXER: the screen as it was left. */
void phrase_resume(void);
enum { PHRASE_STAY, PHRASE_BACK, PHRASE_PATTERN, PHRASE_MIXER };
/* One frame. PHRASE_PATTERN: the hand opened the block under the
 * cursor; phrase_track() says whose, phrase_row() the step of that
 * block the cursor was on. */
int phrase_frame(const PlatInput *in);
int phrase_entry(void);
int phrase_track(void);
int phrase_row(void);

#endif
