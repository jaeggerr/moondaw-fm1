/* SPDX-License-Identifier: GPL-3.0-only */
#include "syllable.h"

#include "../core/player.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"

enum { MAX = 5, SING = 36 /* frames a change is sung for: 600 ms */ };
enum { KEYS, WHEEL };

/* The keyboard, F3 to G5: which of the 27 keys are white, which black. */
static const uint8_t white[16] = { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };
static const uint8_t black[11] = { 1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25 };
/* Their signs, two layers: consonants in QWERTY's rows, vowels; a space types nothing. */
static const char *const on_white[2] = { "RTPSDFGJKLZCVBNM", "QHX+            " };
static const char *const on_black[2] = { "AEIOUY72963", "05148      " };
/* The wheel: empty, then the alphabet in its order -- letters, digits, + and . */
static const char wheel[] = "-ABCDEFGHIJKLMNOPQRSTUVXYZ0123456789+.";

/* What each sign sounds like, for a hand that does not know the alphabet yet. */
static const char *sound(char c)
{
    static const char *const v[] = {
        "Aa  papa", "Ee  ete", "Ii  lit", "Oo  eau", "Uou  loup", "Yu  tu", "7e  sept", "2eu  deux",
        "9eur  neuf", "6o  pomme", "3an  trente", "0on  bon", "5in  cinq", "1i  bit (EN)", "4a  cat (EN)",
        "8oo  book (EN)", "Rr  francais", "Hh  hello", "Cch  chat", "Jj  jour", "Xth  thin (EN)",
        "Qr  right (EN)", "+th  the (EN)", ".weak: softer", "Pp", "Bb", "Tt", "Dd", "Kk", "Gg", "Ff", "Vv",
        "Ss", "Zz", "Mm", "Nn", "Ll",
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
        if (v[i][0] == c)
            return v[i] + 1;
    return "";
}

static char slots[MAX], text[MAX + 1], last;
/* ALGO, as on every screen: the syllable after each change, HIST of them;
 * hist[now] is what shows, hist[now + 1 .. top] what ALGO can do again. */
enum { HIST = 32 };
static char hist[HIST][MAX];
static int now, top;
static uint32_t held; /* the keys under the hand: lit on the drawing, to find one's place */
static int mode, layer, at, instr, track, row, singing;
static uint8_t note;

static const char *join(void)
{
    int n = 0;
    for (int i = 0; i < MAX; i++)
        if (slots[i] != '-')
            text[n++] = slots[i];
    text[n] = 0;
    return text;
}

static void set(const char *s)
{
    int i = 0;
    for (; i < MAX && s[i]; i++)
        slots[i] = s[i];
    for (; i < MAX; i++)
        slots[i] = '-';
}

static void sing(void)
{
    if (!join()[0])
        return;
    player_note_sing(instr, note, text);
    singing = SING;
}

static void keep(void)
{
    if (now == HIST - 1) { /* full: the oldest goes */
        for (int i = 1; i < HIST; i++)
            for (int k = 0; k < MAX; k++)
                hist[i - 1][k] = hist[i][k];
        now--;
    }
    now++;
    for (int k = 0; k < MAX; k++)
        hist[now][k] = slots[k];
    top = now;
}

void syllable_open(const char *t, int in, uint8_t n, int tr, int r)
{
    set(t);
    for (int k = 0; k < MAX; k++)
        hist[0][k] = slots[k];
    now = top = 0;
    instr = in, note = n, track = tr, row = r;
    last = 0;
    at = 0;
    while (at < MAX - 1 && slots[at] != '-')
        at++;
    singing = 0;
    gfx_invalidate();
}

const char *syllable_text(void) { return join(); }

uint32_t syllable_buttons(void)
{
    return 1u << PB_LFO | (mode == KEYS ? 1u << PB_OCTDN | 1u << PB_OCTUP | 1u << PB_ARP : 0u);
}

/* --------------------------------------------------------- the hands --- */
static bool keys_typed(uint32_t keys)
{
    bool changed = false;
    for (int k = 0; k < PLAT_KEYS; k++) {
        char c = ' ';
        int end = MAX;
        if (!((keys >> k) & 1u))
            continue;
        for (int i = 0; i < 16; i++)
            if (white[i] == k)
                c = on_white[layer][i];
        for (int i = 0; i < 11; i++)
            if (black[i] == k)
                c = on_black[layer][i];
        while (end > 0 && slots[end - 1] == '-') /* after the last sign */
            end--;
        if (c != ' ' && end < MAX) {
            slots[end] = c;
            last = c;
            changed = true;
        }
    }
    return changed;
}

static bool weak(void)
{
    char s[MAX + 2] = ".";
    const char *t = join();
    bool had = false;
    int n = 1;
    for (const char *p = t; *p; p++) {
        if (*p == '.')
            had = true;
        else
            s[n++] = *p;
    }
    s[n] = 0;
    set(had ? s + 1 : s);
    last = '.';
    return true;
}

int syllable_frame(const PlatInput *in, uint32_t pressed, uint32_t keys)
{
#define PRESSED(b) ((pressed >> (b)) & 1u)
    bool changed = false;
    int d;

    held = in->keys;
    if (in->enc[PE_PRESET]) {
        mode = mode == KEYS ? WHEEL : KEYS;
        gfx_invalidate();
    }
    if (mode == KEYS) {
        if (PRESSED(PB_OCTUP) || PRESSED(PB_OCTDN))
            layer ^= 1;
        changed = keys_typed(keys);
        if (PRESSED(PB_LFO))
            for (int i = MAX - 1; i >= 0; i--)
                if (slots[i] != '-') {
                    slots[i] = '-';
                    changed = true;
                    break;
                }
        if (PRESSED(PB_ARP))
            changed = weak();
    } else {
        at += in->enc[PE_K2];
        at = at < 0 ? 0 : at >= MAX ? MAX - 1 : at;
        if ((d = in->enc[PE_K3]) != 0) {
            int i = 0;
            while (wheel[i] && wheel[i] != slots[at])
                i++;
            i += d;
            i = i < 0 ? 0 : i >= (int)sizeof wheel - 1 ? (int)sizeof wheel - 2 : i;
            changed = wheel[i] != slots[at];
            slots[at] = wheel[i];
        }
        if (PRESSED(PB_LFO) && slots[at] != '-')
            slots[at] = '-', changed = true;
    }
    if (changed)
        keep();
    if ((d = in->enc[PE_ALGO]) != 0) { /* UNDO left, REDO right */
        int to = now + d;
        to = to < 0 ? 0 : to > top ? top : to;
        changed = to != now;
        now = to;
        for (int k = 0; k < MAX; k++)
            slots[k] = hist[now][k];
    }
    if (changed || PRESSED(PB_PLAY))
        sing();
    else if (singing > 0 && --singing == 0)
        player_note_off();
    ui_led_state(PB_PLAY, singing > 0);
    if (PRESSED(PB_SEL) || PRESSED(PB_HOME)) {
        player_note_off();
        return PRESSED(PB_SEL) ? SYLLABLE_KEPT : SYLLABLE_LEFT;
    }
    return SYLLABLE_TYPING;
#undef PRESSED
}

/* ------------------------------------------------------- the drawing --- */
static void hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 4, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 4, word, C_DIM);
}

static void sign_said(char c, int y)
{
    char s[2] = { c, 0 };
    if (!c || !sound(c)[0])
        return;
    gfx_text(F_COND14, 12, y, s, C_FX);
    gfx_text(F_COND14, 30, y, sound(c), C_DIM);
}

static void draw_keys(void)
{
    int top = 122, w = 15;
    sign_said(last, 90);
    gfx_text(F_COND12, 12, 112, layer ? "LAYER 2" : "LAYER 1", layer ? C_TITLE : C_DIM);
    gfx_text(F_COND12, 64, 112, "OCT other layer   LFO rub out   ARP weak", C_DIM);
    for (int i = 0; i < 16; i++) {
        char c[2] = { on_white[layer][i], 0 };
        bool down = (held >> white[i]) & 1u;
        gfx_fill(i * w + 1, top, w - 2, 98, down ? C_ACCENT : c[0] != ' ' ? C_KEY : C_KEY_NONE, 255);
        if (c[0] != ' ')
            gfx_text(F_COND14, i * w + 4, top + 92, c, C_KEY_ON);
    }
    for (int i = 0; i < 11; i++) {
        char c[2] = { on_black[layer][i], 0 };
        int prev = 0, x;
        for (int j = 0; j < 16; j++)
            if (white[j] < black[i])
                prev = j;
        bool down = (held >> black[i]) & 1u;
        x = (prev + 1) * w - 5;
        gfx_fill(x, top, 10, 56, down ? C_ACCENT : c[0] != ' ' ? C_KEY_B : C_KEY_BOFF, 255);
        if (c[0] != ' ')
            gfx_text(F_COND12, x + 2, top + 50, c, down ? C_ON_ACC : C_TEXT);
    }
}

static void draw_wheel(void)
{
    int i = 0;
    while (wheel[i] && wheel[i] != slots[at])
        i++;
    for (int k = 0; k < MAX; k++) {
        char c[2] = { slots[k], 0 };
        bool here = k == at;
        gfx_fill(26 + k * 40, 80, 32, 40, here ? C_ACCENT : C_SLOT, 255);
        gfx_text_mid(F_COND20, 26 + k * 40, 32, 108, c, here ? C_ON_ACC : c[0] == '-' ? C_DIM : C_TEXT);
    }
    gfx_fill(111, 130, 18, 22, C_WHEEL, 255); /* the wheel around the slot's sign */
    for (int d = -5; d <= 5; d++) {
        char c[2] = { 0, 0 };
        if (i + d < 0 || i + d >= (int)sizeof wheel - 1)
            continue;
        c[0] = wheel[i + d];
        gfx_text_mid(F_COND14, 111 + d * 19, 18, 146, c, d == 0 ? C_ACCENT : d == 1 || d == -1 ? C_TEXT : C_DIM);
    }
    sign_said(slots[at], 176);
    gfx_text(F_COND12, 12, 204, "KNOB 2 the slot   KNOB 3 its sign   LFO empty", C_DIM);
}

void syllable_draw(void)
{
    char where[16] = "T  / STEP ", shown[MAX + 2] = "'", count[4] = "0/5";
    const char *t = join();
    int n = 0;
    where[1] = (char)('1' + track);
    grid_hex2(where + 10, row);
    while (t[n])
        shown[n + 1] = t[n], n++;
    shown[n + 1] = 0;
    count[0] = (char)('0' + n);

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND14, 6, 17, "SYLLABLE", C_TITLE);
    gfx_text(F_COND12, 70, 17, where, C_DIM);
    gfx_text(F_COND12, PLAT_W - 6 - gfx_text_w(F_COND12, mode == KEYS ? "KEYS" : "WHEEL"), 17,
             mode == KEYS ? "KEYS" : "WHEEL", C_TEXT);
    gfx_fill(0, 21, PLAT_W, 1, C_TRACK, 255);
    gfx_fill(40, 26, 160, 40, C_WELL, 255);
    gfx_text_mid(F_COND20, 40, 160, 56, shown, C_TEXT);
    gfx_text(F_COND12, 206, 56, count, C_DIM);
    if (mode == KEYS)
        draw_keys();
    else
        draw_wheel();
    hint(6, "SEL", "TAKE", false);
    hint(82, "PRESET", mode == KEYS ? "WHEEL" : "KEYS", false);
    hint(PLAT_W - 6, "HOME", "BACK", true);
}
