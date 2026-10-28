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

/* ============================================================== VOX ===
 * The formant voice: moondaw's instruments/vox.c, every number of it,
 * in integers. A pulse source through three formant resonators (styles
 * 0 GLIDE, 1 HARD, 2 SOFT) or through a Klatt cascade (3 MELO, 4 SINGR),
 * phoneme targets reached by linear glides. It sings a SYLLABLE in the
 * slot notation of moondaw's FORMAT.md: consonants at the note-on, the
 * first plain vowel held while the note is, the rest at the note-off.
 *
 * Not a band-limited step engine: it computes its samples, and they are
 * added to the track's after the buffer is read. Signals are Q28 (Q24
 * in the cascade), coefficients Q30. */
#define VOX_SYL_LEN 6 /* 5 phoneme characters + NUL */
#define VOX_MAX_SEGS 16

typedef struct {
    uint8_t ms;      /* duration; the nucleus is held instead */
    uint8_t nasal;   /* opens the nasal branch, SINGR only */
    int16_t voice;   /* voiced-source amplitude, Q14 */
    uint16_t f[3];   /* formants, Hz Q2 */
    int16_t a[3];    /* their amplitudes, Q14 */
    uint16_t ncf;    /* noise band centre, Hz Q2 */
    uint16_t nbw;    /* and width, Hz */
    uint16_t na;     /* its amplitude, Q16 */
    uint16_t nflat;  /* unfiltered noise beside the band, Q15 */
} VoxSeg;

typedef struct {
    int32_t a1, a2, g; /* Q30 */
} VoxRes;

typedef struct {
    char syl[VOX_SYL_LEN];
    bool tied, resyl, playing, held, allow_vib;
    uint8_t mode, tract, accent, note;
    int16_t cents;
    uint32_t inc;    /* the note's phase step, Q32 of a cycle */
    int32_t f0;      /* Hz Q16 */
    int32_t w;       /* how much of the sinc train, Q15 */
    int32_t bws;     /* bandwidth widening, Q16 */

    VoxSeg segs[VOX_MAX_SEGS];
    int8_t nsegs, nucleus, seg;
    int32_t pos, seglen;
    int16_t coef_left;

    VoxRes fr[3], nr;
    int32_t fy1[3], fy2[3];
    int32_t ny1a, ny2a, ny1b, ny2b, pw;
    uint32_t phase, rng;
    int32_t glot;
    int32_t ring[64];
    uint8_t ring_pos;

    /* The cascade styles. */
    int32_t ka[5], kb[5], kc[5], ky1[5], ky2[5];
    int32_t npa, npb, npc, npy1, npy2;
    int32_t nza, nzb, nzc, nzx1, nzx2; /* Q16: the zero's gain is large */
    int32_t f1c, f2c, f3c, zfc, f0c;   /* Hz Q16 */
    int32_t voicec, nasalc;            /* Q30 */
    int32_t prev_raw;
    uint32_t vib_phase, t_note;
} Vox;

void vox_init(Vox *v);
/* Silences, keeping the settings and the syllable. */
void vox_reset(Vox *v);
void vox_set_timbre(Vox *v, uint8_t mode);
void vox_set_cents(Vox *v, int cents);
/* As moondaw's vox_set_syllable and vox_set_voice: called right before
 * the note-on. Tied, the vowel carries on unless the step WROTE a
 * syllable, which is then sung. */
void vox_set_syllable(Vox *v, const char *syl, bool tied, bool written);
void vox_set_voice(Vox *v, uint8_t tract, uint8_t accent, bool allow_vib);
void vox_note_on(Vox *v, uint8_t note);
void vox_note_off(Vox *v);
/* Adds `frames` samples, Q15, into out. */
void vox_render(Vox *v, int32_t *out, int frames);
/* Milliseconds of the unvoiced head of a syllable (what a singer places
 * before the beat), and of what follows its nucleus plus the vowel's
 * closing tail (0 when it ends on the vowel). Every length in the
 * engine is a whole number of milliseconds. */
int vox_onset_ms(const char *syl, uint8_t accent);
int vox_coda_ms(const char *syl);

#endif
