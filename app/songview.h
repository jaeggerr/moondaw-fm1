/* SPDX-License-Identifier: GPL-3.0-only */
/* The SONG screen, validated on the panel on 28/10: the arrangement, one
 * row a phrase, the block each track plays in it. songview.c says how the
 * hands move on it. */
#ifndef SONGVIEW_H
#define SONGVIEW_H

#include <stdbool.h>

#include "../platform/platform.h"

enum { SONGVIEW_STAY, SONGVIEW_PATTERN, SONGVIEW_MIXER, SONGVIEW_PHRASE, SONGVIEW_PROJECT, SONGVIEW_BACK };
int songview_entry(void); /* the phrase under the cursor */

void songview_open(bool reset_cursor);
/* Leaving by HOME: a block panel open is closed as by GLO. */
void songview_cancel(void);
/* One frame. SONGVIEW_PATTERN: EDIT opened a block; *entry and *track say
 * which. SONGVIEW_PHRASE: SEL opened a phrase by its number. */
int songview_frame(const PlatInput *in, int *entry, int *track);

#endif
