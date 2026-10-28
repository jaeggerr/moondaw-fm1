/* SPDX-License-Identifier: GPL-3.0-only */
/* The CANVAS: a screen the computer draws on (link.h's CANVAS command), to
 * try a screen on the panel without building a firmware. What it draws is
 * kept as a list -- rectangles, text in the machine's fonts, lines -- and
 * the screen is redrawn from it band by band like any other; raw pixels go
 * straight to the panel instead, there being no frame buffer to keep them
 * in, and a change to the list draws over them. GLO leaves it. */
#ifndef CANVAS_H
#define CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/platform.h"

enum { CANVAS_CLEAR, CANVAS_FILL, CANVAS_TEXT, CANVAS_LINE, CANVAS_PIXELS };
/* One drawing order, as the link carries it after its op (canvas.c says
 * each one's bytes). False when it is malformed or the list is full. */
bool canvas_draw(int op, const uint8_t *p, uint32_t n);
void canvas_open(void);
/* One frame. False once the hand asked to leave (GLO). */
bool canvas_frame(const PlatInput *in);

#endif
