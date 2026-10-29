/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM (29/10), from HOME's root: what the machine holds and runs, read
 * at a glance -- the flash the songs (and, to come, the samples) share and
 * what is left of it, the firmware's version and identity, the CPU's worst
 * block. Nothing to change here; HOME goes back. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>

#include "../platform/platform.h"

void system_open(void);
/* One frame. False once the hand asked to leave (HOME). */
bool system_frame(const PlatInput *in);

#endif
