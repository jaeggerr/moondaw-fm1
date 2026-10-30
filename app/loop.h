/* SPDX-License-Identifier: GPL-3.0-only */
/* The listening loop of PATTERN, PHRASE and SONG, after proto/loop.html:
 * a few steps of one phrase, or a few phrases of the song, played round
 * while they are worked on. One button does it, where the cursor is: the
 * first press sets one end, the second the other, the third forgets both.
 * One loop for the three screens -- `entry` names the phrase whose steps
 * it counts, or -1 for SONG, where it counts the phrases themselves and
 * the player goes round them (30/10). Never saved, never in UNDO. */
#ifndef LOOP_H
#define LOOP_H

#include <stdbool.h>

enum { LOOP_NONE, LOOP_ONE_END, LOOP_SET };

void loop_clear(void);
/* A screen says which phrase it shows (-1: the song itself): a loop set on
 * another is gone. */
void loop_keep(int entry);
void loop_press(int entry, int row);
/* Both ends at once (PATTERN's OP6 on a selection): steps from..to of the
 * phrase `entry`. The same range again forgets it. */
void loop_set(int entry, int from, int to);
int loop_state(void);
int loop_from(void); /* the first step, or the one end set so far */
int loop_to(void);   /* the last step, inclusive */
bool loop_has(int row);

#endif
