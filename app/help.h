/* SPDX-License-Identifier: GPL-3.0-only */
/* HELP (29/10), from HOME's root: a list of topics -- a page a screen,
 * GESTURES, EFFECTS -- its cursor already on the screen one came from, so
 * there is a help of where one is with no button of its own. A page says
 * a gesture in two lines, the control white and what it does grey under
 * it, both in CONDENSED 20 (prototype tools/protos/help.py, layout B: the
 * smaller type of A read worse on the panel).
 *
 *   SELECT, KNOB 1  the topic, then the page's lines     SEL  open a topic
 *   HOME  back: from a page to the list, from the list to HOME
 * EFFECTS is a list of the effects and their line; SEL opens an effect's
 * whole page, the other apps' text word for word (app/fx_help.c), KNOB 2
 * the effect before or after it. */
#ifndef HELP_H
#define HELP_H

#include <stdbool.h>

#include "../platform/platform.h"

enum { HELP_HOME, HELP_SONG, HELP_PATTERN, HELP_SYLLABLE, HELP_INSTRUMENT, HELP_MIXER,
       HELP_PROJECT, HELP_SETTINGS, HELP_GESTURES, HELP_EFFECTS, HELP_TOPICS };
/* `topic` where the cursor starts: the screen one came from. */
void help_open(int topic);
/* One frame. False once the hand left the list (HOME). */
bool help_frame(const PlatInput *in);

#endif
