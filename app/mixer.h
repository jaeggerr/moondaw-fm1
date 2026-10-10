/* SPDX-License-Identifier: GPL-3.0-only */
/* The MIXER screen, after proto/mixer.html: a row per track -- volume,
 * pan, reverb send, delay send, the four knobs -- under the row of the
 * two returns. */
#ifndef MIXER_H
#define MIXER_H

#include <stdbool.h>

#include "../platform/platform.h"

/* `back` is the word of the way out ("SONG", "PAT"); `entry` the phrase
 * PLAY starts the song from. */
void mixer_open(const char *back, int entry);
/* One frame. False once the hand asked to leave (HOME). */
bool mixer_frame(const PlatInput *in);

#endif
