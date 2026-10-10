/* SPDX-License-Identifier: GPL-3.0-only */
/* UNDO: a journal of what the song held before each edit. Not copies of
 * the song -- it is 77 KB and the machine has 72 for everything else --
 * but the few bytes an edit is about to replace.
 *
 * One press takes back one GESTURE: everything written in one frame,
 * and what the same hand went on writing at the same place within a
 * second (a knob turned twelve notches is one gesture, not twelve).
 * When the journal is full the oldest gestures are forgotten. */
#ifndef UNDO_H
#define UNDO_H

#include "../core/song.h"

void undo_reset(Song *s);     /* another song: nothing to take back */
void undo_frame(uint32_t ms); /* once a frame, before the screen */

/* BEFORE writing. A step is named by where it is, not by its address:
 * the pool moves steps around when it is compacted. */
void undo_step(int track, int pat, int row);
/* Anything else in the song, outside the pool: a field, a struct, a run
 * of the arrangement. */
void undo_bytes(void *p, uint32_t len);

bool undo_can(void);
bool undo_pop(void); /* true when something was taken back */

#endif
