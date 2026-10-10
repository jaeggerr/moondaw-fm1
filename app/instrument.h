/* SPDX-License-Identifier: GPL-3.0-only */
/* The INSTRUMENT screen, after proto/instrument.html: the settings of
 * one sound in rows of four, because the right hand has four knobs. */
#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include <stdbool.h>

#include "../platform/platform.h"

/* `entry` is the phrase PLAY plays, to hear the sound in its song. */
void instrument_open(int instr, int entry);
/* One frame. False once the hand asked to leave (HOME). */
bool instrument_frame(const PlatInput *in);

#endif
