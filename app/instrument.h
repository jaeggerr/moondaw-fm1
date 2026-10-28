/* SPDX-License-Identifier: GPL-3.0-only */
/* The INSTRUMENT screen, as agreed on 28/10: the name, a preview on PLAY,
 * the settings grouped on rows whose column names change with the row.
 * instrument.c says how the hands move on it. */
#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include <stdbool.h>

#include "../platform/platform.h"

/* `entry` is the phrase it was opened from (PLAY is the preview now). */
void instrument_open(int instr, int entry);
/* One frame. False once the hand asked to leave (GLO). */
bool instrument_frame(const PlatInput *in);

#endif
