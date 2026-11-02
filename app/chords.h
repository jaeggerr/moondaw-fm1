/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CHORDS_H
#define CHORDS_H

#include "../core/song.h"
#include "../platform/platform.h"

/* CHORDS, EDIT held + ARP on PATTERN's notes (01/11, 02/11): the chords of the
 * song's KEY, by section and by degree within one, and what each will do on
 * the step. The list opens silent; PLAY arms it, and from then on what the hand
 * changes sounds.
 *
 *   SELECT, KNOB 1  the chord     KNOB 2  the section
 *   KNOB 3  which note is lowest, up to as many as the chord has
 *   KNOB 4  the arpeggio: -- all at once, 01 to 0F ticks a note (FX_ARP on the
 *           step); a step with no effect slot left says FX FULL and stays --
 *   OCT- OCT+  the keyboard's octave, which the chord is built in
 *   the white keys  by position, whatever the key: the first plays degree I of the
 *         section, the seventh VII, the next seven the same an octave up, the last
 *         two nothing. It sounds while held, armed or not; lit where the section
 *         has a chord on that degree, and the cursor goes to it
 *   OP1-OP5  the section, the one in force lit
 *   SEL  writes it on the step      HOME or ARP  leaves, nothing written
 *   OP1-OP5, KNOB 3 and KNOB 4 are what the chord mode below plays with: the
 *         list is where one sets, the keyboard where one writes
 *   PLAY  listening, on or off: on, the chord under the cursor rings, and every
 *         chord, inversion, arpeggio or octave the hand reaches takes over at once;
 *         the song playing, it is stopped first, as CHOOSE INSTRUMENT has it
 *
 * A step holds four notes, so a chord is four at most: the name and the row
 * never promise more than is written. The order is fixed, the key's own and
 * not the phrase's so far: the same turn always finds the same chord. */

/* `oct` is the keyboard's octave, kept by the caller and moved from here;
 * `st` what the step holds now, for the arpeggio it already has. It opens on
 * the section the chord mode stands in (below). */
void chords_open(const Song *s, int instr, int *oct, const Step *st);
enum { CHORDS_OPEN, CHORDS_KEPT, CHORDS_LEFT };
/* One frame, with the buttons and keys pressed this frame. */
int chords_frame(const PlatInput *in, uint32_t pressed, uint32_t keys);
void chords_draw(void);
/* The buttons of its own, for the guide (ui.h). */
uint32_t chords_buttons(void);
/* After CHORDS_KEPT, and after chords_mode_keys: the notes (how many), the
 * arpeggio's ticks, 0 for none. */
int chords_notes(uint8_t out[STEP_MAX_NOTES]);
int chords_arp(void);

/* THE CHORD MODE, PATTERN's ARP lit (02/11). The list is lovely to look at and
 * to listen to, and useless to write with: a chord taken is a chord that closes
 * it, so a progression cost opening it, walking to the chord and leaving, over
 * and over ("c'est joli d'avoir l'ecran avec la liste des accords mais tu peux
 * juste les ecouter"). So the two halves come apart. ARP is a toggle: lit, the
 * keyboard *is* the chords, written straight onto the step under PATTERN's own
 * cursor, no screen in the way; EDIT held + ARP opens the list, where the
 * section, the inversion and the arpeggio are set. Not a long press, which is
 * the quarter of a second nobody can feel that EDIT_TAP died of on 31/10 -- what
 * tells two gestures apart here is the other hand, as EDIT + OP5 does.
 *
 *   the white keys  by position, the section's chord on that degree: the first
 *         plays I, the seventh VII, the next seven the same an octave up. It
 *         sounds as it is written, and lit where the section has one there
 *   OP1-OP5  the section, the one in force lit, its name in the corner
 *   the inversion and the arpeggio  the list's last, as left there
 *
 * One white key writes the whole step -- a chord is the step, so the column the
 * cursor sits in has nothing to say about it -- and the cursor stays where it
 * is: a chord one got wrong is a chord one wants to write over, not to step
 * past ("on peut avoir fait une erreur je prefere qu'on controle la ligne
 * manuellement", 2/11). With REC armed the same key records the chord at the
 * playhead, which the list could never do.
 *
 * It cannot be on with EDIT's caps lock, which would make the black keys both
 * the sections and the clipboard; the one pressed last wins. */
void chords_mode_bind(const Song *s, int instr, int *oct);
/* One frame of it, the keys pressed and the keys down. It sounds what the hand
 * reaches; true where a key set a chord, for the caller to write it. */
bool chords_mode_keys(uint32_t pressed, uint32_t down);
void chords_mode_leds(void);
const char *chords_mode_word(void); /* the section in force, for the corner */

#endif
