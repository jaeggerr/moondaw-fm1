/* SPDX-License-Identifier: GPL-3.0-only */
/* What a hand does to a step, whichever screen it is on: PATTERN and
 * PHRASE write notes the same way. Each returns false when the step
 * could not be written because the pool is full -- the screen has to
 * say so. */
#ifndef EDIT_H
#define EDIT_H

#include "../core/song.h"

bool edit_write_note(Song *s, int track, int pat, int row, int k, uint8_t note);
/* The semitones, and under the lowest one OFF (on the first note only --
 * a cut is the whole step's) and then the empty slot. The encoders have
 * no push: emptying is turning all the way down. */
bool edit_turn_note(Song *s, int track, int pat, int row, int k, int d);
bool edit_turn_octave(Song *s, int track, int pat, int row, int k, int d);
bool edit_clear_step(Song *s, int track, int pat, int row);
/* The instrument in force on a row: the last one written at or above it,
 * else the first. What a note written there will sound like. */
int edit_instr_at(const Song *s, int track, int pat, int row);

#endif
