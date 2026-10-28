/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song, and nothing sounds; the data is KIDS's, edited in RAM only.
 * (HOME, SONG, the SECTION, PATTERN and the MIXER, the mockups before this one, are decided.)
 *
 * PROJECT, reached by EDIT: what holds for the whole song, one setting a row, the value in
 * the right column. SELECT the setting, KNOB 2 its value.
 *   NAME    SEL opens the name: one cell a letter, KNOB 1 the place, KNOB 2 the letter
 *           (space, A-Z, 0-9, -), SEL keeps it, GLO goes back without it
 *   BPM     20..400            SWING  50..75 %
 *   METER   beats in a bar: 2/4 3/4 4/4 ... (the file's beats_per_bar)
 *   STEP    what a step is worth: 1/16 at 4 steps a beat, 1/12 at 3 (triplets), 1/8 at 2
 *           (the file's steps_per_beat)
 *   TICKS   1..24 a step       TRACKS  1..8 */
#include "../platform/platform.h"
#include "theme.h"

#define ROW 28
#define HEAD 16
#define LABEL_X 14
#define VALUE_X 120
#define NAME_LEN 12

enum { S_NAME, S_BPM, S_SWING, S_METER, S_STEP, S_TICKS, S_TRACKS, SETTINGS };
static const char *const label[SETTINGS] = { "NAME", "BPM", "SWING", "METER", "STEP", "TICKS", "TRACKS" };
static const int lo[SETTINGS] = { 0, 20, 50, 1, 1, 1, 1 }, hi[SETTINGS] = { 0, 400, 75, 16, 8, 24, 8 };
static int val[SETTINGS] = { 0, 120, 50, 4, 4, 6, 8 };
static char name[NAME_LEN + 1] = "KIDS";
static char draft[NAME_LEN + 1];
static int row, naming, at;
static uint32_t prev_btns;

static const char letters[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";

static char *dec(char *s, int v)
{
    char d[6];
    int k = 0;
    do
        d[k++] = (char)('0' + v % 10);
    while ((v /= 10) > 0);
    while (k)
        *s++ = d[--k];
    *s = 0;
    return s;
}

static void value_of(int k, char *s)
{
    char *p;
    switch (k) {
    case S_NAME:
        for (p = name; *p; p++)
            *s++ = *p;
        *s = 0;
        break;
    case S_SWING:
        p = dec(s, val[k]);
        p[0] = ' ', p[1] = '%', p[2] = 0;
        break;
    case S_METER: /* beats in a bar, over the quarter */
        p = dec(s, val[k]);
        p[0] = '/', p[1] = '4', p[2] = 0;
        break;
    case S_STEP: /* a step is 1 / (4 x steps a beat) of a whole note */
        s[0] = '1', s[1] = '/';
        dec(s + 2, 4 * val[k]);
        break;
    default:
        dec(s, val[k]);
    }
}

static void hint(int x, const char *key, const char *word, int right)
{
    int w = gfx_text_w(F_COND_14, key) + 5 + gfx_text_w(F_COND_14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND_14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND_14, x + 5, PLAT_H - 6, word, C_DIM);
}

static void draw_name(void)
{
    enum { CELL = 18, X0 = (PLAT_W - NAME_LEN * CELL) / 2, Y = 90 };
    gfx_text_mid(F_COND_12, 0, PLAT_W, 60, "NAME", C_DIM);
    for (int i = 0; i < NAME_LEN; i++) {
        char s[2] = { draft[i] ? draft[i] : ' ', 0 };
        int x = X0 + i * CELL, here = i == at;
        if (here)
            gfx_slab(x + 1, Y, CELL - 2, ROW, C_ACCENT, 255);
        else
            gfx_fill(x + 3, Y + ROW - 2, CELL - 6, 1, C_DIM, 255); /* where a letter goes */
        gfx_text_mid(F_COND_20, x, CELL, Y + 2 + FONTS[F_COND_20].ascent, s, here ? C_ON_ACC : C_TEXT);
    }
    hint(14, "SEL", "OK", 0);
    hint(PLAT_W - 14, "GLO", "BACK", 1);
}

static void draw(void)
{
    char s[NAME_LEN + 4];
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND_12, 4, HEAD - 3, "PROJECT", C_DIM);
    if (naming) {
        draw_name();
        return;
    }
    for (int k = 0; k < SETTINGS; k++) {
        int y = HEAD + k * ROW, base = y + 2 + FONTS[F_COND_20].ascent, here = k == row;
        if (here) {
            gfx_slab(2, y + 1, PLAT_W - 4, ROW - 2, C_ACCENT, 255);
            gfx_slab(VALUE_X - 6, y + 3, PLAT_W - VALUE_X - 2, ROW - 6, C_BG, 255);
        }
        gfx_text(F_COND_20, LABEL_X, base, label[k], here ? C_ON_ACC : C_DIM);
        value_of(k, s);
        gfx_text(F_COND_20, VALUE_X, base, s, here ? C_ACCENT : C_TEXT);
    }
}

void app_init(void)
{
    row = naming = 0;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

int app_load(const char *trk, uint32_t len, const char *name_)
{
    (void)trk, (void)len, (void)name_;
    return 0;
}

static int letter_index(char c)
{
    for (int i = 0; letters[i]; i++)
        if (letters[i] == c)
            return i;
    return 0;
}

void app_frame(const PlatInput *in)
{
    uint32_t press = in->btns & ~prev_btns;
    prev_btns = in->btns;

    if (naming) {
        int n = (int)sizeof letters - 1;
        at += in->enc[PE_K1];
        at = at < 0 ? 0 : at >= NAME_LEN ? NAME_LEN - 1 : at;
        if (in->enc[PE_K2]) {
            int i = (letter_index(draft[at] ? draft[at] : ' ') + in->enc[PE_K2]) % n;
            for (int j = 0; j < at; j++) /* a letter after the end pulls spaces in before it */
                if (!draft[j])
                    draft[j] = ' ';
            draft[at] = letters[i < 0 ? i + n : i];
        }
        if (press & 1u << PB_SEL) { /* the spaces at the end are not kept */
            int end = NAME_LEN;
            while (end > 0 && (!draft[end - 1] || draft[end - 1] == ' '))
                end--;
            if (end > 0) {
                for (int j = 0; j < end; j++)
                    name[j] = draft[j];
                name[end] = 0;
            }
            naming = 0;
        }
        if (press & 1u << PB_GLO)
            naming = 0;
        gfx_frame(draw);
        return;
    }

    row += in->enc[PE_SELECT];
    row = row < 0 ? 0 : row >= SETTINGS ? SETTINGS - 1 : row;
    if (in->enc[PE_K2] && row != S_NAME) {
        val[row] += in->enc[PE_K2];
        val[row] = val[row] < lo[row] ? lo[row] : val[row] > hi[row] ? hi[row] : val[row];
    }
    if (press & 1u << PB_SEL && row == S_NAME) {
        int end = 0;
        while (name[end])
            end++;
        for (int j = 0; j <= NAME_LEN; j++)
            draft[j] = j < end ? name[j] : 0;
        at = 0;
        naming = 1;
    }
    gfx_frame(draw);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
