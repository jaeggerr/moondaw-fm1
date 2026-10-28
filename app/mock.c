/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song; the data is copied from real ones. PRESETS turns the variant, SELECT moves the cursor,
 * KNOB 1 scrolls the tracks sideways when they do not all fit.
 *
 * SONG, what a project opens on: its structure. One row a phrase, in the song's order, its
 * number always at the left. Then the tracks, two ways to compare:
 *   SQUARES  one square a track, filled when the track plays in that phrase;
 *   NUMBERS  the block each track plays (-- when silent), six tracks a screen, the track
 *            numbers small above them, KNOB 1 to see the others.
 * The cursor starts on the first phrase; the phrase playing is marked by a triangle.
 * (HOME, the first mockup, is now app/menu.c.) */
#include "../platform/platform.h"
#include "theme.h"

#define ROW 28
#define ROWS 8
#define TEXT_X 14
#define SQ 14 /* SQUARES: a track's square */
#define SQ_GAP 5
#define SQ_X 54
#define HEAD 16 /* NUMBERS: the line of track numbers */
#define CELL_X 50
#define CELL_W 31
#define CELLS 6 /* tracks a screen */
#define NONE 0xFF

typedef struct {
    const uint8_t (*blocks)[8]; /* per phrase, the block of each track, NONE when silent */
    int n, tracks, playing;     /* playing: a phrase, or -1 stopped */
    int numbers;                /* 0 SQUARES, 1 NUMBERS */
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
    { kids, 19, 8, -1, 0 },  /* KIDS just opened, SQUARES */
    { kids, 19, 8, -1, 1 },  /* the same, NUMBERS */
    { kids, 19, 8, 5, 1 },   /* KIDS playing, NUMBERS */
    { robot, 4, 6, -1, 1 },  /* a short song on six tracks: they all fit */
};
#define NVAR ((int)(sizeof variants / sizeof variants[0]))

static int var, cursor, top, left; /* left: the first track shown, NUMBERS */

static void hex2(char *s, int v)
{
    s[0] = "0123456789ABCDEF"[v >> 4 & 15];
    s[1] = "0123456789ABCDEF"[v & 15];
    s[2] = 0;
}

static void draw_song(void)
{
    const Variant *v = &variants[var];
    int y0 = v->numbers ? HEAD : 0, rows = ROWS;
    char s[3];

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    if (v->numbers) /* which tracks these are */
        for (int c = 0; c < CELLS && left + c < v->tracks; c++) {
            s[0] = (char)('1' + left + c), s[1] = 0;
            gfx_text_mid(F_COND_12, CELL_X + c * CELL_W, CELL_W, HEAD - 3, s, C_DIM);
        }
    for (int r = 0; r < rows && top + r < v->n; r++) {
        int i = top + r, y = y0 + r * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int here = i == cursor, plays = i == v->playing;
        uint16_t ink = here ? C_ON_ACC : plays ? C_TITLE : C_TEXT;
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4 - (v->n > rows ? 6 : 0), ROW - 2, C_ACCENT, 255);
        if (plays) /* a triangle pointing right: what plays */
            for (int k = 0; k < 6; k++)
                gfx_fill(4 + k, y + 8 + k, 1, 12 - 2 * k, here ? C_ON_ACC : C_TITLE, 255);
        hex2(s, i);
        gfx_text(F_COND_20, TEXT_X, base, s, ink);
        for (int t = 0; t < v->tracks; t++) {
            int b = v->blocks[i][t];
            if (!v->numbers) {
                int x = SQ_X + t * (SQ + SQ_GAP), sy = y + (ROW - SQ) / 2;
                if (b != NONE)
                    gfx_fill(x, sy, SQ, SQ, ink, 255);
                else
                    gfx_frame_rect(x, sy, SQ, SQ, here ? C_ON_ACC : C_DIM, 255);
            } else if (t >= left && t < left + CELLS) {
                int x = CELL_X + (t - left) * CELL_W;
                if (b != NONE)
                    hex2(s, b);
                gfx_text_mid(F_COND_20, x, CELL_W, base, b != NONE ? s : "--",
                             b != NONE ? ink : here ? C_ON_ACC : C_DIM);
            }
        }
    }
    if (v->numbers) /* the phrase's number apart from the tracks' blocks */
        gfx_fill(CELL_X - 3, y0, 1, rows * ROW, C_DIM, 255);
    if (v->n > rows) { /* where the screen is in the song */
        int h = rows * ROW * rows / v->n, y = y0 + rows * ROW * top / v->n;
        gfx_fill(PLAT_W - 4, y0, 2, rows * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

static void enter(int v)
{
    var = v;
    cursor = top = left = 0;
}

void app_init(void)
{
    enter(0);
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
    int last;
    if (in->enc[PE_PRESET])
        enter(((var + in->enc[PE_PRESET]) % NVAR + NVAR) % NVAR);
    v = &variants[var];
    cursor += in->enc[PE_SELECT];
    cursor = cursor < 0 ? 0 : cursor >= v->n ? v->n - 1 : cursor;
    if (cursor < top)
        top = cursor;
    if (cursor >= top + ROWS)
        top = cursor - ROWS + 1;
    last = v->numbers && v->tracks > CELLS ? v->tracks - CELLS : 0;
    left += in->enc[PE_K1];
    left = left < 0 ? 0 : left > last ? last : left;
    gfx_frame(draw_song);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
