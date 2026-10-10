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
enum { PATTERN_STAY, PATTERN_BACK, PATTERN_SONG, PATTERN_INSTR, PATTERN_MIXER };
/* Right after pattern_open, when PHRASE opened it: HOME leads back
 * there, and the cursor starts on the step it was on. */
void pattern_from_phrase(int row);
/* One frame. PATTERN_BACK: the hand asked to leave by HOME, to where it
 * came from; PATTERN_SONG: by the SONG button.
 * PATTERN_INSTR: it asked for the instrument of the step under the
 * cursor; pattern_instr() says which, pattern_entry() the phrase. */
int pattern_frame(const PlatInput *in);
int pattern_instr(void);
int pattern_entry(void);
/* Back from INSTRUMENT: the screen as it was left. */
void pattern_resume(void);

#endif
