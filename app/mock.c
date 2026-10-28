/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song, and nothing sounds; the data is copied from real songs and edited in RAM only.
 * PRESETS turns the variant.
 *
 * SONG, what a project opens on: its structure. One row a phrase, in the song's order, its
 * number always at the left; then the block each track plays (-- when silent), six tracks a
 * screen with their numbers small above. (Numbers were chosen over one square a track: they
 * say more.) The phrase playing is marked by a triangle.
 *
 *   SELECT  the phrase (the green row)
 *   KNOB 1  the track: a cell cursor inside the row, the grid scrolls sideways to follow it;
 *           left of the first track it lands on the phrase's number
 *   KNOB 2  the cell's block, down to -- (silent), up to one past the track's last (a new one)
 *   SEL     the list of the track's blocks, in a panel over the right three quarters:
 *           SELECT walks it, starting on the block in place; PLAY would play the one under
 *           the cursor alone, looped; SEL takes it; GLO goes back without changing anything.
 *           The two buttons are written at the bottom, on the side their buttons are on.
 *   SEL on the phrase's number: the SECTION, that phrase step by step.
 *
 * SECTION: the same grid, one row a step (its number at the left, white on the beat), one
 * column a track (five a screen: a note is three characters), the note each track plays on
 * that step (-- when none). SELECT the step,
 * KNOB 1 the track; SEL on a cell will open that track's block to edit (the next screen, not
 * drawn yet). GLO goes back to SONG, as it leaves the panel: GLO is "back" everywhere. KIDS
 * only: its blocks are in mock_kids.h.
 *
 * PATTERN (SEL on a SECTION cell, or SEQ on a SONG cell), a proposal: the block in three
 * views that FX turns through -- NOTES, FX, INS -- each the same grid: SELECT the step,
 * KNOB 1 the column, KNOB 2 would change it (KNOB 3 the effect's type), the keys would write
 * the note. GLO goes back where it came from. The block is always the same here. */
#include "../platform/platform.h"
#include "theme.h"
#include "mock_kids.h"

#define ROW 28
#define ROWS 8
#define TEXT_X 14
#define HEAD 16 /* the line of track numbers */
#define CELL_X 50
#define CELL_W 31
#define CELLS 6 /* tracks a screen */
#define NONE 0xFF
#define SEC_W 37 /* SECTION: a note is three characters, five tracks a screen */
#define SEC_CELLS 5
#define MAXP 32

#define POP_X 46 /* the panel: the phrase numbers stay visible at its left */
#define POP_ROWS 7
#define POP_TOP 18

typedef struct {
    const uint8_t (*blocks)[8]; /* per phrase, the block of each track, NONE when silent */
    int n, tracks, playing;     /* playing: a phrase, or -1 stopped */
} Variant;

/* KIDS.trk and robot-demo.trk, as their arrangements are */
static const uint8_t kids[][8] = {
    { 0x00, NONE, 0x00, NONE, NONE, NONE, NONE, 0x00 }, { 0x01, NONE, 0x00, 0x00, NONE, 0x00, NONE, 0x00 },
    { 0x02, NONE, 0x01, 0x01, 0x00, 0x01, 0x00, 0x01 }, { 0x02, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01 },
    { 0x02, 0x01, 0x02, 0x02, 0x01, 0x01, 0x01, 0x02 }, { 0x03, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x02 },
    { NONE, NONE, 0x00, 0x00, NONE, NONE, NONE, 0x00 }, { 0x02, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01 },
    { 0x03, 0x02, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01 }, { 0x02, 0x01, 0x02, 0x02, 0x01, 0x01, 0x01, 0x02 },
    { 0x03, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x02 }, { 0x02, 0x01, NONE, 0x03, 0x02, 0x01, 0x02, 0x03 },
    { NONE, 0x03, NONE, 0x04, 0x03, 0x01, 0x03, 0x04 }, { 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x02 },
    { 0x03, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x02 }, { 0x02, 0x02, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01 },
    { 0x04, NONE, 0x00, 0x00, NONE, 0x02, NONE, 0x00 }, { NONE, NONE, 0x00, NONE, NONE, NONE, NONE, 0x00 },
    { NONE, NONE, 0x03, NONE, NONE, NONE, NONE, 0x05 },
};
static const uint8_t robot[][8] = {
    { NONE, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { NONE, NONE, NONE, NONE, 0x00, 0x00 },
};

static const Variant variants[] = {
    { kids, 19, 8, -1 },  /* KIDS just opened */
    { kids, 19, 8, 5 },   /* KIDS playing */
    { robot, 4, 6, -1 },  /* a short song on six tracks: they all fit */
};
#define NVAR ((int)(sizeof variants / sizeof variants[0]))

static uint8_t grid[MAXP][8]; /* the variant's blocks, edited here */
static int nblocks[8];        /* blocks each track has, from the song: one more is "new" */
static int var, cursor, top, track, left;
static int pop, pop_cur, pop_top, pop_plays; /* the panel; its rows: -- then 00.. then a new one */
static int sec, sec_len, sec_cur, sec_top, sec_track, sec_left; /* the SECTION open, of `cursor` */
static int pat, pat_track, pat_view, pat_col, pat_cur, pat_top; /* PATTERN: 1 from SECTION, 2 from SONG */
static uint32_t prev_btns;

static void hex2(char *s, int v)
{
    s[0] = "0123456789ABCDEF"[v >> 4 & 15];
    s[1] = "0123456789ABCDEF"[v & 15];
    s[2] = 0;
}

static void triangle(int x, int y, uint16_t c) /* pointing right: what plays */
{
    for (int k = 0; k < 6; k++)
        gfx_fill(x + k, y + k, 1, 12 - 2 * k, c, 255);
}

static void draw_song(void)
{
    const Variant *v = &variants[var];
    char s[3];

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    for (int c = 0; c < CELLS && left + c < v->tracks; c++) { /* which tracks these are */
        s[0] = (char)('1' + left + c), s[1] = 0;
        gfx_text_mid(F_COND_12, CELL_X + c * CELL_W, CELL_W, HEAD - 3, s,
                     left + c == track ? C_TEXT : C_DIM);
    }
    for (int r = 0; r < ROWS && top + r < v->n; r++) {
        int i = top + r, y = HEAD + r * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int here = i == cursor, plays = i == v->playing;
        uint16_t ink = here ? C_ON_ACC : plays ? C_TITLE : C_TEXT;
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4 - (v->n > ROWS ? 6 : 0), ROW - 2, C_ACCENT, 255);
        if (plays)
            triangle(4, y + 8, here ? C_ON_ACC : C_TITLE);
        hex2(s, i);
        if (here && track < 0) /* the cell cursor on the phrase's number */
            gfx_slab(TEXT_X - 6, y + 3, CELL_X - 3 - TEXT_X, ROW - 6, C_BG, 255);
        gfx_text(F_COND_20, TEXT_X, base, s, here && track < 0 ? C_ACCENT : ink);
        for (int t = left; t < left + CELLS && t < v->tracks; t++) {
            int b = grid[i][t], x = CELL_X + (t - left) * CELL_W, cell = here && t == track;
            uint16_t c = cell ? C_ACCENT : b == NONE ? (here ? C_ON_ACC : C_DIM) : ink;
            if (cell) /* the cell cursor: hollowed out of the row */
                gfx_slab(x + 1, y + 3, CELL_W - 2, ROW - 6, C_BG, 255);
            if (b != NONE)
                hex2(s, b);
            gfx_text_mid(F_COND_20, x, CELL_W, base, b != NONE ? s : "--", c);
        }
    }
    gfx_fill(CELL_X - 3, HEAD, 1, ROWS * ROW, C_DIM, 255);
    if (v->n > ROWS) { /* where the screen is in the song */
        int h = ROWS * ROW * ROWS / v->n, y = HEAD + ROWS * ROW * top / v->n;
        gfx_fill(PLAT_W - 4, HEAD, 2, ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

/* Panel row k: -1 is --, then the blocks, then one new one. */
static int pop_rows(void) { return nblocks[track] + 2; }
static int pop_block(int k) { return k == 0 ? NONE : k - 1; }

static void hint(int x, const char *key, const char *word, int right)
{
    int w = gfx_text_w(F_COND_14, key) + 5 + gfx_text_w(F_COND_14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND_14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND_14, x + 5, PLAT_H - 6, word, C_DIM);
}

static void draw_pop(void)
{
    int n = pop_rows(), now = grid[cursor][track];
    char s[16];

    draw_song();
    gfx_fill(POP_X, 0, PLAT_W - POP_X, PLAT_H, C_BG, 255);
    gfx_fill(POP_X, 0, 1, PLAT_H, C_TEXT, 255);         /* over the grid: its edge */
    s[0] = 'T', s[1] = 'R', s[2] = 'A', s[3] = 'C', s[4] = 'K', s[5] = ' ';
    s[6] = (char)('1' + track), s[7] = 0;
    gfx_text(F_COND_12, POP_X + 10, 13, s, C_DIM);
    for (int r = 0; r < POP_ROWS && pop_top + r < n; r++) {
        int k = pop_top + r, b = pop_block(k), y = POP_TOP + r * ROW;
        int base = y + 2 + FONTS[F_COND_20].ascent, here = k == pop_cur, x;
        uint16_t ink = here ? C_ON_ACC : b == NONE || k == n - 1 ? C_DIM : C_TEXT;
        if (here)
            gfx_slab(POP_X + 4, y + 1, PLAT_W - POP_X - 8 - (n > POP_ROWS ? 6 : 0), ROW - 2, C_ACCENT, 255);
        if (b == now) /* the block in place, as HOME marks the open project */
            gfx_fill(POP_X + 6, y + 6, 3, ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
        if (k == pop_plays)
            triangle(PLAT_W - 24, y + 8, here ? C_ON_ACC : C_TITLE);
        if (b == NONE) {
            x = gfx_text(F_COND_20, POP_X + 14, base, "--", ink);
            gfx_text(F_COND_14, x + 8, base, "silent", ink);
        } else {
            hex2(s, b);
            x = gfx_text(F_COND_20, POP_X + 14, base, s, ink);
            if (k == n - 1)
                gfx_text(F_COND_14, x + 8, base, "new", ink);
        }
    }
    if (n > POP_ROWS) {
        int h = POP_ROWS * ROW * POP_ROWS / n, y = POP_TOP + POP_ROWS * ROW * pop_top / n;
        gfx_fill(PLAT_W - 4, POP_TOP, 2, POP_ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
    hint(POP_X + 10, "SEL", "OK", 0);
    hint(PLAT_W - 8, "GLO", "BACK", 1);
}

static void note_name(char *s, int n) /* MIDI note -> C-4, C#4 */
{
    static const char nm[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    s[0] = nm[n % 12 * 2], s[1] = nm[n % 12 * 2 + 1], s[2] = (char)('0' + n / 12 - 1), s[3] = 0;
}

static int sec_note(int step, int t) /* what track t plays on that step of the section */
{
    int b = grid[cursor][t], len;
    if (b == NONE || b >= 8 || !(len = kids_len[t][b]))
        return 0;
    return kids_note[t][b][step % len];
}

static void draw_section(void)
{
    char s[4];
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    for (int c = 0; c < SEC_CELLS && sec_left + c < 8; c++) {
        s[0] = (char)('1' + sec_left + c), s[1] = 0;
        gfx_text_mid(F_COND_12, CELL_X + c * SEC_W, SEC_W, HEAD - 3, s,
                     sec_left + c == sec_track ? C_TEXT : C_DIM);
    }
    hex2(s, cursor); /* whose steps these are */
    gfx_text(F_COND_12, TEXT_X - 6, HEAD - 3, "SEC", C_DIM);
    gfx_text(F_COND_12, TEXT_X + 14, HEAD - 3, s, C_TITLE);
    for (int r = 0; r < ROWS && sec_top + r < sec_len; r++) {
        int i = sec_top + r, y = HEAD + r * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int here = i == sec_cur;
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 10, ROW - 2, C_ACCENT, 255);
        hex2(s, i);
        gfx_text(F_COND_20, TEXT_X, base, s, here ? C_ON_ACC : i % 4 ? C_DIM : C_TEXT);
        for (int t = sec_left; t < sec_left + SEC_CELLS && t < 8; t++) {
            int n = sec_note(i, t), x = CELL_X + (t - sec_left) * SEC_W, cell = here && t == sec_track;
            uint16_t c = cell ? C_ACCENT : here ? C_ON_ACC : n ? C_TEXT : C_DIM;
            if (cell)
                gfx_slab(x + 1, y + 3, SEC_W - 2, ROW - 6, C_BG, 255);
            if (n)
                note_name(s, n);
            gfx_text_mid(F_COND_20, x, SEC_W, base, n ? s : "--", c);
        }
    }
    gfx_fill(CELL_X - 3, HEAD, 1, ROWS * ROW, C_DIM, 255);
    {   /* where the screen is in the section */
        int h = ROWS * ROW * ROWS / sec_len, y = HEAD + ROWS * ROW * sec_top / sec_len;
        gfx_fill(PLAT_W - 4, HEAD, 2, ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

/* ------------------------------------------------------------------ PATTERN --- */
/* Three views of the same steps, FX turning from one to the next: NOTES (the four notes of a
 * chord), FX (the three effects: KNOB 2 the value, KNOB 3 the type), INS (the instrument of
 * each step, by name; grey where the step keeps the track's own). */
enum { V_NOTES, V_FX, V_INS, NVIEWS };
static const char *const view_name[NVIEWS] = { "NOTES", "FX", "INS" };
static const int view_cols[NVIEWS] = { 4, 3, 1 };
static const int view_w[NVIEWS] = { 38, 60, 180 };
static const char *const fx_names[] = { "", "SLI", "VEL", "TRM", "GAT", "VOL", "WAV" };
static const char *const ins_names[] = { "GTRTREM", "GTRMUTE", "PBASS", "CLAVI" };
#define TRACK_INS 0 /* the track's own instrument */
typedef struct {
    uint8_t n[4], ins, fx[3], val[3]; /* n: MIDI, 0 none; ins 0xFF: the track's; fx: fx_names */
} MStep;
#define A4 69
#define CH1 { A4, A4 + 3, A4 + 7, 0 }      /* A-4 C-5 E-5 */
#define CH2 { A4 + 2, A4 + 7, A4 + 10, 0 } /* B-4 E-5 G-5 */
#define E_ { { 0 }, 0xFF, { 0 }, { 0 } }
#define S1 { CH1, 0xFF, { 0 }, { 0 } }
#define S2 { CH2, 0xFF, { 0 }, { 0 } }
/* the start of tremolo.trk's t0 06, with a few effects and instrument changes added to show */
static const MStep pat_steps[32] = {
    { CH1, 0xFF, { 1, 2, 3 }, { 0x00, 0x7F, 0x5A } }, E_, S1, S1, E_, S1, S1, E_,
    { CH1, 1, { 0 }, { 0 } }, S1, E_, S1, S1, E_, E_, E_,
    { CH2, 0xFF, { 5, 0, 6 }, { 0x60, 0, 0x02 } }, E_, S2, S2, E_, S2, S2, E_,
    { CH2, 3, { 3, 0, 0 }, { 0x40, 0, 0 } }, S2, E_, S2, S2, E_, E_, { { A4 }, 0xFF, { 4, 0, 0 }, { 0x06, 0, 0 } },
};

static void draw_pattern(void)
{
    int cols = view_cols[pat_view], w = view_w[pat_view];
    char s[12];
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    s[0] = 'T', s[1] = (char)('1' + pat_track), s[2] = 0;
    gfx_text(F_COND_12, gfx_text(F_COND_12, 4, HEAD - 3, s, C_TITLE) + 4, HEAD - 3, view_name[pat_view], C_TEXT);
    for (int c = 0; c < cols; c++) { /* the columns' names */
        const char *h = pat_view == V_INS ? "INSTRUMENT" : 0;
        if (!h) {
            s[0] = pat_view == V_FX ? 'F' : (char)('1' + c), s[1] = pat_view == V_FX ? 'X' : 0;
            s[2] = ' ', s[3] = (char)('1' + c), s[4] = 0;
            h = s;
        }
        gfx_text_mid(F_COND_12, CELL_X + c * w, w, HEAD - 3, h, c == pat_col ? C_TEXT : C_DIM);
    }
    for (int r = 0; r < ROWS && pat_top + r < 32; r++) {
        int i = pat_top + r, y = HEAD + r * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int here = i == pat_cur;
        const MStep *st = &pat_steps[i];
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 10, ROW - 2, C_ACCENT, 255);
        hex2(s, i);
        gfx_text(F_COND_20, TEXT_X, base, s, here ? C_ON_ACC : i % 4 ? C_DIM : C_TEXT);
        for (int c = 0; c < cols; c++) {
            int x = CELL_X + c * w, cell = here && c == pat_col;
            uint16_t ink = cell ? C_ACCENT : here ? C_ON_ACC : C_TEXT;
            uint16_t dim = cell ? C_ACCENT : here ? C_ON_ACC : C_DIM;
            if (cell)
                gfx_slab(x + 1, y + 3, w - 2, ROW - 6, C_BG, 255);
            if (pat_view == V_NOTES) {
                if (st->n[c])
                    note_name(s, st->n[c]);
                gfx_text_mid(F_COND_20, x, w, base, st->n[c] ? s : "---", st->n[c] ? ink : dim);
            } else if (pat_view == V_FX) {
                if (!st->fx[c]) {
                    gfx_text_mid(F_COND_20, x, w, base, "---", dim);
                } else {
                    int tw = gfx_text_w(F_COND_20, fx_names[st->fx[c]]) + 4 + gfx_text_w(F_COND_20, "00");
                    int xx = gfx_text(F_COND_20, x + (w - tw) / 2, base, fx_names[st->fx[c]],
                                      cell || here ? ink : C_FX);
                    hex2(s, st->val[c]);
                    gfx_text(F_COND_20, xx + 4, base, s, ink);
                }
            } else { /* the instrument, by name: grey where the step keeps the track's */
                int k = st->ins == 0xFF ? TRACK_INS : st->ins, xx;
                uint16_t c2 = st->ins == 0xFF ? dim : ink;
                if (!st->n[0]) {
                    gfx_text(F_COND_20, x + 10, base, "--", dim);
                    continue;
                }
                hex2(s, k);
                xx = gfx_text(F_COND_20, x + 10, base, s, c2);
                gfx_text(F_COND_20, xx + 10, base, ins_names[k], c2);
            }
        }
    }
    gfx_fill(CELL_X - 3, HEAD, 1, ROWS * ROW, C_DIM, 255);
    {
        int h = ROWS * ROW * ROWS / 32, y = HEAD + ROWS * ROW * pat_top / 32;
        gfx_fill(PLAT_W - 4, HEAD, 2, ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

static void open_pattern(int from_song, int t)
{
    pat = from_song ? 2 : 1;
    pat_track = t;
    pat_view = pat_col = pat_cur = pat_top = 0;
}

static void enter(int v)
{
    var = v;
    cursor = top = track = left = pop = sec = pat = 0;
    for (int t = 0; t < 8; t++)
        nblocks[t] = 0;
    for (int i = 0; i < variants[v].n; i++)
        for (int t = 0; t < 8; t++) {
            grid[i][t] = t < variants[v].tracks ? variants[v].blocks[i][t] : NONE;
            if (grid[i][t] != NONE && grid[i][t] + 1 > nblocks[t])
                nblocks[t] = grid[i][t] + 1;
        }
}

static void clamp_scroll(int *cur, int *first, int n, int shown)
{
    *cur = *cur < 0 ? 0 : *cur >= n ? n - 1 : *cur;
    if (*cur < *first)
        *first = *cur;
    if (*cur >= *first + shown)
        *first = *cur - shown + 1;
}

void app_init(void)
{
    enter(0);
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
    const Variant *v;
    uint32_t press = in->btns & ~prev_btns;
    prev_btns = in->btns;

    if (pat) {
        pat_cur += in->enc[PE_SELECT];
        clamp_scroll(&pat_cur, &pat_top, 32, ROWS);
        if (press & 1u << PB_FX) { /* NOTES -> FX -> INS -> NOTES */
            pat_view = (pat_view + 1) % NVIEWS;
            pat_col = 0;
        }
        pat_col += in->enc[PE_K1];
        pat_col = pat_col < 0 ? 0 : pat_col >= view_cols[pat_view] ? view_cols[pat_view] - 1 : pat_col;
        if (press & 1u << PB_GLO) { /* back where it was opened from */
            if (pat == 2)
                sec = 0;
            pat = 0;
        }
        gfx_frame(pat ? draw_pattern : sec ? draw_section : draw_song);
        return;
    }
    if (sec) {
        sec_cur += in->enc[PE_SELECT];
        clamp_scroll(&sec_cur, &sec_top, sec_len, ROWS);
        sec_track += in->enc[PE_K1];
        clamp_scroll(&sec_track, &sec_left, 8, SEC_CELLS);
        if (press & 1u << PB_GLO)
            sec = 0;
        if (press & 1u << PB_SEL) {
            open_pattern(0, sec_track);
            gfx_frame(draw_pattern);
            return;
        }
        gfx_frame(sec ? draw_section : draw_song);
        return;
    }
    if (pop) {
        pop_cur += in->enc[PE_SELECT];
        clamp_scroll(&pop_cur, &pop_top, pop_rows(), POP_ROWS);
        if (press & 1u << PB_PLAY)
            pop_plays = pop_plays == pop_cur ? -1 : pop_cur;
        if (press & 1u << PB_SEL) {
            int b = pop_block(pop_cur);
            grid[cursor][track] = (uint8_t)b;
            pop = 0;
        }
        if (press & 1u << PB_GLO)
            pop = 0;
        gfx_frame(pop ? draw_pop : draw_song);
        return;
    }

    if (in->enc[PE_PRESET])
        enter(((var + in->enc[PE_PRESET]) % NVAR + NVAR) % NVAR);
    v = &variants[var];
    cursor += in->enc[PE_SELECT];
    clamp_scroll(&cursor, &top, v->n, ROWS);
    track += in->enc[PE_K1];
    track = track < -1 ? -1 : track >= v->tracks ? v->tracks - 1 : track; /* -1: the number */
    {
        int t = track < 0 ? 0 : track;
        clamp_scroll(&t, &left, v->tracks, CELLS);
    }
    if (in->enc[PE_K2] && track >= 0) { /* -- below 00, one new block above the last */
        int b = grid[cursor][track] == NONE ? -1 : grid[cursor][track];
        b += in->enc[PE_K2];
        b = b < -1 ? -1 : b > nblocks[track] ? nblocks[track] : b;
        grid[cursor][track] = b < 0 ? NONE : (uint8_t)b;
    }
    if (press & 1u << PB_SEQ && track >= 0 && grid[cursor][track] != NONE) { /* straight to the block */
        open_pattern(1, track);
        gfx_frame(draw_pattern);
        return;
    }
    if (press & 1u << PB_SEL && track < 0 && v->blocks == kids) { /* into the SECTION */
        sec = 1;
        sec_len = 1;
        for (int t = 0; t < 8; t++) /* steps "auto": its longest block */
            if (grid[cursor][t] < 8 && kids_len[t][grid[cursor][t]] > sec_len)
                sec_len = kids_len[t][grid[cursor][t]];
        sec_cur = sec_top = sec_track = sec_left = 0;
    } else if (press & 1u << PB_SEL && track >= 0) {
        int now = grid[cursor][track];
        pop = 1;
        pop_cur = now == NONE ? 0 : now + 1;
        pop_top = 0;
        pop_plays = -1;
        clamp_scroll(&pop_cur, &pop_top, pop_rows(), POP_ROWS);
    }
    gfx_frame(sec ? draw_section : pop ? draw_pop : draw_song);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
