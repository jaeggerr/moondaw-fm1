/* SPDX-License-Identifier: GPL-3.0-only */
#include "namer.h"

#include "grid.h"
#include "theme.h"

enum { MAX = 16, CELL = 18, Y = 90 };

static const char letters[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
static char draft[MAX + 1], *target;
static int len, at;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int letter_index(char c)
{
    for (int i = 0; letters[i]; i++)
        if (letters[i] == c)
            return i;
    return 0;
}

void namer_open(char *t, int n)
{
    int end = 0;
    target = t;
    len = n > MAX ? MAX : n;
    while (end < len && t[end])
        end++;
    for (int j = 0; j <= MAX; j++)
        draft[j] = j < end ? t[j] : 0;
    at = 0;
}

/* The name as typed: the spaces at its end are not kept, and an empty
 * one changes nothing. */
static void take(void)
{
    int end = len;
    while (end > 0 && (!draft[end - 1] || draft[end - 1] == ' '))
        end--;
    if (!end)
        return;
    for (int j = 0; j < end; j++)
        target[j] = draft[j];
    target[end] = 0;
}

int namer_frame(const PlatInput *in, uint32_t pressed)
{
    int n = (int)sizeof letters - 1;
    at = clampi(at + in->enc[PE_K1], 0, len - 1);
    if (in->enc[PE_K3]) {
        int i = (letter_index(draft[at] ? draft[at] : ' ') + in->enc[PE_K3]) % n;
        for (int j = 0; j < at; j++) /* a letter after the end pulls spaces in before it */
            if (!draft[j])
                draft[j] = ' ';
        draft[at] = letters[i < 0 ? i + n : i];
    }
    if (pressed & 1u << PB_SEL) {
        take();
        return NAMER_KEPT;
    }
    return pressed & 1u << PB_GLO ? NAMER_LEFT : NAMER_TYPING;
}

static void hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 6, word, C_DIM);
}

void namer_draw(void)
{
    int x0 = (PLAT_W - len * CELL) / 2;
    gfx_text_mid(F_COND12, 0, PLAT_W, 60, "NAME", C_DIM);
    for (int i = 0; i < len; i++) {
        char s[2] = { draft[i] ? draft[i] : ' ', 0 };
        int x = x0 + i * CELL;
        bool here = i == at;
        if (here)
            gfx_slab(x + 1, Y, CELL - 2, GRID_ROW, C_ACCENT, 255);
        else
            gfx_fill(x + 3, Y + GRID_ROW - 2, CELL - 6, 1, C_DIM, 255); /* where a letter goes */
        gfx_text_mid(F_COND20, x, CELL, Y + 2 + FONTS[F_COND20].ascent, s, here ? C_ON_ACC : C_TEXT);
    }
    hint(14, "SEL", "OK", false);
    hint(PLAT_W - 14, "GLO", "BACK", true);
}
