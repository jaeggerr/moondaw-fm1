/* SPDX-License-Identifier: GPL-3.0-only */
#include "settings.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "ui.h"

enum { S_LEDS, S_LIGHT, S_COUNT, S_FXHUE, ROWS };
enum { LABEL_X = 14, VALUE_X = 120 };

static const char *const label[ROWS] = { "LEDS", "LIGHT", "COUNT-IN", "FX HUE" };
static const char *const fxhue_word[UI_FX_HUES] = { "OFF", "VIOLET", "CYAN", "SAND", "SWAP" };
static const char *const count_word[UI_COUNT_MAX + 1] = { "OFF", "1 BAR", "2 BARS" };
static const char *const leds_word[3] = { "OFF", "STATE", "GUIDE" };
/* LIGHT's steps, dimmest first: the panel's levels (platform.h). */
static const int light_level[] = { PLAT_LED_MID, PLAT_LED_50, PLAT_LED_62, PLAT_LED_75, PLAT_LED_87, PLAT_LED_LIT };
static const char *const light_word[] = { "15 %", "50 %", "62 %", "75 %", "87 %", "100 %" };
enum { LIGHTS = sizeof light_level / sizeof light_level[0] };

static int row, was_leds, was_light, was_count;
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
        gfx_text(F_COND20, VALUE_X, grid_base(k),
                 k == S_LEDS      ? leds_word[ui_leds]
                 : k == S_LIGHT    ? light_word[light_at()]
                 : k == S_COUNT    ? count_word[ui_count_in]
                                   : fxhue_word[ui_fx_hue],
                 here ? C_ACCENT : C_TEXT);
    }
}

void settings_open(void)
{
    row = 0;
    was_leds = ui_leds, was_light = ui_light, was_count = ui_count_in;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

bool settings_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    int d = in->enc[PE_K3];

    prev_btns = in->btns;
    row = clampi(row + in->enc[PE_SELECT] + in->enc[PE_K1], 0, ROWS - 1);
    if (d && row == S_LEDS)
        ui_leds = clampi(ui_leds + d, UI_LEDS_OFF, UI_LEDS_GUIDE);
    else if (d && row == S_LIGHT)
        ui_light = light_level[clampi(light_at() + d, 0, LIGHTS - 1)];
    else if (d && row == S_COUNT)
        ui_count_in = clampi(ui_count_in + d, 0, UI_COUNT_MAX);
    else if (d)
        ui_fx_hue = clampi(ui_fx_hue + d, 0, UI_FX_HUES - 1);
    /* LIGHT's row lights a few buttons at it, to judge on the panel */
    ui_led_guide(row == S_LIGHT ? 1u << PB_FX | 1u << PB_ENV | 1u << PB_LFO | 1u << PB_EDIT | 1u << PB_SEQ |
                                      1u << PB_ARP
                                : 0u);
    if ((btns >> PB_PLAY & 1u) && player_playing()) /* stop, from anywhere */
        player_stop();
    ui_led_state(PB_PLAY, player_playing());
    gfx_frame(draw);
    return !((btns >> PB_HOME) & 1u);
}

void settings_close(void)
{
    if (ui_leds != was_leds || ui_light != was_light || ui_count_in != was_count)
        app_keep_settings();
}
