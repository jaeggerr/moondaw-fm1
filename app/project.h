/* SPDX-License-Identifier: GPL-3.0-only */
/* The PROJECT screen, validated on the panel on 28/10, reached by EDIT:
 * what holds for the whole song, one setting a row -- its name, tempo,
 * swing, meter, the step's worth, ticks, tracks. project.c says how the
 * hands move on it. */
#ifndef PROJECT_H
#define PROJECT_H

#include <stdbool.h>

#include "../platform/platform.h"

void project_open(void);
/* One frame. False once the hand asked to leave (GLO). */
bool project_frame(const PlatInput *in);

#endif
