/* SPDX-License-Identifier: GPL-3.0-only */
/* The pieces every screen draws the same way: the four cards of KNOB
 * 1-4 under the bar, and the twelve buttons at their place. One
 * implementation, so a correction lands on every screen at once. */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

enum { UI_CARDS_Y = 22, UI_CARDS_H = 22 };

char *ui_hex2(char *b, int v);
char *ui_dec(char *b, int v);

/* Card k of four. A null label is a knob that does nothing here: the
 * card keeps its place and says so by being empty. */
void ui_card(int k, const char *label, const char *value, const char *unit);
void ui_cards_rule(void); /* the green line under them */
/* The loop's steps in the bar, `LOOP 03-08`, at x. Returns its width,
 * 0 when there is no loop to say. */
int ui_loop_slot(int x, int y);

/* The guide (29/10): the buttons that do something on the screen in
 * front glow faintly, a lit LED still meaning a state (PLAY playing).
 * `usable` is a PB_* mask; the screens that have none yet give 0. On
 * until a SETTINGS screen can turn it off. */
extern bool ui_guide;
/* Lit rather than glowing (29/10, a trial): the glow did not read on the
 * white LEDs. PLAY is then left out: lit is its state. */
extern bool ui_guide_lit;
void ui_led_guide(uint32_t usable);
/* The LEDs were written behind the guide's back (the link's LED): the
 * next ui_led_guide writes them all again. */
void ui_led_guide_again(void);

/* What a button of the map is, this frame. */
enum {
    UI_BTN_NONE,  /* does nothing on this screen: an empty cell */
    UI_BTN_OFF,   /* named, and not available now (or not written yet) */
    UI_BTN_ON,
    UI_BTN_BACK,  /* on, and it names where it leads: cyan */
    UI_BTN_LIVE,  /* something is running: green */
    UI_BTN_ASK,   /* a question waits on it: red */
    UI_BTN_HERE,  /* a state of the screen is on, or waits for its other half: cyan */
};
/* `held` is PlatInput.btns: a button that is on shows green while down. */
void ui_btn_map(const char *const word[12], const uint8_t state[12], uint32_t held);

#endif
