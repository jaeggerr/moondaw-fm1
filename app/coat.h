/* SPDX-License-Identifier: GPL-3.0-only */
/* The COAT (02/11): the help worn ON the screen instead of opened beside
 * it. OCT- and OCT+ together switch it on, and it stays on until the same
 * chord switches it off -- the one chord the panel has left, every button
 * but EDIT acting on its press (app/ui.h). The two are read before any
 * screen sees the frame and rubbed out of it; pressed together they move
 * the keyboard's octave in opposite directions and by nothing anyway.
 *
 * It takes the FOOT of the panel, two rows of 28, and gives back what the
 * twelve buttons do UNDER THE CURSOR THAT IS THERE NOW -- not a list of
 * what the screen can do somewhere. The two rows are the machine's own
 * two rows of six (FX SEL ENV LFO EDIT GLO / HOME SAVE ARP SEQ PLAY REC),
 * so the cell read sits where the button lies under the thumb: nothing to
 * look up. A dark cell does nothing here, and teaches the panel as fast
 * as a lit one.
 *
 * Words are indexed by PB_*, which IS the panel's order for the first
 * twelve. NULL leaves the cell dark. COAT_CHARS is what fits: a word
 * wider than its cell is drawn narrower rather than into its neighbour,
 * and an abbreviation nobody reads is worse than a dark cell -- so prefer
 * dropping the cell to shortening the word past sense.
 *
 * While EDIT is held the twelve are not what the hand is about to press,
 * so the foot gives the BLACK KEYS instead (app/ui.h's OP keys), and the
 * one thing EDIT answers to on every screen: SELECT, the listening speed.
 *
 * Only SONG wears it today. A screen that does not call coat_draw keeps
 * its eight rows and shows nothing, switched on or not. */
#ifndef COAT_H
#define COAT_H

#include <stdbool.h>

#include "../platform/platform.h"

enum {
    COAT_BTNS = 12,  /* PB_FX .. PB_REC: OCT- and OCT+ live by the knobs */
    COAT_ROWS = 2,   /* grid rows the foot takes from the screen */
    COAT_CHARS = 7,  /* what fits a 40 px cell at CONDENSED 12 */
};

extern bool coat_on;

/* COAT_ROWS while it is worn, 0 otherwise: what a screen takes off its
 * own row count. A screen that does not ask keeps all eight. */
int coat_rows(void);

/* The foot, from `words` indexed by PB_FX .. PB_REC; NULL stays dark.
 * Drawn last, over everything, by the screen that wears it. While EDIT
 * is held it draws the black keys instead and ignores `words`. */
void coat_draw(const char *const *words, bool edit_held);

#endif
