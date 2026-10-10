/* SPDX-License-Identifier: GPL-3.0-only */
/* The SONG screen, after proto/song.html: the arrangement, one row a
 * phrase, the eight tracks side by side, a block number in each cell. */
#ifndef SONGVIEW_H
#define SONGVIEW_H

#include <stdbool.h>

#include "../platform/platform.h"

enum { SONGVIEW_STAY, SONGVIEW_MENU, SONGVIEW_PATTERN };

void songview_open(bool reset_cursor);
/* One frame. SONGVIEW_PATTERN: the hand opened a block; *entry and
 * *track say which. */
int songview_frame(const PlatInput *in, int *entry, int *track);

#endif
