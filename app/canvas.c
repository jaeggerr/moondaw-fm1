/* SPDX-License-Identifier: GPL-3.0-only */
#include "canvas.h"

#include "gfx.h"

/* The orders, as they came, after a CLEAR's background:
 *   FILL    x y w h colour          (u8 x4, u16)
 *   TEXT    x y font colour chars   (u8 x3, u16, then the text; y is the baseline;
 *                                    font 0 CONDENSED 20, 1 14, 2 12, 3 MONO 8)
 *   LINE    x0 y0 x1 y1 colour      (u8 x4, u16)
 *   PIXELS  x y count, then count colours (u16): drawn now, not kept
 * Colours are RGB565, low byte first. */
enum { LIST = 4096 };

static uint8_t list[LIST];
static uint32_t used;
static uint16_t background;
static bool changed;
static uint32_t prev_btns;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void draw(void)
{
    static const int fonts[4] = { F_COND20, F_COND14, F_COND12, F_MONO8 };
    gfx_fill(0, 0, PLAT_W, PLAT_H, background, 255);
    for (uint32_t i = 0; i < used;) {
        const uint8_t *p = &list[i + 2];
        int op = list[i], n = list[i + 1];
        if (op == CANVAS_FILL) {
            gfx_fill(p[0], p[1], p[2], p[3], rd16(p + 4), 255);
        } else if (op == CANVAS_LINE) {
            gfx_line(p[0], p[1], p[2], p[3], rd16(p + 4));
        } else if (op == CANVAS_TEXT) {
            char s[64];
            int k = 0;
            for (; k < n - 5 && k < 63; k++)
                s[k] = (char)p[5 + k];
            s[k] = 0;
            gfx_text(fonts[p[2] & 3], p[0], p[1], s, rd16(p + 3));
        }
        i += 2u + (uint32_t)n;
    }
}

bool canvas_draw(int op, const uint8_t *p, uint32_t n)
{
    if (op == CANVAS_CLEAR) {
        if (n != 2)
            return false;
        background = rd16(p);
        used = 0;
        changed = true;
        return true;
    }
    if (op == CANVAS_PIXELS) { /* straight to the panel: nothing keeps them */
        static uint16_t row[PLAT_W];
        uint32_t count = n >= 3 ? p[2] : 0;
        if (n != 3 + 2 * count || !count || p[0] + count > PLAT_W || p[1] >= PLAT_H)
            return false;
        for (uint32_t k = 0; k < count; k++) {
            uint16_t c = rd16(p + 3 + 2 * k);
#ifdef PLAT_FM1
            c = (uint16_t)(c << 8 | c >> 8); /* the panel wants the high byte first */
#endif
            row[k] = c;
        }
        plat_blit(p[0], p[1], (int)count, 1, row);
        return true;
    }
    if ((op == CANVAS_FILL || op == CANVAS_LINE) ? n != 6 : op != CANVAS_TEXT || n < 6 || n > 5 + 63)
        return false;
    if (used + 2 + n > LIST)
        return false;
    list[used] = (uint8_t)op;
    list[used + 1] = (uint8_t)n;
    for (uint32_t k = 0; k < n; k++)
        list[used + 2 + k] = p[k];
    used += 2 + n;
    changed = true;
    return true;
}

void canvas_open(void)
{
    changed = true;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

bool canvas_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    prev_btns = in->btns;
    if (changed) { /* drawn again only when the list changed: raw pixels stay until then */
        gfx_invalidate();
        gfx_frame(draw);
        changed = false;
    }
    return !(btns & 1u << PB_GLO);
}
