/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample voice: a one-shot of the bank (core/bank.h) played straight
 * out of the flash, decoded as it goes, resampled to the note.
 *
 * A sample is mono IMA ADPCM at SMP_RATE, four bits a frame, cut in
 * blocks of SMP_BLOCK bytes: a predictor (int16, little-endian) and a
 * step index, then SMP_BLOCK_FRAMES - 1 codes, the low nibble first. A
 * block starts afresh, so a voice can begin on any of them, and the
 * bank lays them out so that none crosses one of the store's sectors.
 *
 * The voice holds no sound in RAM: a pointer into the flash, the
 * decoder's two numbers and the last two frames, ~40 bytes. The sector
 * table it reads through is the bank's, set from the main loop
 * (smp_attach), and every voice dies at smp_hold(): a bank about to be
 * rewritten is never read again. Freestanding: no libc, no float. */
#ifndef SAMPLER_H
#define SAMPLER_H

#include <stdbool.h>
#include <stdint.h>

#include "song.h"

#define SMP_RATE         22050
#define SMP_BLOCK        255 /* 4080, a sector's payload, is 16 of them */
#define SMP_BLOCK_FRAMES 505 /* the predictor, then 252 bytes of codes */
#define SMP_MAX_SECTORS  80  /* the store has 73 */

typedef struct {
    const uint8_t *p;  /* the next byte of codes */
    uint32_t at;       /* the next block: its place in the bank's payload */
    uint32_t left;     /* frames not yet decoded */
    uint32_t frac;     /* Q16, between s0 and s1 */
    uint32_t inc;      /* Q16 frames of the sample a frame out */
    int32_t gain;      /* Q12: the zone's gain, and the level */
    int16_t s0, s1, pred;
    uint16_t codes;    /* left in the block */
    int16_t cents;     /* the note from the root, the zone's tune, the track's bends */
    int16_t base;      /* the first two, cents */
    uint8_t idx, odd, gen, tail;
    bool playing;
} SmpVoice;

/* -------------------------------------------------- the main loop --- */
/* Every voice stops at its next render and nothing of the bank is read
 * until smp_attach. Before any change to the store's bank slot. */
void smp_hold(void);
/* The bank's sectors, in file order: where each one's payload starts,
 * readable in place. */
void smp_attach(const uint8_t *const *payloads, int n);
/* Entry `i` of the song's table plays the bank's sample whose first
 * block is at payload position `at`, `frames` long; 0 frames: silent. */
void smp_map(int i, uint32_t at, uint32_t frames);

/* --------------------------------------------------- the audio side --- */
void smp_init(SmpVoice *v);
/* Entry `smp` of the table at `note`, `root` the note it sounds as
 * recorded, `tune` cents; `gain_q16` its level. False when the entry
 * has no sound: the voice stays silent. */
bool smp_start(SmpVoice *v, int smp, uint8_t note, uint8_t root, int tune, uint32_t gain_q16);
void smp_set_cents(SmpVoice *v, int cents);
void smp_stop(SmpVoice *v);
/* Adds `frames` samples, Q15, into out. */
void smp_render(SmpVoice *v, int32_t *out, int frames);

/* ------------------------------------------------- the player's pool ---
 * SMP_VOICES shared by every track, lent to a note when it is struck and
 * kept until it has rung out or another note needs it: a free one first,
 * then one that has finished, then the one struck longest ago. A slot is
 * the player's (its Voice's), -1 for none, cleared here when its voice
 * is taken back. Kept out of the player, whose code runs from RAM on the
 * FM-1 and has no room left: these run from the flash, as DRUM's. */
#define SMP_VOICES 16

void smp_pool_init(void);
/* Note `note` of instrument `inst` of `s`: the step's fSMP entry when
 * fsmp >= 0 (rooted at C-4 over the whole keyboard, FORMAT.md), else the
 * first of the instrument's zones that holds the note. Nothing to play
 * leaves the slot empty. */
void smp_strike(int8_t *slot, const Song *s, int inst, int fsmp, uint8_t note);
void smp_free(int8_t *slot);
void smp_cents(int8_t slot, int cents);
void smp_mix(int8_t slot, int32_t *out, int frames);
bool smp_sounds(int8_t slot); /* reading the flash */

/* ---------------------------------------------------------- codec --- */
/* One code into the next frame: the decoder the voice runs, for the
 * tests and anything that reads the bank back. */
int16_t smp_decode(int16_t *pred, uint8_t *idx, uint8_t code);

#endif
