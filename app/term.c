/* SPDX-License-Identifier: GPL-3.0-only */
#include "term.h"

#include "../platform/platform.h"
#include "font/font8x8_basic.h"

typedef struct {
    uint8_t ch, col; /* col = fg | bg << 4 */
} Cell;

static Cell want[TERM_ROWS][TERM_COLS];
static Cell shown[TERM_ROWS][TERM_COLS];
static uint8_t all_dirty = 1;

/* One text row at most. In RAM and not on the stack: on the FM-1 the
 * blit is a DMA out of this buffer. */
static uint16_t strip[TERM_COLS * 8 * 8];

static const uint16_t pal[C_COUNT] = {
    PLAT_RGB(0x00, 0x00, 0x00), PLAT_RGB(0x50, 0x58, 0x60),
    PLAT_RGB(0xB8, 0xC0, 0xC8), PLAT_RGB(0xFF, 0xFF, 0xFF),
    PLAT_RGB(0x40, 0xD8, 0xF0), PLAT_RGB(0x60, 0xE0, 0x70),
    PLAT_RGB(0xF0, 0xB0, 0x30), PLAT_RGB(0xF0, 0x50, 0x50),
};

void term_invalidate(void) { all_dirty = 1; }

void term_clear(void)
{
    for (int y = 0; y < TERM_ROWS; y++)
        for (int x = 0; x < TERM_COLS; x++) {
            want[y][x].ch = ' ';
            want[y][x].col = C_TEXT;
        }
}

void term_putc(int x, int y, char c, int fg, int bg)
{
    if (x < 0 || y < 0 || x >= TERM_COLS || y >= TERM_ROWS)
        return;
    want[y][x].ch = (uint8_t)c & 0x7Fu;
    want[y][x].col = (uint8_t)(fg | bg << 4);
}

void term_put(int x, int y, const char *s, int fg, int bg)
{
    while (*s)
        term_putc(x++, y, *s++, fg, bg);
}

void term_num(int x, int y, int32_t v, int width, int fg, int bg)
{
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    int i = width;
    do {
        term_putc(x + --i, y, (char)('0' + u % 10u), fg, bg);
        u /= 10u;
    } while (u && i > 0);
    if (v < 0 && i > 0)
        term_putc(x + --i, y, '-', fg, bg);
    while (i > 0)
        term_putc(x + --i, y, ' ', fg, bg);
}

static void row_flush(int y)
{
    int x0 = 0, x1 = TERM_COLS - 1;
    if (!all_dirty) {
        while (x0 < TERM_COLS && want[y][x0].ch == shown[y][x0].ch &&
               want[y][x0].col == shown[y][x0].col)
            x0++;
        if (x0 == TERM_COLS)
            return;
        while (want[y][x1].ch == shown[y][x1].ch &&
               want[y][x1].col == shown[y][x1].col)
            x1--;
    }
    int w = (x1 - x0 + 1) * 8;
    for (int x = x0; x <= x1; x++) {
        Cell c = want[y][x];
        uint16_t fg = pal[c.col & 15u], bg = pal[c.col >> 4];
        const unsigned char *g = font8x8_basic[c.ch];
        for (int r = 0; r < 8; r++) {
            uint16_t *p = &strip[r * w + (x - x0) * 8];
            for (int b = 0; b < 8; b++)
                p[b] = (g[r] >> b) & 1u ? fg : bg;
        }
        shown[y][x] = c;
    }
    plat_blit(x0 * 8, y * 8, w, 8, strip);
}

void term_flush(void)
{
    for (int y = 0; y < TERM_ROWS; y++)
        row_flush(y);
    all_dirty = 0;
}
