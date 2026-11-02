/* SPDX-License-Identifier: GPL-3.0-only */
/* SAMPLES (2/11), from HOME's root: what the sample bank holds, and the
 * one thing that could not be done without a computer -- taking a sample
 * out of it. The bank shares the flash with the songs (core/bank.h), so a
 * sample never played is a song's worth of room, and until now the only
 * way to free it was to send the whole bank again from the computer.
 *
 * The samples by name, CONDENSED 20, seven rows, what each one takes at
 * its right and what is left of the flash in the corner. SELECT or KNOB 1
 * moves, SEL asks, SEL again deletes -- a flash erase has no UNDO, as in
 * PROJECTS' DELETE -- HOME goes back. Filling the bank stays the
 * computer's (tools/fm1_samples.py); here one can only be taken away.
 *
 * A row no song on the machine names says UNUSED, small, left of its size
 * (2/11, his ask -- he had asked for two sections, used and unused, and a
 * mark keeps the list in the order one reads it by). It is read out of the
 * flash when the screen opens, every song as the text it is
 * (core/bank.h, bank_used); a song that will not read leaves every row
 * unmarked, which is the way round that cannot invite a mistake. The
 * question says it again, the other way: whether a song still names what
 * is about to go.
 *
 * PLAY hears the one under the cursor (2/11, his ask): « des fois que le
 * nom n'est pas clair ». tr9_mtom says little and tr9_ltom says the same
 * thing; the sound says which of the two can go. It is heard through no
 * instrument of the song and no zone, as it was recorded
 * (core/player.h, player_audition_sample). While a song plays, PLAY keeps
 * the reflex it has everywhere else and stops it. */
#ifndef SAMPLES_H
#define SAMPLES_H

#include <stdbool.h>

#include "../platform/platform.h"

void samples_open(void);
/* One frame. False once the hand asked to leave (HOME). */
bool samples_frame(const PlatInput *in);

#endif
