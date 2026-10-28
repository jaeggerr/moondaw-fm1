/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song, and nothing sounds; the data is copied from a real song and edited in RAM only.
 * (HOME, SONG, the SECTION and PATTERN, the mockups before this one, are now the app's.)
 *
 * MIXER, reached by FX: the grid once more. One row a track, its number at the left; four
 * columns, VOL PAN REV ECH, the values as the file writes them. Above track 1, hidden until
 * SELECT goes up to it, the row of the two global effects: ROOM DAMP (the reverb), TIME FB
 * (the delay); the column names change with it.
 *
 *   SELECT  the row       KNOB 1  the column; left of the first, the track's number
 *   KNOB 2  the value; on a track's number: normal -> MUTE -> SOLO
 * A muted track is grey, its number red; a soloed one has its number in cyan. */
#include "../platform/platform.h"
#include "theme.h"

#define ROW 28
#define ROWS 8
#define HEAD 16
#define NUM_X 14
#define CELL_X 50
#define CELL_W 45
#define COLS 4

/* KIDS.trk: trk lines, then reverb 76 98 and delay 18 52 */
static int mix[8][COLS] = {
    { 51, 0, 0, 0 },    { 44, 0, 12, 0 },    { 26, -18, 30, 38 }, { 28, 18, 22, 22 },
    { 52, 0, 0, 0 },    { 25, -34, 8, 0 },   { 27, 30, 22, 0 },   { 22, 14, 54, 18 },
};
static int fx[COLS] = { 76, 98, 18, 52 };
static int state[8];          /* 0 plays, 1 muted, 2 solo */
static int row, col, top;     /* row -1: the effects; col -1: the number */
static uint32_t prev_btns;

static char *dec(char *s, int v)
{
    char d[6];
    int k = 0, neg = v < 0;
    if (neg)
        v = -v;
    do
        d[k++] = (char)('0' + v % 10);
    while ((v /= 10) > 0);
    if (neg)
        *s++ = '-';
    while (k)
        *s++ = d[--k];
    *s = 0;
    return s;
}

static int any_solo(void)
{
    for (int t = 0; t < 8; t++)
        if (state[t] == 2)
            return 1;
    return 0;
}

static void draw(void)
{
    static const char *const track_head[COLS] = { "VOL", "PAN", "REV", "ECH" };
    static const char *const fx_head[COLS] = { "ROOM", "DAMP", "TIME", "FB" };
    char s[8];

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND_12, 4, HEAD - 3, "MIXER", C_DIM);
    for (int c = 0; c < COLS; c++)
        gfx_text_mid(F_COND_12, CELL_X + c * CELL_W, CELL_W, HEAD - 3,
                     row < 0 ? fx_head[c] : track_head[c], c == col ? C_TEXT : C_DIM);
    for (int v = 0; v < ROWS && top + v < 9; v++) {
        int r = top + v - 1, y = HEAD + v * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int band = r == row, silent = r >= 0 && (state[r] == 1 || (any_solo() && state[r] != 2));
        uint16_t num_ink, ink;
        if (band)
            gfx_slab(2, y + 1, PLAT_W - 10, ROW - 2, C_ACCENT, 255);
        if (band && col < 0)
            gfx_slab(NUM_X - 6, y + 3, CELL_X - NUM_X + 1, ROW - 6, C_BG, 255);
        num_ink = r < 0 ? C_TITLE : state[r] == 1 ? C_MUTE : state[r] == 2 ? C_TITLE : C_TEXT;
        num_ink = band && col < 0 ? C_ACCENT : band ? C_ON_ACC : num_ink;
        if (r < 0) {
            gfx_text(F_COND_20, NUM_X, base, "FX", num_ink);
        } else {
            int x;
            s[0] = (char)('1' + r), s[1] = 0;
            x = gfx_text(F_COND_20, NUM_X, base, s, num_ink);
            if (state[r])
                gfx_text(F_COND_14, x + 5, base, state[r] == 1 ? "M" : "S", num_ink);
        }
        for (int c = 0; c < COLS; c++) {
            int x = CELL_X + c * CELL_W, on = band && c == col;
            int val = r < 0 ? fx[c] : mix[r][c];
            if (r >= 0 && c == 1) { /* pan: C, L18, R18 */
                s[0] = val < 0 ? 'L' : val > 0 ? 'R' : 'C', s[1] = 0;
                if (val)
                    dec(s + 1, val < 0 ? -val : val);
            } else {
                dec(s, val);
            }
            ink = on ? C_ACCENT : band ? C_ON_ACC : silent ? C_DIM
                  : (r >= 0 && c >= 2 && val == 0) ? C_DIM : C_TEXT;
            if (on)
                gfx_slab(x + 1, y + 3, CELL_W - 2, ROW - 6, C_BG, 255);
            gfx_text_mid(F_COND_20, x, CELL_W, base, s, ink);
        }
    }
    gfx_fill(CELL_X - 3, HEAD, 1, ROWS * ROW, C_DIM, 255);
    {   /* nine rows: one hides */
        int h = ROWS * ROW * ROWS / 9, y = HEAD + ROWS * ROW * top / 9;
        gfx_fill(PLAT_W - 4, HEAD, 2, ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

void app_init(void)
{
    row = col = 0;
    top = 1; /* the effects' row is above, out of sight */
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
    static const int lo[COLS] = { 0, -64, 0, 0 }, hi[COLS] = { 127, 63, 127, 127 };
    int d = in->enc[PE_K2];
    prev_btns = in->btns;

    row += in->enc[PE_SELECT];
    row = row < -1 ? -1 : row > 7 ? 7 : row;
    col += in->enc[PE_K1];
    col = col < -1 ? -1 : col >= COLS ? COLS - 1 : col;
    if (row + 1 < top)
        top = row + 1;
    if (row + 1 >= top + ROWS)
        top = row + 1 - ROWS + 1;
    if (d && col < 0 && row >= 0) {
        state[row] += d;
        state[row] = state[row] < 0 ? 0 : state[row] > 2 ? 2 : state[row];
    } else if (d && col >= 0) {
        int *p = row < 0 ? &fx[col] : &mix[row][col];
        int l = row < 0 ? 0 : lo[col], h = row < 0 ? 127 : hi[col];
        *p += d;
        *p = *p < l ? l : *p > h ? h : *p;
    }
    gfx_frame(draw);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
