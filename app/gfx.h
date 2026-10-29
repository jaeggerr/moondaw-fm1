/* SPDX-License-Identifier: GPL-3.0-only */
/* Painting in pixels, for the screens drawn after proto/: filled
 * rectangles that blend, and antialiased type baked by
 * tools/gen_fonts.py.
 *
 * There is no framebuffer -- 240 x 240 in RGB565 is 115 KB the FM-1
 * would rather keep for the song. A screen is a function that draws
 * everything; gfx_frame() calls it once per BAND of 16 lines, clipped
 * to that band, and sends a band down the SPI only when its pixels
 * differ from what the panel already shows. One band is 7.7 KB. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#ifdef GFX_SPECIMEN
#include "fonts_spec.h" /* app/type.c: the sizes to weigh on the panel */
#else
#include "fonts.h"
#endif

#define GFX_RGB(r, g, b) \
    ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

void gfx_frame(void (*draw)(void));
void gfx_invalidate(void); /* the panel shows something else: resend all */
/* Band `b` (0..14) of what the last gfx_frame() drew, drawn again for
 * the computer to look at (link.h's SCREEN), RGB565 as gfx paints it;
 * it is good until the next gfx call. Raw pixels sent to the panel
 * behind gfx's back are not in it. NULL before the first frame. */
const uint16_t *gfx_band(int b);

/* `a` is the coverage, 0..255: 255 replaces, less blends over. */
void gfx_fill(int x, int y, int w, int h, uint16_t c, int a);
/* The same with the four corner pixels left alone: a radius of one. */
void gfx_slab(int x, int y, int w, int h, uint16_t c, int a);
void gfx_frame_rect(int x, int y, int w, int h, uint16_t c, int a);
void gfx_dash_v(int x, int y, int h, uint16_t c, int a); /* 2 on, 2 off */
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);

/* `y` is the BASELINE. Returns the x after the last glyph. */
int gfx_text(int font, int x, int y, const char *s, uint16_t c);
int gfx_text_w(int font, const char *s);
void gfx_text_mid(int font, int x, int w, int y, const char *s, uint16_t c);

#endif
