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
 * (HOME, the first mockup, is now app/menu.c.) */
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

static void enter(int v)
{
    var = v;
    cursor = top = track = left = pop = sec = 0;
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

    if (sec) {
        sec_cur += in->enc[PE_SELECT];
        clamp_scroll(&sec_cur, &sec_top, sec_len, ROWS);
        sec_track += in->enc[PE_K1];
        clamp_scroll(&sec_track, &sec_left, 8, SEC_CELLS);
        if (press & 1u << PB_GLO)
            sec = 0;
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
