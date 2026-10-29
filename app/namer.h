/* SPDX-License-Identifier: GPL-3.0-only */
/* Typing a name on a machine with no letters, as validated with PROJECT on
 * 28/10: one cell a letter, KNOB 1 the place, KNOB 3 the letter (space,
 * A-Z, 0-9, -); SEL keeps it, GLO goes back without it. PROJECT names the
 * song with it, INSTRUMENT an instrument. */
#ifndef NAMER_H
#define NAMER_H

#include <stdbool.h>

#include "../platform/platform.h"

/* `target` is `len` characters at most, NUL-terminated. */
void namer_open(char *target, int len);
enum { NAMER_TYPING, NAMER_KEPT, NAMER_LEFT };
/* One frame, with the buttons pressed this frame. NAMER_KEPT: the target
 * holds the new name (unchanged when it was left empty). */
int namer_frame(const PlatInput *in, uint32_t pressed);
void namer_draw(void);

#endif
