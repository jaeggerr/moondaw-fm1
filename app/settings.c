/* SPDX-License-Identifier: GPL-3.0-only */
#include "settings.h"

#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "ui.h"

enum { S_LEDS, S_LIGHT, ROWS };
enum { LABEL_X = 14, VALUE_X = 120 };

static const char *const label[ROWS] = { "LEDS", "LIGHT" };
static const char *const leds_word[3] = { "OFF", "STATE", "GUIDE" };
/* LIGHT's steps, dimmest first: the panel's levels (platform.h). */
static const int light_level[] = { PLAT_LED_MID, PLAT_LED_50, PLAT_LED_62, PLAT_LED_75, PLAT_LED_87, PLAT_LED_LIT };
static const char *const light_word[] = { "15 %", "50 %", "62 %", "75 %", "87 %", "100 %" };
enum { LIGHTS = sizeof light_level / sizeof light_level[0] };

static int row, was_leds, was_light;
static uint32_t prev_btns;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int light_at(void)
{
    for (int i = 0; i < LIGHTS; i++)
        if (light_level[i] == ui_light)
            return i;
    return 1;
}

static void draw(void)
{
    grid_clear();
    grid_corner("SETTINGS", C_DIM, 0, 0);
    for (int k = 0; k < ROWS; k++) {
        bool here = k == row;
        if (here) {
            grid_band(k, PLAT_W - 4);
            grid_hollow(k, VALUE_X - 6, PLAT_W - VALUE_X);
        }
        gfx_text(F_COND20, LABEL_X, grid_base(k), label[k], here ? C_ON_ACC : C_DIM);
        gfx_text(F_COND20, VALUE_X, grid_base(k), k == S_LEDS ? leds_word[ui_leds] : light_word[light_at()],
                 here ? C_ACCENT : C_TEXT);
    }
}

void settings_open(void)
{
    row = 0;
    was_leds = ui_leds, was_light = ui_light;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

bool settings_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    int d = in->enc[PE_K3];

    prev_btns = in->btns;
    row = clampi(row + in->enc[PE_SELECT] + in->enc[PE_K2], 0, ROWS - 1);
    if (d && row == S_LEDS)
        ui_leds = clampi(ui_leds + d, UI_LEDS_OFF, UI_LEDS_GUIDE);
    else if (d)
        ui_light = light_level[clampi(light_at() + d, 0, LIGHTS - 1)];
    /* LIGHT's row lights a few buttons at it, to judge on the panel */
    ui_led_guide(row == S_LIGHT ? 1u << PB_FX | 1u << PB_ENV | 1u << PB_LFO | 1u << PB_EDIT | 1u << PB_SEQ |
                                      1u << PB_ARP
                                : 0u);
    gfx_frame(draw);
    return !((btns >> PB_GLO) & 1u);
}

void settings_close(void)
{
    if (ui_leds != was_leds || ui_light != was_light)
        app_keep_settings();
}
