/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mockups judged on the panel (MOONDAW_APP=mock, built with GFX_SPECIMEN): one screen
 * at a time, in CONDENSED 20, the size chosen on the FM-1 on 28/10. Nothing here touches a
 * song; the data is made up. KNOB 1 turns the variant, SELECT moves the cursor in it.
 *
 * HOME: the projects, by name only, one a row; the open one marked by a bar at its left, a dot
 * after its name while it has unsaved changes; + NEW last; the firmware version small at the
 * bottom. The cursor starts on the open project. */
#include "../platform/platform.h"
#include "gfx.h"

#define BG GFX_RGB(0, 0, 0)
#define FG GFX_RGB(235, 235, 235)
#define DIM GFX_RGB(120, 120, 120)
#define CYAN GFX_RGB(0, 200, 230)
#define AMBER GFX_RGB(255, 170, 0)
#define CURSOR GFX_RGB(80, 220, 120)

#define ROW 28                    /* CONDENSED 20 is 24 px high: 4 px between rows */
#define ROWS 8                    /* 8 x 28 = 224, the version takes the last 16 */
#define TEXT_X 12

typedef struct {
    const char *title;            /* shown nowhere: what the variant is for */
    const char *const *names;
    int n, open, dirty;           /* open: index or -1 */
} Variant;

static const char *const few[] = { "KIDS", "tremolo-chip", "robot-demo", "sandstorm3" };
static const char *const many[] = {
    "AMPTEST", "DAFT-PUNK", "DAFT-PUNK-EN", "KIDS", "LEOMOTEUR", "clavioline", "piano-velocity",
    "robot-demo", "sandstorm3", "song03", "supersaw2", "tremolo", "tremolo-chip",
};

static const Variant variants[] = {
    { "a few projects", few, 4, 1, 0 },
    { "the open one has unsaved changes", few, 4, 1, 1 },
    { "more than a screen", many, 13, 11, 0 },
    { "the first start: nothing yet", 0, 0, -1, 0 },
};
#define NVAR ((int)(sizeof variants / sizeof variants[0]))

static int var, cursor, top;

static void enter(int v)
{
    var = v;
    int count = variants[v].n + 1, last = count > ROWS ? count - ROWS : 0;
    cursor = variants[v].open >= 0 ? variants[v].open : 0;
    top = cursor - ROWS / 2 > 0 ? cursor - ROWS / 2 : 0; /* the open one mid-screen... */
    top = top > last ? last : top;                       /* ...but never blank rows below */
}

static void draw_home(void)
{
    const Variant *v = &variants[var];
    int count = v->n + 1;                      /* + NEW */
    gfx_fill(0, 0, PLAT_W, PLAT_H, BG, 255);
    for (int r = 0; r < ROWS && top + r < count; r++) {
        int i = top + r, y = r * ROW, base = y + 2 + FONTS[F_COND_20].ascent;
        int here = i == cursor;
        const char *name = i < v->n ? v->names[i] : "+ NEW";
        uint16_t c = here ? BG : i < v->n ? FG : DIM;
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4 - (count > ROWS ? 6 : 0), ROW - 2, CURSOR, 255);
        if (i == v->open)
            gfx_fill(4, y + 6, 3, ROW - 12, here ? BG : CYAN, 255);
        int x = gfx_text(F_COND_20, TEXT_X, base, name, c);
        if (i == v->open && v->dirty)
            gfx_fill(x + 6, base - 9, 7, 7, here ? BG : AMBER, 255);
    }
    if (count > ROWS) {                        /* where the screen is in the list */
        int h = ROWS * ROW * ROWS / count, y = ROWS * ROW * top / count;
        gfx_fill(PLAT_W - 4, 0, 2, ROWS * ROW, GFX_RGB(40, 40, 40), 255);
        gfx_fill(PLAT_W - 4, y, 2, h, DIM, 255);
    }
    gfx_text(F_COND_12, TEXT_X, PLAT_H - 4, "MOONDAW v0.1", DIM);
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
    int count;
    if (in->enc[PE_K1])
        enter(((var + in->enc[PE_K1]) % NVAR + NVAR) % NVAR);
    count = variants[var].n + 1;
    cursor += in->enc[PE_SELECT];
    cursor = cursor < 0 ? 0 : cursor >= count ? count - 1 : cursor;
    if (cursor < top)
        top = cursor;
    if (cursor >= top + ROWS)
        top = cursor - ROWS + 1;
    gfx_frame(draw_home);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
