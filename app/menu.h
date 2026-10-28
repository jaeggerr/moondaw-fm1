/* SPDX-License-Identifier: GPL-3.0-only */
/* HOME, where the machine opens and where the HOME button leads from
 * anywhere: the projects by name, and the row that makes a new one. */
#ifndef MENU_H
#define MENU_H

#include <stdbool.h>

#include "../platform/platform.h"

void menu_open(void);
/* One frame. True once SEL opened a project (now the song in memory):
 * the caller goes into it. */
bool menu_frame(const PlatInput *in);

#endif
