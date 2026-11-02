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
 * computer's (tools/fm1_samples.py); here one can only be taken away. */
#ifndef SAMPLES_H
#define SAMPLES_H

#include <stdbool.h>

#include "../platform/platform.h"

void samples_open(void);
/* One frame. False once the hand asked to leave (HOME). */
bool samples_frame(const PlatInput *in);

#endif
