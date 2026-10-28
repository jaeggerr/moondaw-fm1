/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song, and nothing sounds. (HOME, SONG, the SECTION, PATTERN, the MIXER and the PROJECT,
 * the mockups before this one, are the app's.)
 *
 * INSTRUMENT, a proposal: the settings that go together on one row of up to four columns,
 * and the column names change with the row (the MIXER's returns row did it first). NAME,
 * SOUND (engine, timbre), ENV (attack decay sustain release), FILTER (cutoff resonance
 * envelope velocity), LEVEL (gain, level), then the layers, one row each (LYR: its sound,
 * semitones, fine tune, gain), and a "+ LYR" row to add one. A column is one cell or two
 * (an engine's name is long). A layer shows its timbre alone: SAW can only be WAVE,
 * 25% only PULSE. PLAY plays a short preview of the instrument, marked by the triangle.
 * The instrument is KIDS's CHORDS, with one layer added to show one. */
#include "../platform/platform.h"
#include "theme.h"

#define ROW 28
#define ROWS 8
#define HEAD 16
#define LABEL_X 8
#define CELL_X 66
#define CELL_W 33 /* five cells a row; a column takes one or two */

typedef struct {
    const char *label;
    const char *head[4];
    const char *val[4];
    uint8_t span[4]; /* cells each column takes; 0 ends the row */
} Line;

static const Line lines[] = {
    { "NAME", { 0 }, { "CHORDS" }, { 0 } },
    { "SOUND", { "ENGINE", "TIMBRE" }, { "PULSE", "25%" }, { 2, 2 } },
    { "ENV", { "ATK", "DEC", "SUS", "REL" }, { "8", "60", "170", "40" }, { 1, 1, 1, 1 } },
    { "FILTER", { "CUT", "RES", "ENV", "VEL" }, { "168", "40", "72", "64" }, { 1, 1, 1, 1 } },
    { "LEVEL", { "GAIN", "LEVEL" }, { "-4", "0" }, { 1, 1 } },
    { "LYR 1", { "SOUND", "SEMI", "FINE", "GAIN" }, { "SAW", "+12", "+5", "-6" }, { 2, 1, 1, 1 } },
    { "+ LYR", { 0 }, { 0 }, { 0 } },
};

static int col_x(int r, int c) /* where column c of row r starts */
{
    int x = CELL_X;
    for (int k = 0; k < c; k++)
        x += lines[r].span[k] * CELL_W;
    return x;
}
#define NLINES ((int)(sizeof lines / sizeof lines[0]))

static int row, col, top, previewing;
static uint32_t prev_btns;

static int cols_of(int r)
{
    int n = 0;
    while (n < 4 && lines[r].head[n])
        n++;
    return n;
}

static void draw(void)
{
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    {
        int x = gfx_text(F_COND_12, 4, HEAD - 3, "INS", C_DIM);
        gfx_text(F_COND_12, x + 4, HEAD - 3, "09", C_TITLE);
    }
    for (int c = 0; c < cols_of(row); c++) /* the column names: this row's */
        gfx_text_mid(F_COND_12, col_x(row, c), lines[row].span[c] * CELL_W, HEAD - 3, lines[row].head[c],
                     c == col ? C_TEXT : C_DIM);
    for (int v = 0; v < ROWS && top + v < NLINES; v++) {
        int r = top + v, y = HEAD + v * ROW, base = y + 2 + FONTS[F_COND_20].ascent, band = r == row;
        const Line *l = &lines[r];
        if (band)
            gfx_slab(2, y + 1, PLAT_W - 4, ROW - 2, C_ACCENT, 255);
        if (r == 0 && previewing) /* what plays */
            for (int k = 0; k < 6; k++)
                gfx_fill(PLAT_W - 16 + k, y + 8 + k, 1, 12 - 2 * k, band ? C_ON_ACC : C_TITLE, 255);
        gfx_text(F_COND_20, LABEL_X, base, l->label, band ? C_ON_ACC : r == NLINES - 1 ? C_DIM : C_DIM);
        if (!l->head[0]) { /* one value over the whole row: the name */
            if (l->val[0]) {
                if (band)
                    gfx_slab(CELL_X - 4, y + 3, 130, ROW - 6, C_BG, 255);
                gfx_text(F_COND_20, CELL_X, base, l->val[0], band ? C_ACCENT : C_TEXT);
            }
            continue;
        }
        for (int c = 0; c < 4 && l->head[c]; c++) {
            int x = col_x(r, c), w = l->span[c] * CELL_W, on = band && c == col;
            if (on)
                gfx_slab(x + 1, y + 3, w - 2, ROW - 6, C_BG, 255);
            gfx_text_mid(F_COND_20, x, w, base, l->val[c], on ? C_ACCENT : band ? C_ON_ACC : C_TEXT);
        }
    }
    gfx_fill(CELL_X - 5, HEAD, 1, ROWS * ROW > (NLINES - top) * ROW ? (NLINES - top) * ROW : ROWS * ROW, C_DIM, 255);
}

void app_init(void)
{
    row = col = top = previewing = 0;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

int app_load(const char *trk, uint32_t len, const char *name)
{
    (void)trk, (void)len, (void)name;
    return 0;
}

void app_frame(const PlatInput *in)
{
    uint32_t press = in->btns & ~prev_btns;
    int n;
    prev_btns = in->btns;
    row += in->enc[PE_SELECT];
    row = row < 0 ? 0 : row >= NLINES ? NLINES - 1 : row;
    n = cols_of(row);
    col += in->enc[PE_K1];
    col = col < 0 ? 0 : n && col >= n ? n - 1 : col;
    if (!n)
        col = 0;
    if (row < top)
        top = row;
    if (row >= top + ROWS)
        top = row - ROWS + 1;
    if (press & 1u << PB_PLAY)
        previewing = !previewing;
    gfx_frame(draw);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
