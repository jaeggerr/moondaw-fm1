/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CHORDS_H
#define CHORDS_H

#include "../core/song.h"
#include "../platform/platform.h"

/* CHORDS, ARP on PATTERN's notes (01/11): the chords of the song's KEY, by
 * section and the likeliest first, each heard as the cursor reaches it, and
 * what it will do on the step.
 *
 *   SELECT, KNOB 1  the chord     KNOB 2  the section
 *   KNOB 3  which note is lowest, up to as many as the chord has
 *   KNOB 4  the arpeggio: -- all at once, 01 to 0F ticks a note (FX_ARP on the
 *           step); a step with no effect slot left says FX FULL and stays --
 *   OCT- OCT+  the keyboard's octave, which the chord is built in
 *   SEL  writes it on the step      HOME or ARP  leaves, nothing written
 *   PLAY  the chord once more; the song playing, stops it
 *
 * A step holds four notes, so a chord is four at most: the name and the row
 * never promise more than is written. The order is fixed, the key's own and
 * not the phrase's so far: the same turn always finds the same chord. */

/* `oct` is the keyboard's octave, kept by the caller and moved from here;
 * `st` what the step holds now, for the arpeggio it already has. */
void chords_open(const Song *s, int instr, int *oct, const Step *st);
enum { CHORDS_OPEN, CHORDS_KEPT, CHORDS_LEFT };
/* One frame, with the buttons pressed this frame. */
int chords_frame(const PlatInput *in, uint32_t pressed);
void chords_draw(void);
/* The buttons of its own, for the guide (ui.h). */
uint32_t chords_buttons(void);
/* After CHORDS_KEPT: the notes (how many), the arpeggio's ticks, 0 for none. */
int chords_notes(uint8_t out[STEP_MAX_NOTES]);
int chords_arp(void);

#endif
