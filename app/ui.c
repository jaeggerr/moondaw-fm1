/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"

#include "../core/player.h"

#include "app.h"
#include "gfx.h"
#include "loop.h"

char *ui_hex2(char *b, int v)
{
    b[0] = "0123456789ABCDEF"[(v >> 4) & 15];
    b[1] = "0123456789ABCDEF"[v & 15];
    b[2] = '\0';
    return b;
}

char *ui_dec(char *b, int v)
{
    char t[8];
    int n = 0, i = 0;
    do
        t[n++] = (char)('0' + v % 10);
    while ((v /= 10) > 0);
    while (n)
        b[i++] = t[--n];
    b[i] = '\0';
    return b;
}

void ui_card(int k, const char *label, const char *value, const char *unit)
{
    int w = (APP_INNER - 6) / 4, x = APP_X0 + k * (w + 2);
    char kn[3] = { 'K', (char)('1' + k), '\0' };

    gfx_slab(x, UI_CARDS_Y, w, UI_CARDS_H, C_TEXT, label ? 20 : 8);
    gfx_text(F_MONO8, gfx_text(F_MONO8, x + 3, UI_CARDS_Y + 8, kn, label ? C_ACCENT : C_DIM) + 3,
             UI_CARDS_Y + 8, label ? label : "", C_DIM);
    if (value) {
        int vx = gfx_text(F_COND12, x + 3, UI_CARDS_Y + 19, value, C_TEXT);
        if (unit)
            gfx_text(F_MONO8, vx + 3, UI_CARDS_Y + 19, unit, C_DIM);
    }
}

void ui_cards_rule(void)
{
    gfx_fill(APP_X0, UI_CARDS_Y + UI_CARDS_H + 3, APP_INNER, 1, C_ACCENT, 71);
}

int ui_loop_slot(int x, int y)
{
    char t[8];
    int tx;
    if (loop_state() == LOOP_NONE)
        return 0;
    ui_hex2(t, loop_from());
    t[2] = '-';
    if (loop_state() == LOOP_SET)
        ui_hex2(t + 3, loop_to());
    else
        t[3] = t[4] = '.', t[5] = '\0';
    gfx_slab(x, y, 58, 12, C_TITLE, 36);
    tx = gfx_text(F_MONO8, x + 4, y + 9, "LOOP", C_TITLE);
    gfx_text(F_COND12, tx + 3, y + 10, t, C_TITLE);
    return 58;
}

void ui_btn_map(const char *const word[12], const uint8_t state[12], uint32_t held)
{
    for (int b = 0; b < 12; b++) {
        int w = (APP_INNER - 10) / 6, x = APP_X0 + (b % 6) * (w + 2),
            y = BTN_MAP_Y + (b / 6) * (BTN_MAP_H + 1);
        uint8_t s = state[b];
        bool down = (s == UI_BTN_ON || s == UI_BTN_BACK || s == UI_BTN_HERE) && ((held >> APP_BTN_AT[b]) & 1u);
        bool green = s == UI_BTN_LIVE || down, red = s == UI_BTN_ASK, cyan = s == UI_BTN_HERE && !down;

        gfx_slab(x, y, w, BTN_MAP_H, red ? C_MUTE : green ? C_ACCENT : cyan ? C_TITLE : C_TEXT,
                 red || green || cyan ? 255 : s >= UI_BTN_ON ? 18 : 8);
        if (s != UI_BTN_NONE)
            gfx_text_mid(F_COND9, x, w, y + 8, word[b],
                         red ? C_WHITE : green || cyan ? C_ON_ACC : s == UI_BTN_OFF ? C_BEAT
                         : s == UI_BTN_BACK ? C_TITLE : C_TEXT);
    }
}

int ui_leds = UI_LEDS_GUIDE, ui_light = PLAT_LED_50, ui_count_in = 1;
static uint32_t shown = ~0u; /* what the guide last wrote */

static uint32_t keys_shown = ~0u;

void ui_led_guide_again(void) { shown = keys_shown = ~0u; }

void ui_led_state(int btn, bool on) { plat_led_btn(btn, on && ui_leds != UI_LEDS_OFF); }

void ui_led_keys(uint32_t usable, uint32_t lit, uint32_t half)
{
    static uint32_t lit_shown = ~0u, half_shown = ~0u;
    if (ui_leds != UI_LEDS_GUIDE)
        usable = 0;
    if (ui_leds == UI_LEDS_OFF)
        lit = half = 0;
    if (usable == keys_shown && lit == lit_shown && half == half_shown)
        return;
    for (int k = 0; k < PLAT_KEYS; k++)
        plat_led_level(PLAT_BTNS + k, (lit >> k) & 1u    ? PLAT_LED_LIT
                                      : (half >> k) & 1u ? PLAT_LED_50
                                      : (usable >> k) & 1u ? ui_light
                                                           : PLAT_LED_OFF);
    keys_shown = usable;
    lit_shown = lit, half_shown = half;
}

void ui_led_mono(int track, uint32_t usable)
{
    bool alone = track >= 0 && player_soloed(track), muted = track >= 0 && player_muted(track);
    ui_led_keys(usable, alone ? 1u << UI_KEY_MONO : 0u, muted ? 1u << UI_KEY_MONO : 0u);
}

bool ui_shift(UiShift *s, const PlatInput *in, uint32_t pressed, uint32_t keys, bool ops, int track,
              const char **said)
{
    bool tapped = false;
    if (pressed & 1u << PB_EDIT)
        s->held = true, s->used = false;
    if (s->held && keys)
        s->used = true;
    if (track >= 0 && (s->held || ops || s->mono) && (keys >> UI_KEY_MONO & 1u))
        s->mono = 1, s->muted = false;
    else if (s->mono && (in->keys >> UI_KEY_MONO & 1u) && ++s->mono == MONO_HOLD && track >= 0) {
        if (player_soloed(track)) /* held: muted, whatever it was */
            player_toggle_solo(track);
        if (!player_muted(track))
            player_toggle_mute(track);
        s->muted = true;
        *said = "MUTE";
    } else if (s->mono && !(in->keys >> UI_KEY_MONO & 1u)) {
        if (!s->muted && track >= 0) { /* tapped: alone, or back to normal */
            if (player_soloed(track) || player_muted(track)) {
                if (player_soloed(track))
                    player_toggle_solo(track);
                if (player_muted(track))
                    player_toggle_mute(track);
                *said = "NORMAL";
            } else {
                player_toggle_solo(track);
                *said = "SOLO";
            }
        }
        s->mono = 0;
    }
    if (s->held && !(in->btns & 1u << PB_EDIT)) {
        s->held = false;
        tapped = !s->used;
    }
    return tapped;
}

void ui_led_guide(uint32_t usable)
{
    static int lit_at = -1;
    if (ui_leds != UI_LEDS_GUIDE)
        usable = 0;
    /* PLAY is the screen's: lit is its state, playing. HOME, SAVE: the same
     * everywhere; SEL: a way through the screens, not a feature of this one. */
    usable &= ~(1u << PB_PLAY | 1u << PB_REC | 1u << PB_HOME | 1u << PB_SAVE | 1u << PB_SEL);
    if (usable == shown && ui_light == lit_at)
        return;
    for (int b = 0; b < PLAT_BTNS; b++)
        if (b != PB_PLAY && b != PB_REC) /* the screens' states */
            plat_led_level(b, (usable >> b) & 1u ? ui_light : PLAT_LED_OFF);
    shown = usable;
    lit_at = ui_light;
}
