/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM: Felucca's drum voices (core/drum_voice.c) as an engine.
 *
 * An instrument is a kit; its TIMBRE picks which (STD, STD with the round
 * kick, or one of Felucca's model kits), and the NOTE picks the drum, on
 * the General MIDI map: C-2 (36) the kick, D-2 the snare, D#2 the clap,
 * F#2 the closed hat, A#2 the open hat, the toms on F G A B C D, and so on
 * to 81; a note outside 35..81 plays its pitch class of 36..47. Its four
 * knobs (TUNE TONE DECAY SNAP, 64 = as designed) move every drum of the
 * kit from its design, as Felucca's own do.
 *
 * Like VOX it computes its samples and adds them to the track's after the
 * band-limited buffer is read. Each drum of a track is one lane, mono: a
 * hit on a lane cuts that lane's last hit, the lanes ring together, a
 * closed hat chokes the open one. The lanes are lent from a pool shared
 * by every track; the hit is the envelope. */
#ifndef DRUM_H
#define DRUM_H

#include <stdbool.h>
#include <stdint.h>

enum { DRUM_TUNE, DRUM_TONE, DRUM_DECAY, DRUM_SNAP, DRUM_KNOBS };
#define DRUM_KITS 10   /* the timbres: STD ROUND 80 10 66 55 77 DEEP TIGHT PERC */
#define DRUM_VOICES 12 /* lanes ringing at once, every track together */

void drum_init(void);
/* A hit of `note` (0..127) on track `track`'s kit, at velocity 0..127;
 * knob[]: offsets from the design, -64..63. Between two renders. */
void drum_hit(int track, uint8_t kit, uint8_t note, uint8_t vel, const int8_t knob[DRUM_KNOBS]);
/* Adds the track's ringing lanes into mono, Q15, `frames` samples. */
void drum_render(int track, int32_t *mono, int frames);
/* Every lane of the track, silent at once; whether any still rings. */
void drum_silence(int track);
bool drum_rings(int track);
/* The kit's name (5 characters at most), and a note's drum as two
 * letters, the drum machine way (BD SD CP CH OH TM RS CB...). */
const char *drum_kit_name(uint8_t kit);
const char *drum_note_abbr(uint8_t kit, uint8_t note);

#endif
