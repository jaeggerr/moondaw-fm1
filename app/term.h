/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen as the tracker sees it: 30x30 cells of 8x8, a character
 * and two palette entries each. 2.7 KB instead of a 115 KB framebuffer,
 * and only the cells that changed go down the SPI. */
#ifndef TERM_H
#define TERM_H

#include <stdint.h>

#define TERM_COLS 30
#define TERM_ROWS 30

enum { C_BLACK, C_DIM, C_TEXT, C_WHITE, C_CYAN, C_GREEN, C_AMBER, C_RED, C_COUNT };

void term_clear(void);
void term_put(int x, int y, const char *s, int fg, int bg);
void term_putc(int x, int y, char c, int fg, int bg);
void term_num(int x, int y, int32_t v, int width, int fg, int bg); /* right-aligned */
void term_flush(void); /* blits what changed since the last flush */
void term_invalidate(void); /* the panel shows something else: resend all */

#endif
