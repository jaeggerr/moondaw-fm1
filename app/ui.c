/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"

#include "app.h"
#include "gfx.h"

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

void ui_btn_map(const char *const word[12], const uint8_t state[12], uint32_t held)
{
    for (int b = 0; b < 12; b++) {
        int w = (APP_INNER - 10) / 6, x = APP_X0 + (b % 6) * (w + 2),
            y = BTN_MAP_Y + (b / 6) * (BTN_MAP_H + 1);
        uint8_t s = state[b];
        bool down = (s == UI_BTN_ON || s == UI_BTN_BACK) && ((held >> APP_BTN_AT[b]) & 1u);
        bool green = s == UI_BTN_LIVE || down, red = s == UI_BTN_ASK;

        gfx_slab(x, y, w, BTN_MAP_H, red ? C_MUTE : green ? C_ACCENT : C_TEXT,
                 red || green ? 255 : s >= UI_BTN_ON ? 18 : 8);
        if (s != UI_BTN_NONE)
            gfx_text_mid(F_COND9, x, w, y + 8, word[b],
                         red ? C_WHITE : green ? C_ON_ACC : s == UI_BTN_OFF ? C_BEAT
                         : s == UI_BTN_BACK ? C_TITLE : C_TEXT);
    }
}
