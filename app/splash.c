/* SPDX-License-Identifier: GPL-3.0-only */
#include "splash.h"

#include "../platform/platform.h"
#include "gfx.h"
#include "logo.h"
#include "theme.h"
#include "version.h"

/* The Mac app's wordmark, as pixels rather than as a picture: 66 x 40 at
 * two bits each is 660 bytes, where the same thing drawn into RGB565
 * would have been 115 KB against an image that has to fit under 0x97000.
 * SCALE is an integer, so the edges stay as hard as the icon's own
 * generator draws them -- it is pixel art and must not be resampled.
 *
 * Its two inks are the UI's: gold is C_FX, cyan is C_TITLE. Both repos
 * took them from moondaw's src/ui/ui.c, so there is one value, not two
 * that have to be kept level. */
enum {
    SCALE = 3,
    LOGO_X = (PLAT_W - LOGO_COLS * SCALE) / 2,  /* 21: 198 of 240 across */
    LOGO_Y = 48,
    SUB_Y = 198,                             /* the baseline under it */
};

static void logo(void)
{
    for (int y = 0; y < LOGO_ROWS; y++)
        for (int x = 0; x < LOGO_COLS; x++) {
            int i = y * LOGO_COLS + x;
            int ink = logo_px[i >> 2] >> (2 * (i & 3)) & 3;
            if (ink)
                gfx_fill(LOGO_X + x * SCALE, LOGO_Y + y * SCALE, SCALE, SCALE,
                         ink == 1 ? C_FX : C_TITLE, 255);
        }
}

static void draw(void)
{
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    logo();
    gfx_text_mid(F_COND12, 0, PLAT_W, SUB_Y, "FM-1   " MOONDAW_VERSION, C_DIM);
}

void splash_show(void)
{
    gfx_invalidate();   /* the panel's RAM holds whatever it held: send every band */
    gfx_frame(draw);
}
