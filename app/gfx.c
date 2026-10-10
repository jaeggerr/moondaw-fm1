/* SPDX-License-Identifier: GPL-3.0-only */
#include "gfx.h"

#include "../platform/platform.h"

#define BAND 16
#define NBANDS (PLAT_H / BAND)

static uint16_t band[PLAT_W * BAND]; /* the DMA reads it: RAM, not stack */
static uint32_t shown[NBANDS];
static uint8_t force = 1;
static int by; /* first line of the band being drawn */

void gfx_invalidate(void) { force = 1; }

static uint16_t mix(uint16_t d, uint16_t s, int a)
{
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    return (uint16_t)((dr + (((sr - dr) * a) >> 8)) << 11 |
                      (dg + (((sg - dg) * a) >> 8)) << 5 |
                      (db + (((sb - db) * a) >> 8)));
}

void gfx_fill(int x, int y, int w, int h, uint16_t c, int a)
{
    int x1 = x + w > PLAT_W ? PLAT_W : x + w;
    int y0 = y < by ? by : y, y1 = y + h > by + BAND ? by + BAND : y + h;
    if (x < 0)
        x = 0;
    if (a >= 255)
        a = 256;
    for (int j = y0; j < y1; j++) {
        uint16_t *p = &band[(j - by) * PLAT_W];
        for (int i = x; i < x1; i++)
            p[i] = a == 256 ? c : mix(p[i], c, a);
    }
}

void gfx_slab(int x, int y, int w, int h, uint16_t c, int a)
{
    if (w < 3 || h < 3) {
        gfx_fill(x, y, w, h, c, a);
        return;
    }
    gfx_fill(x + 1, y, w - 2, 1, c, a);
    gfx_fill(x, y + 1, w, h - 2, c, a);
    gfx_fill(x + 1, y + h - 1, w - 2, 1, c, a);
}

void gfx_frame_rect(int x, int y, int w, int h, uint16_t c, int a)
{
    gfx_fill(x + 1, y, w - 2, 1, c, a);
    gfx_fill(x + 1, y + h - 1, w - 2, 1, c, a);
    gfx_fill(x, y + 1, 1, h - 2, c, a);
    gfx_fill(x + w - 1, y + 1, 1, h - 2, c, a);
}

void gfx_dash_v(int x, int y, int h, uint16_t c, int a)
{
    for (int j = 0; j < h; j++)
        if (((y + j) & 3) < 2)
            gfx_fill(x, y + j, 1, 1, c, a);
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;

    for (;;) {
        if (y0 >= by && y0 < by + BAND && x0 >= 0 && x0 < PLAT_W)
            band[(y0 - by) * PLAT_W + x0] = c;
        if (x0 == x1 && y0 == y1)
            break;
        if (2 * err >= dy) {
            err += dy;
            x0 += sx;
        } else {
            err += dx;
            y0 += sy;
        }
    }
}

static const Glyph *glyph_of(const Font *f, const char **s)
{
    unsigned char ch = (unsigned char)*(*s)++;
    /* The one character outside ASCII: U+2039, the back label's guillemet. */
    if (ch == 0xE2 && (unsigned char)(*s)[0] == 0x80 && (unsigned char)(*s)[1] == 0xB9) {
        *s += 2;
        return &f->g[95];
    }
    return &f->g[ch >= 32 && ch < 127 ? ch - 32 : 0];
}

int gfx_text_w(int font, const char *s)
{
    const Font *f = &FONTS[font];
    int w = 0;
    while (*s)
        w += glyph_of(f, &s)->adv;
    return w;
}

int gfx_text(int font, int x, int y, const char *s, uint16_t c)
{
    const Font *f = &FONTS[font];
    while (*s) {
        const Glyph *g = glyph_of(f, &s);
        int gx = x + g->x, gy = y - f->ascent + g->y;
        if (gy < by + BAND && gy + g->h > by)
            for (int j = 0; j < g->h; j++) {
                int py = gy + j;
                if (py < by || py >= by + BAND)
                    continue;
                for (int i = 0; i < g->w; i++) {
                    int k = j * g->w + i, px = gx + i;
                    int n = (f->px[g->at + k / 2] >> (k & 1 ? 0 : 4)) & 15;
                    if (n && px >= 0 && px < PLAT_W) {
                        uint16_t *p = &band[(py - by) * PLAT_W + px];
                        *p = n == 15 ? c : mix(*p, c, n * 17);
                    }
                }
            }
        x += g->adv;
    }
    return x;
}

void gfx_text_mid(int font, int x, int w, int y, const char *s, uint16_t c)
{
    gfx_text(font, x + (w - gfx_text_w(font, s)) / 2, y, s, c);
}

void gfx_frame(void (*draw)(void))
{
    for (int b = 0; b < NBANDS; b++) {
        uint32_t h = 2166136261u;
        by = b * BAND;
        draw();
        for (int i = 0; i < PLAT_W * BAND; i++)
            h = (h ^ band[i]) * 16777619u;
        if (h == shown[b] && !force)
            continue;
        shown[b] = h;
#ifdef PLAT_FM1
        for (int i = 0; i < PLAT_W * BAND; i++) /* the panel wants the high byte first */
            band[i] = (uint16_t)(band[i] << 8 | band[i] >> 8);
#endif
        plat_blit(0, by, PLAT_W, BAND, band);
    }
    force = 0;
}
