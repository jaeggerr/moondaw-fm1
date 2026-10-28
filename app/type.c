/* SPDX-License-Identifier: GPL-3.0-only */
/* The type specimen (MOONDAW_APP=type, built with GFX_SPECIMEN): what is readable on the FM-1's
 * panel, judged on the panel -- the screens drawn without it were too small (28/10).
 *
 * Page 0 is a ruler: the panel's edge, ticks every 10 px, and a 100 px square to measure with a
 * real ruler. Every other page is one face at one size, filled with tracker rows at that size;
 * its title says how many lines and characters a screen of it holds. KNOB 1 or SELECT turns the
 * page. */
#include "../platform/platform.h"
#include "gfx.h"

#define BG GFX_RGB(0, 0, 0)
#define FG GFX_RGB(255, 255, 255)
#define DIM GFX_RGB(120, 120, 120)
#define CYAN GFX_RGB(0, 200, 230)
#define AMBER GFX_RGB(255, 170, 0)
#define GREEN GFX_RGB(80, 220, 120)

#define NFONTS ((int)(sizeof FONTS / sizeof FONTS[0]))

static int page; /* 0 the ruler, then FONTS[page - 1] */

static const char *const rows[] = {
    "00 C-4 01 VOL 40", "01 --- -- ARP 37", "02 D#5 03 SLD 0C", "03 OFF -- --- --",
    "04 G-3 02 VIB 46", "05 A#4 01 RMP 10", "06 --- -- --- --", "07 F-2 04 CUT 7F",
};
static const char *const lines[] = {
    "LEN 16  BPM 120  SW 50", "Il1| 0O8B 5S 2Z 6G", "PATTERN PHRASE SONG MIXER",
};

static char *num(char *p, int n) /* no libc on the device */
{
    char d[8];
    int k = 0;
    do
        d[k++] = (char)('0' + n % 10);
    while ((n /= 10) > 0);
    while (k)
        *p++ = d[--k];
    return p;
}

static char *cat(char *p, const char *t)
{
    while (*t)
        *p++ = *t++;
    *p = 0;
    return p;
}

static void draw_ruler(void)
{
    char s[32], *p;
    gfx_fill(0, 0, PLAT_W, PLAT_H, BG, 255);
    gfx_frame_rect(0, 0, PLAT_W, PLAT_H, FG, 255);
    for (int i = 10; i < PLAT_W; i += 10) {
        int len = i % 50 ? 4 : 10;
        gfx_fill(i, 0, 1, len, FG, 255);
        gfx_fill(0, i, len, 1, FG, 255);
    }
    gfx_frame_rect(70, 70, 100, 100, CYAN, 255);
    gfx_text_mid(F_COND_14, 70, 100, 115, "100 px", CYAN);
    gfx_text_mid(F_COND_12, 0, PLAT_W, 200, "KNOB 1: next page", DIM);
    p = num(s, NFONTS);
    cat(p, " sizes after this page");
    gfx_text_mid(F_COND_12, 0, PLAT_W, 216, s, DIM);
}

static void draw_font(void)
{
    int f = page - 1, pitch = FONTS[f].height + 2, y = FONTS[f].ascent + 2, n = 0;
    int fit = (PLAT_H - 4) / pitch, cols = (PLAT_W - 4) / gfx_text_w(f, "0");
    char s[48], *p;

    gfx_fill(0, 0, PLAT_W, PLAT_H, BG, 255);
    p = cat(s, FONT_NAMES[f]); /* the title, then "  14L 26C" */
    p = cat(num(cat(p, "  "), fit), "L ");
    cat(num(p, cols), "C");
    gfx_text(f, 2, y, s, CYAN);
    for (int i = 0; i < 3 && y + pitch <= PLAT_H; i++) {
        y += pitch;
        gfx_text(f, 2, y, lines[i], i == 1 ? AMBER : FG);
    }
    while (y + pitch <= PLAT_H) {
        y += pitch;
        if (n % 4 == 0) /* a beat's first row, as a tracker marks it */
            gfx_fill(0, y - FONTS[f].ascent - 1, PLAT_W, pitch, GFX_RGB(30, 40, 50), 255);
        gfx_text(f, 2, y, rows[n % 8], n == 2 ? GREEN : FG);
        n++;
    }
}

void app_init(void) { gfx_invalidate(); }

int app_load(const char *trk, uint32_t len, const char *name)
{
    (void)trk, (void)len, (void)name;
    return 0;
}

void app_frame(const PlatInput *in)
{
    int d = in->enc[PE_K1] + in->enc[PE_SELECT];
    page = ((page + d) % (NFONTS + 1) + NFONTS + 1) % (NFONTS + 1);
    gfx_frame(page ? draw_font : draw_ruler);
}

void app_audio(int16_t *out, int frames)
{
    for (int i = 0; i < 2 * frames; i++)
        out[i] = 0;
}
