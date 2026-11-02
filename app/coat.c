/* SPDX-License-Identifier: GPL-3.0-only */
#include "coat.h"

#include "fonts.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"

bool coat_on;

int coat_rows(void) { return coat_on ? COAT_ROWS : 0; }

enum {
    COLS = 6,
    CELL_W = PLAT_W / COLS,              /* 40 */
    TOP = PLAT_H - COAT_ROWS * GRID_ROW, /* 184 */
    NAME_Y = 10,                         /* baselines inside a row of 28 */
    WORD_Y = 24,
};

/* The black keys EDIT gives a command to, in the order they lie under the
 * fingers (app/ui.h). Six of them fill a row of the foot exactly, which
 * is why MONO sits alone on the second: it is far up the keyboard, and a
 * cell that lies about where a key is would be worse than none. */
static const char *const op_name[COLS] = { "OP1", "OP2", "OP3", "OP4", "OP5", "OP6" };
static const char *const op_word[COLS] = { "COPY", "PASTE", "CUT", "CLEAR", "SEL", "LOOP" };

/* Centred in its cell, and never into the next one. A word too wide at
 * CONDENSED 12 is tried at 11 before it is given up -- PATTERN and PROJECT
 * are both over by a hair, and a cyan name with nothing under it is the
 * worst cell of all: it says a button does something and will not say
 * what. What it still must not do is SHORTEN the word (coat.h): past 11
 * the cell goes dark name and all, which at least tells the truth. */
static void cell(int c, int r, const char *name, const char *word, uint16_t ink)
{
    static const int sizes[] = { F_COND12, F_COND11 };
    int x = c * CELL_W, y = TOP + r * GRID_ROW, w;
    if (word) {
        for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            w = gfx_text_w(sizes[i], word);
            if (w <= CELL_W - 4) { /* 2 px clear of the rule either side */
                gfx_text(sizes[i], x + (CELL_W - w) / 2, y + WORD_Y, word, C_TEXT);
                break;
            }
            if (i + 1 == sizeof sizes / sizeof sizes[0])
                return; /* no name either: the cell says nothing rather than half */
        }
    }
    w = gfx_text_w(F_COND9, name);
    gfx_text(F_COND9, x + (CELL_W - w) / 2, y + NAME_Y, name, ink);
}

static void draw_keys(void)
{
    for (int c = 0; c < COLS; c++)
        cell(c, 0, op_name[c], op_word[c], C_ACCENT);
    cell(0, 1, "SELECT", "SPEED", C_ACCENT); /* EDIT + SELECT, on every screen (app/ui.h) */
    cell(COLS - 1, 1, "MONO", "SOLO", C_ACCENT);
}

void coat_draw(const char *const *words, bool edit_held)
{
    static const char *const btn[COAT_BTNS] = { "FX",   "SEL",  "ENV", "LFO", "EDIT", "GLO",
                                                "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
    if (!coat_on)
        return;
    gfx_fill(0, TOP, PLAT_W, PLAT_H - TOP, C_BG, 255);
    gfx_fill(0, TOP, PLAT_W, 1, C_TRACK, 255);
    /* The twelve read as twelve: without a rule between them PATTERN and
     * SECTION, side by side and both at the edge of their cell, run into
     * one word (seen on the panel, 02/11). */
    for (int c = 1; c < COLS; c++)
        gfx_fill(c * CELL_W, TOP + 2, 1, PLAT_H - TOP - 4, C_TRACK, 255);
    if (edit_held) {
        draw_keys();
        return;
    }
    for (int b = 0; b < COAT_BTNS; b++)
        cell(b % COLS, b / COLS, btn[b], words[b], words[b] ? C_TITLE : C_TEXT_FAINT);
}
