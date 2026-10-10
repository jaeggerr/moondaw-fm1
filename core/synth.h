/* SPDX-License-Identifier: GPL-3.0-only */
/* The three chip engines -- PULSE, NOISE, WAVE -- and the band-limited
 * buffer they draw into.
 *
 * moondaw gives every voice its own blip_buf and sums floats. Here the
 * voices of a track all add their steps into ONE buffer: band-limited
 * synthesis is linear, so the sum is the same sound, and a track costs
 * 600 bytes instead of 20 voices x 3 engines x 4 KB. Integers only. */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdbool.h>
#include <stdint.h>

#define SYNTH_FS       44100
#define SYNTH_MAX_FRAMES 128 /* one render call, one I2S half */
#define SYNTH_BUF_EXTRA 18

typedef struct {
    uint32_t offset;                 /* part of a sample carried over, in 1/40000 */
    int32_t integrator;
    int32_t buf[SYNTH_MAX_FRAMES + SYNTH_BUF_EXTRA];
    int32_t clocks;                  /* of the frame being drawn */
} Blep;

typedef struct {
    uint8_t engine;  /* ENGINE_*; one that does not sound stays silent */
    uint8_t timbre;
    uint8_t note;
    bool playing;
    int16_t cents;
    uint16_t gain;   /* Q12, 4096 = unity: a layer's level */
    int32_t level;   /* what this voice last put in the buffer */
    int32_t until;   /* clocks before its next edge */
    int32_t a, b;    /* pulse: high and low clocks; wave, noise: a = step */
    uint16_t lfsr;
    uint8_t idx;     /* wave: position in the 32 steps */
} Osc;

void blep_reset(Blep *b);
/* A frame: begin, let every voice draw, then read `frames` samples.
 * The samples are Q15 (32768 = full scale) and NOT clamped -- twenty
 * voices add up, and the chain downstream has the headroom. */
void blep_begin(Blep *b, int frames);
void blep_read(Blep *b, int32_t *out, int frames);

void osc_init(Osc *o);
void osc_select(Osc *o, Blep *b, uint8_t engine); /* silences on a change */
void osc_set_timbre(Osc *o, uint8_t timbre);
void osc_set_cents(Osc *o, int cents);
void osc_note_on(Osc *o, uint8_t note);
void osc_note_off(Osc *o, Blep *b);
void osc_draw(Osc *o, Blep *b); /* between blep_begin and blep_read */

#endif
