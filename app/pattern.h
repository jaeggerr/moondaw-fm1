/* SPDX-License-Identifier: GPL-3.0-only */
/* The PATTERN screen, validated on the panel on 28/10: one block of one
 * track in three views (NOTES, FX, INS). pattern.c says how the hands
 * move on it. */
#ifndef PATTERN_H
#define PATTERN_H

#include "../core/song.h"
#include "../platform/platform.h"

/* False when the phrase plays nothing on that track: there is no block
 * to open. */
bool pattern_open(Song *s, int entry, int track);
enum { PATTERN_STAY, PATTERN_BACK, PATTERN_SONG, PATTERN_INSTR, PATTERN_PROJECT };
/* Right after pattern_open, when the SECTION opened it: the cursor
 * starts on the step it was on. */
void pattern_from_phrase(int row);
/* One frame. PATTERN_BACK: the hand asked to leave by HOME, to where it
 * came from; PATTERN_SONG: an UNDO took the block away.
 * PATTERN_INSTR: it asked for the instrument of the step under the
 * cursor; pattern_instr() says which, pattern_entry() the phrase. */
int pattern_frame(const PlatInput *in);
int pattern_instr(void);
int pattern_entry(void);
/* Whether the SYLLABLE screen is in front (HELP's topic). */
bool pattern_in_syllable(void);
/* Back from INSTRUMENT: the screen as it was left. */
void pattern_resume(void);

#endif
