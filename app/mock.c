/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song; the data is copied from real ones. KNOB 1 turns the variant, SELECT moves the cursor.
 *
 * SONG, what a project opens on: its structure. One row a phrase, in the song's order: the
 * phrase's number, then one square a track -- filled when the track plays in that phrase,
 * empty when it does not. No names (none of the 13 songs has one), no blocks, no tempo. The
 * cursor starts on the first phrase; the phrase playing is marked by a triangle. (HOME, the
 * first mockup, is now app/menu.c.) */
#include "../platform/platform.h"
#include "gfx.h"

#define BG GFX_RGB(0, 0, 0)
#define FG GFX_RGB(235, 235, 235)
#define DIM GFX_RGB(120, 120, 120)
#define CYAN GFX_RGB(0, 200, 230)
#define CURSOR GFX_RGB(80, 220, 120)

#define ROW 28
#define ROWS 8
#define TEXT_X 14
#define SQ 14                     /* a track's square */
#define SQ_GAP 5
#define SQ_X 54

typedef struct {
    const uint8_t *phrases;       /* bit t: track t plays in that phrase */
    int n, tracks, playing;       /* playing: a phrase, or -1 stopped */
} Variant;

/* KIDS.trk and robot-demo.trk, as their arrangements are */
static const uint8_t kids[] = { 0x85, 0xAD, 0xFD, 0xFF, 0xFF, 0xFF, 0x8C, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFB, 0xFA, 0xFF, 0xFF, 0xFF, 0xAD, 0x84, 0x84 };
static const uint8_t robot[] = { 0x3E, 0x3F, 0x3F, 0x30 };

static const Variant variants[] = {
    { kids, 19, 8, -1 },          /* KIDS just opened */
    { kids, 19, 8, 5 },           /* KIDS playing */
    { robot, 4, 6, -1 },          /* a short song on six tracks */
};
#define NVAR ((int)(sizeof variants / sizeof variants[0]))

static int var, cursor, top;

static void draw_song(void)
{
    const Variant *v = &variants[var];
    gfx_fill(0, 0, PLAT_W, PLAT_H, BG, 255);
    for (int r = 0; r < ROWS && top + r < v->n; r++) {
        int i = top + r, y = r * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int here = i == cursor, plays = i == v->playing;
        char num[3] = { "0123456789ABCDEF"[i >> 4], "0123456789ABCDEF"[i & 15], 0 };
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4 - (v->n > ROWS ? 6 : 0), ROW - 2, CURSOR, 255);
        if (plays) /* a triangle pointing right: what plays */
            for (int k = 0; k < 6; k++)
                gfx_fill(4 + k, y + 8 + k, 1, 12 - 2 * k, here ? BG : CYAN, 255);
        gfx_text(F_COND_20, TEXT_X, base, num, here ? BG : plays ? CYAN : FG);
        for (int t = 0; t < v->tracks; t++) {
            int x = SQ_X + t * (SQ + SQ_GAP), sy = y + (ROW - SQ) / 2;
            if (v->phrases[i] >> t & 1)
                gfx_fill(x, sy, SQ, SQ, here ? BG : plays ? CYAN : FG, 255);
            else
                gfx_frame_rect(x, sy, SQ, SQ, here ? BG : DIM, 255);
        }
    }
    if (v->n > ROWS) { /* where the screen is in the song */
        int h = ROWS * ROW * ROWS / v->n, y = ROWS * ROW * top / v->n;
        gfx_fill(PLAT_W - 4, 0, 2, ROWS * ROW, GFX_RGB(40, 40, 40), 255);
        gfx_fill(PLAT_W - 4, y, 2, h, DIM, 255);
    }
}

static void enter(int v)
{
    var = v;
    cursor = 0;
    top = 0;
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
    int n;
    if (in->enc[PE_K1])
        enter(((var + in->enc[PE_K1]) % NVAR + NVAR) % NVAR);
    n = variants[var].n;
    cursor += in->enc[PE_SELECT];
    cursor = cursor < 0 ? 0 : cursor >= n ? n - 1 : cursor;
    if (cursor < top)
        top = cursor;
    if (cursor >= top + ROWS)
        top = cursor - ROWS + 1;
    gfx_frame(draw_song);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
