/* SPDX-License-Identifier: GPL-3.0-only */
/* The MIXER screen, validated on the panel on 28/10: a row per track --
 * volume, pan, reverb send, delay send -- under the row of the two
 * returns. mixer.c says how the hands move on it. */
#ifndef MIXER_H
#define MIXER_H

#include <stdbool.h>

#include "../platform/platform.h"

/* `back` names where it was opened from (unused since the grid: GLO says
 * it); `entry` the phrase PLAY starts the song from. */
void mixer_open(const char *back, int entry);
/* One frame: GLO goes back, EDIT to the PROJECT (29/10). */
enum { MIXER_BACK, MIXER_STAY, MIXER_PROJECT };
int mixer_frame(const PlatInput *in);

#endif
