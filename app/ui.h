/* SPDX-License-Identifier: GPL-3.0-only */
/* The pieces every screen draws the same way: the four cards of KNOB
 * 1-4 under the bar, and the twelve buttons at their place. One
 * implementation, so a correction lands on every screen at once. */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/platform.h"

enum { UI_CARDS_Y = 22, UI_CARDS_H = 22 };

char *ui_hex2(char *b, int v);
char *ui_dec(char *b, int v); /* 12 bytes hold any int */

/* Card k of four. A null label is a knob that does nothing here: the
 * card keeps its place and says so by being empty. */
void ui_card(int k, const char *label, const char *value, const char *unit);
void ui_cards_rule(void); /* the green line under them */
/* The loop's steps in the bar, `LOOP 03-08`, at x. Returns its width,
 * 0 when there is no loop to say. */
int ui_loop_slot(int x, int y);

/* The guide (29/10): the buttons that do something on the screen in
 * front light, a lit LED still meaning a state (PLAY playing). HOME,
 * SAVE and SEL never do: the same on every screen, or a way through them;
 * nor GLO where it only goes back -- the screens leave it out then. Every
 * screen gives its own once a frame; HOME and the canvas have none.
 * `usable` is a PB_* mask.
 *
 * EDIT is not lit either, for the same reason as SEL: it is a shift, the
 * same on every screen that has it, not a feature of the one in front. The
 * exception is the screens where a quick tap locks its mode on (PATTERN's
 * and SONG's SELECTION): there the light says the mode is on, as a CAPS
 * LOCK's does, and it goes out when the mode does (30/10).
 *
 * SETTINGS says how much of it: LEDS OFF (none, PLAY's state either),
 * STATE (PLAY's alone), GUIDE (the default); and how bright the guide
 * is, ui_light a PLAT_LED_* level -- 50 % by default: the glow is worse
 * than dark on the white LEDs, the labels drown in it (29/10). PLAY (and
 * REC, later) are apart: they light only for their state. */
enum { UI_LEDS_OFF, UI_LEDS_STATE, UI_LEDS_GUIDE };
extern int ui_leds, ui_light;
/* SETTINGS' COUNT-IN: bars REC counts before it records, 0 to 2; 1 by default. */
enum { UI_COUNT_MAX = 2 };
extern int ui_count_in;
void ui_led_guide(uint32_t usable);
/* A state's LED (PLAY playing), unless LEDS is OFF. */
void ui_led_state(int btn, bool on);
/* The LEDs were written behind the guide's back (the link's LED): the
 * next ui_led_guide writes them all again. */
void ui_led_guide_again(void);
/* The same for the keys (bit k = key k), where the keyboard does
 * something else than notes: PATTERN's OP keys while EDIT has them.
 * `lit`: a key's state, lit as PLAY's is (MONO while the track in front
 * plays alone), unless LEDS is OFF. */
void ui_led_keys(uint32_t usable, uint32_t lit, uint32_t half);
/* The black keys read OP1-6, PIT, GLO, MONO, POLY. The commands EDIT gives
 * them are the same wherever a screen has them (PATTERN's steps, SONG's
 * blocks): the clipboard in the first three, clearing in OP4, listening in
 * OP6 (29/10, NOTES.md). */
enum {
    UI_OP_COPY = 1,
    UI_OP_PASTE = 3,
    UI_OP_CUT = 5,
    UI_OP_CLEAR = 8,
    UI_KEY_LOOP = 13,
    UI_KEY_MONO = 20
};

/* EDIT as a shift (29/10), shared by the screens where a track is in front
 * (PATTERN, SONG, SECTION, MIXER): while EDIT is down the black keys are
 * commands; MONO tapped puts the track alone (solo, cumulative), or back to
 * normal from solo or mute; MONO held MONO_HOLD frames mutes it. A tap of
 * EDIT with no key meanwhile is the screen's own EDIT, given at the
 * release. `track` -1: none in front.
 *
 * The screens where EDIT locks a mode on (PATTERN's and SONG's SELECTION)
 * ask for the tap to be a quick one as well, `frames < EDIT_TAP`: held
 * down, EDIT is the shift and nothing else, and letting it go after a
 * long press leaves the mode as it was -- a quick tap locks it, as a CAPS
 * LOCK, and another takes it off (30/10). MIXER and INSTRUMENT, where the
 * tap only goes to the PROJECT, take it however long it was. */
typedef struct {
    bool held, used;  /* EDIT down; a key used while it was */
    int frames;       /* frames EDIT has been down, read at the release */
    int mono;         /* frames MONO has been down, 0 when up */
    bool muted;       /* this MONO press already muted */
} UiShift;
enum { MONO_HOLD = 30, EDIT_TAP = 15 }; /* 0.5 s; a tap at most 0.25 s */
/* One frame, with the buttons and keys pressed this frame; `ops` true
 * where the keys are commands anyway (PATTERN's SELECTION). True when
 * EDIT was tapped. `*said` the corner's word when MONO acted. */
bool ui_shift(UiShift *s, const PlatInput *in, uint32_t pressed, uint32_t keys, bool ops, int track,
              const char **said);
/* MONO's LED for the track in front: lit alone, half muted. */
void ui_led_mono(int track, uint32_t usable);

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
