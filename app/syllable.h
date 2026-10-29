/* SPDX-License-Identifier: GPL-3.0-only */
/* Writing a VOX syllable, as tried on the panel on 29/10
 * (tools/protos/syllables.py): two ways to the same syllable, PRESETS
 * turning from one to the other as the hand is at ease.
 *
 *   KEYS   the keyboard types a sign a key: consonants on the white keys in
 *          their QWERTY order, vowels on the black; OCT+ or OCT- the other
 *          layer (Q H X +, and the rarer vowels); LFO rubs the last sign out,
 *          ARP marks the syllable weak (.)
 *   WHEEL  five slots: KNOB 1 the slot, KNOB 2 turns its sign through the
 *          alphabet's order (letters, digits, + .), its sound written below;
 *          LFO empties the slot
 *
 * Each change is sung on the step's note by its instrument, PLAY sings it
 * again; SEL keeps it, GLO goes back without it. The alphabet is the
 * format's (moondaw's FORMAT.md, Syllables). */
#ifndef SYLLABLE_H
#define SYLLABLE_H

#include <stdbool.h>

#include "../platform/platform.h"

/* `text` the syllable there now ("" for none), five signs at most; `instr`
 * and `note` what sings it; `track` and `row` only say where, at the top. */
void syllable_open(const char *text, int instr, uint8_t note, int track, int row);
enum { SYLLABLE_TYPING, SYLLABLE_KEPT, SYLLABLE_LEFT };
/* One frame, with the buttons and keys pressed this frame. */
int syllable_frame(const PlatInput *in, uint32_t pressed, uint32_t keys);
void syllable_draw(void);
/* After SYLLABLE_KEPT: the syllable ("" = none). */
const char *syllable_text(void);

#endif
