/* SPDX-License-Identifier: GPL-3.0-only */
#include "canvas.h"

#include "gfx.h"

/* The orders' bytes, after their op and length:
 *   CLEAR   colour                  (u16): the list emptied, a new background
 *   FILL    x y w h colour          (u8 x4, u16)
 *   TEXT    x y font colour chars   (u8 x3, u16, then the text; y is the baseline;
 *                                    font 0 CONDENSED 20, 1 14, 2 12, 3 MONO 8)
 *   LINE    x0 y0 x1 y1 colour      (u8 x4, u16)
 *   PIXELS  x y count, then count colours (u16): drawn now, not kept
 *   KEEP    flags                   (bit 0: GLO is the computer's, not the way out)
 * Colours are RGB565, low byte first. */
enum { LIST = 4096 };

static uint8_t list[LIST];
static uint32_t used;
static uint16_t background;
static bool changed, pixels, keep_glo;
static uint32_t prev_btns, prev_keys;
/* The gestures not yet collected. */
static uint32_t ev_pressed, ev_released, ev_down, ev_up, ev_held;
static int ev_turns[PLAT_ENCS];

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

static bool pixels_now(const uint8_t *p, uint32_t n)
{
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
    pixels = true;
    return true;
}

static bool order(int op, const uint8_t *p, uint32_t n)
{
    if (op == CANVAS_CLEAR) {
        if (n != 2)
            return false;
        background = rd16(p);
        used = 0;
        changed = true;
        return true;
    }
    if (op == CANVAS_KEEP) {
        if (n != 1)
            return false;
        keep_glo = p[0] & 1;
        return true;
    }
    if (op == CANVAS_PIXELS)
        return pixels_now(p, n);
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

bool canvas_orders(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n;) {
        if (i + 2 > n || i + 2 + p[i + 1] > n || !order(p[i], p + i + 2, p[i + 1]))
            return false;
        i += 2u + p[i + 1];
    }
    return true;
}

static void put32(uint8_t *o, uint32_t v)
{
    o[0] = (uint8_t)v, o[1] = (uint8_t)(v >> 8), o[2] = (uint8_t)(v >> 16), o[3] = (uint8_t)(v >> 24);
}

void canvas_events(uint8_t out[CANVAS_EVENTS])
{
    put32(out, ev_pressed);
    put32(out + 4, ev_released);
    put32(out + 8, ev_down);
    put32(out + 12, ev_up);
    put32(out + 16, ev_held);
    for (int e = 0; e < PLAT_ENCS; e++) {
        int v = ev_turns[e];
        out[20 + e] = (uint8_t)(int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
        ev_turns[e] = 0;
    }
    ev_pressed = ev_released = ev_down = ev_up = 0;
}

void canvas_open(void)
{
    changed = true;
    keep_glo = false;
    ev_pressed = ev_released = ev_down = ev_up = 0;
    for (int e = 0; e < PLAT_ENCS; e++)
        ev_turns[e] = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

bool canvas_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;

    if (prev_btns != 0xFFFFFFFFu) { /* the gestures, for the computer to collect */
        ev_pressed |= btns;
        ev_released |= prev_btns & ~in->btns;
        ev_down |= in->keys & ~prev_keys;
        ev_up |= prev_keys & ~in->keys;
    }
    ev_held = in->keys;
    for (int e = 0; e < PLAT_ENCS; e++)
        ev_turns[e] += in->enc[e];
    prev_btns = in->btns;
    prev_keys = in->keys;

    if (changed) {
        /* Only the bands that changed go down the SPI -- unless raw pixels were
         * drawn behind gfx's back: then all of them, to draw over them. */
        if (pixels)
            gfx_invalidate();
        gfx_frame(draw);
        changed = pixels = false;
    }
    return keep_glo || !(btns & 1u << PB_GLO);
}
