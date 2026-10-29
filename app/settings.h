/* SPDX-License-Identifier: GPL-3.0-only */
/* SETTINGS (29/10): what holds for the machine rather than the song,
 * reached by GLO from HOME -- GLO is what is global, as it opens PROJECT
 * from SONG. One setting a row, as PROJECT:
 *
 *   LEDS   OFF, STATE (PLAY's alone), GUIDE (and what each screen can do)
 *   LIGHT  how bright the guide is, 15 to 100 %; on its row a few buttons
 *          light at it, to judge
 *
 *   SELECT, KNOB 2  the setting     KNOB 3  its value     GLO  back to HOME
 * Kept in flash beside which song was open, when the screen is left. */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

#include "../platform/platform.h"

void settings_open(void);
/* One frame. False once the hand asked to leave (GLO). */
bool settings_frame(const PlatInput *in);
/* Leaving, by GLO or HOME: what changed goes to the flash. */
void settings_close(void);

#endif
