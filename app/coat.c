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

/* The black keys, laid out AS THEY LIE: a row of the foot is one octave,
 * three together then two, so the eye finds a key by its shape in the
 * group and not by counting (his design, 02/11). Slots 0-2 are F# G# A#,
 * 3-4 are C# D#; row 0 is the first octave, row 1 the second.
 *
 * All ten are drawn, PIT, GLO and POLY included: they are printed on the
 * panel (NOTES.md 29/10) and command nothing, so they say "--". A gap
 * would read as a missing key, which is worse than a key saying it is
 * mute. F#5, the eleventh, opens a third octave no OP lives in, and is
 * left out. */
enum { KEYS_COLS = 5, KEY_W = 44, KEY_GAP_AT = 152 };
static const char *const key_name[2][KEYS_COLS] = { { "OP1", "OP2", "OP3", "OP4", "OP5" },
                                                    { "OP6", "PIT", "GLO", "MONO", "POLY" } };
static const char *const key_word[2][KEYS_COLS] = { { "COPY", "PASTE", "CUT", "CLEAR", "SEL" },
                                                    { "LOOP", 0, 0, "SOLO", 0 } };
static int key_x(int s) { return s < 3 ? s * KEY_W : KEY_GAP_AT + (s - 3) * KEY_W; }

/* Centred in its cell, and never into the next one. A word too wide at
 * CONDENSED 12 drops to 11, then to 9 -- the name's own size -- before it
 * is given up: SYLLABLE on the effects does not fit at any ordinary size,
 * and small is better than absent. What it must never do is SHORTEN the
 * word (coat.h); an abbreviation nobody reads is worse than nothing.
 *
 * The NAME is always drawn, whatever became of the word. A blank cell
 * means "this button does nothing here", so leaving one blank while the
 * button is live would be a lie -- the first build did exactly that to
 * SEL on the effects, and the cell read as dead (seen on the panel,
 * 03/11). A live name with no word under it says the honest thing: there
 * is something here and the foot cannot say what. */
static void cell_at(int x, int cw, int r, const char *name, const char *word, uint16_t ink, uint16_t wink)
{
    static const int sizes[] = { F_COND12, F_COND11, F_COND9 };
    int y = TOP + r * GRID_ROW, w;
    for (unsigned i = 0; word && i < sizeof sizes / sizeof sizes[0]; i++) {
        w = gfx_text_w(sizes[i], word);
        if (w <= cw - 4) { /* 2 px clear of the rule either side */
            gfx_text(sizes[i], x + (cw - w) / 2, y + WORD_Y, word, wink);
            break;
        }
    }
    w = gfx_text_w(F_COND9, name);
    gfx_text(F_COND9, x + (cw - w) / 2, y + NAME_Y, name, ink);
}

static void cell(int c, int r, const char *name, const char *word, uint16_t ink)
{
    cell_at(c * CELL_W, CELL_W, r, name, word, ink, C_TEXT);
}

static void draw_keys(void)
{
    for (int r = 0; r < 2; r++)
        for (int s = 0; s < KEYS_COLS; s++) {
            const char *word = key_word[r][s];
            cell_at(key_x(s), KEY_W, r, key_name[r][s], word ? word : "--",
                    word ? C_ACCENT : C_TEXT_FAINT, word ? C_TEXT : C_DIM);
        }
}

void coat_draw(const char *const *words, bool edit_held)
{
    static const char *const btn[COAT_BTNS] = { "FX",   "SEL",  "ENV", "LFO", "EDIT", "GLO",
                                                "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
    if (!coat_on)
        return;
    gfx_fill(0, TOP, PLAT_W, PLAT_H - TOP, C_BG, 255);
    gfx_fill(0, TOP, PLAT_W, 1, C_TRACK, 255);
    if (edit_held) { /* the keys carry their own grouping: a rule would fight it */
        draw_keys();
        return;
    }
    /* The twelve read as twelve: without a rule between them PATTERN and
     * SECTION, side by side and both at the edge of their cell, run into
     * one word (seen on the panel, 02/11). */
    for (int c = 1; c < COLS; c++)
        gfx_fill(c * CELL_W, TOP + 2, 1, PLAT_H - TOP - 4, C_TRACK, 255);
    for (int b = 0; b < COAT_BTNS; b++)
        cell(b % COLS, b / COLS, btn[b], words[b], words[b] ? C_TITLE : C_TEXT_FAINT);
}
