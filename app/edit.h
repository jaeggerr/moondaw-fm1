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
/* The note key k (0..PLAT_KEYS-1, the first F) plays at octave `oct` on
 * instrument `instr`: F-3 up. On a DRUM kit the keys are its drums
 * instead, one a key, and `oct` does not move them: key k is the kit's
 * k-th drum at the pitch it was designed for, -1 past the last. */
int edit_key_note(const Song *s, int instr, int oct, int k);
/* Which keys a kit answers to, a bit a key, for the screens to light; 0
 * where the instrument is not a kit and the whole keyboard plays. */
uint32_t edit_key_drums(const Song *s, int instr);

/* --- the clipboard ------------------------------------------------------
 * ONE buffer: a run of steps and a mask of the fields it holds. What the
 * mask contains is what the selection covered, and paste writes those
 * fields and leaves the rest of the target alone -- an fx copied on its
 * own drops onto another step without touching its notes. As moondaw's. */
enum {
    EDIT_CLIP_N1 = 1 << 0, /* .. N4 = 1 << 3 */
    EDIT_CLIP_INSTR = 1 << 4,
    EDIT_CLIP_FX1 = 1 << 5, /* .. FX3 = 1 << 7 */
    EDIT_CLIP_ALL = 0xFF,
};
void edit_clip_reset(void);
int edit_clip_len(void); /* 0 = nothing copied yet */
void edit_clip_copy(const Song *s, int track, int pat, int from, int count, int mask);
/* At `row`, downwards, stopping at the end of the block rather than
 * wrapping. An fx-ONLY clipboard lands on `fx_slot` when it is >= 0 --
 * what was copied is the command, not the slot it came from -- sliding
 * back to fit. An fx that would say the same thing twice on a step is
 * refused. Returns the rows written, -1 when the pool is full. */
int edit_clip_paste(Song *s, int track, int pat, int row, int fx_slot);
/* What a copy would take, this clears. No OFF is left behind. */
bool edit_erase_range(Song *s, int track, int pat, int from, int count, int mask);

#endif
