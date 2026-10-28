/* SPDX-License-Identifier: GPL-3.0-only */
/* The CANVAS: a screen the computer draws on (link.h's CANVAS command), to
 * try a screen on the panel without building a firmware -- still, or alive:
 * the hands' gestures on it are kept for the computer to collect (EVENTS),
 * so a script there can answer them and draw again.
 *
 * What it draws is kept as a list -- rectangles, text in the machine's
 * fonts, lines -- and the screen is redrawn from it band by band like any
 * other; raw pixels go straight to the panel instead, there being no frame
 * buffer to keep them in, and a change to the list draws over them. GLO
 * leaves it, unless the computer keeps GLO for itself; HOME always does. */
#ifndef CANVAS_H
#define CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/platform.h"

enum { CANVAS_CLEAR, CANVAS_FILL, CANVAS_TEXT, CANVAS_LINE, CANVAS_PIXELS, CANVAS_KEEP };
/* Drawing orders, one after the other, each `op, length, bytes` (canvas.c
 * says each one's bytes). False, and nothing after it done, at the first
 * malformed one or when the list is full. */
bool canvas_orders(const uint8_t *p, uint32_t n);
/* The gestures since the last call, 27 bytes: buttons pressed u32, released
 * u32, keys pressed u32, released u32, held u32, then the seven knobs'
 * turns, i8 each (PLAT_ENCS order). Collected, they start over. */
enum { CANVAS_EVENTS = 27 };
void canvas_events(uint8_t out[CANVAS_EVENTS]);
void canvas_open(void);
/* One frame. False once the hand asked to leave (GLO, unless kept). */
bool canvas_frame(const PlatInput *in);

#endif
