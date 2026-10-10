/* SPDX-License-Identifier: GPL-3.0-only */
/* Where the machine opens, after proto/menu.html: the song in memory,
 * the songs in flash, and the row that makes a new one. */
#ifndef MENU_H
#define MENU_H

#include <stdbool.h>

#include "../platform/platform.h"

void menu_open(void);
/* One frame. True once the hand opened the song in memory: the caller
 * goes into it. */
bool menu_frame(const PlatInput *in);

#endif
