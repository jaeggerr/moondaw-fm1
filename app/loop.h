/* SPDX-License-Identifier: GPL-3.0-only */
/* The listening loop of PATTERN and PHRASE, after proto/loop.html: a
 * few steps of one phrase played round while they are worked on. One
 * button does it, where the cursor is: the first press sets one end,
 * the second the other, the third forgets both. One loop for the two
 * screens, in steps of the phrase; never saved, never in UNDO. */
#ifndef LOOP_H
#define LOOP_H

#include <stdbool.h>

enum { LOOP_NONE, LOOP_ONE_END, LOOP_SET };

void loop_clear(void);
/* A screen says which phrase it shows: a loop set on another is gone. */
void loop_keep(int entry);
void loop_press(int entry, int row);
int loop_state(void);
int loop_from(void); /* the first step, or the one end set so far */
int loop_to(void);   /* the last step, inclusive */
bool loop_has(int row);

#endif
