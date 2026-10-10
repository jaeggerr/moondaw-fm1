/* SPDX-License-Identifier: GPL-3.0-only */
/* The band-limited step is blip_buf's, by Shay Green (blargg): the
 * kernel table below is copied from blip_buf 1.1.0, and blep_add is its
 * blip_add_delta in 32 bits.
 *
 *   blip_buf 1.1.0. http://www.slack.net/~ant/
 *   Library Copyright (C) 2003-2009 Shay Green. This library is free
 *   software; you can redistribute it and/or modify it under the terms
 *   of the GNU Lesser General Public License as published by the Free
 *   Software Foundation; either version 2.1 of the License, or (at your
 *   option) any later version.
 *
 * The engines follow moondaw's pulse.c, noise.c and wave.c edge for
 * edge: same 4 MHz clock, same periods, same LFSR, same tables. */
#include "synth.h"

#include "song.h"
#include "tables.h"

/* The clock is 4 MHz and a sample is 1/44100 s: 40000 clocks are 441
 * samples, exactly. Positions are counted in 1/40000 of a sample, so
 * nothing is rounded and the pitch never drifts against moondaw's. */
#define CLK_NUM     441u   /* 1/40000 samples per clock */
#define CLK_DEN     40000u
#define PHASE_BITS  5
#define PHASE_COUNT (1 << PHASE_BITS)
#define HALF_WIDTH  8
/* The buffer holds sample << 10. blip_buf holds << 15 and clamps each
 * voice to 16 bits; five bits are given to the sum of a whole track. */
#define SUM_SHIFT   10

/* Sinc_Generator( 0.9, 0.55, 4.5 ) */
static const int16_t bl_step[PHASE_COUNT + 1][HALF_WIDTH] = {
    { 43, -115, 350, -488, 1136, -914, 5861, 21022 },
    { 44, -118, 348, -473, 1076, -799, 5274, 21001 },
    { 45, -121, 344, -454, 1011, -677, 4706, 20936 },
    { 46, -122, 336, -431, 942, -549, 4156, 20829 },
    { 47, -123, 327, -404, 868, -418, 3629, 20679 },
    { 47, -122, 316, -375, 792, -285, 3124, 20488 },
    { 47, -120, 303, -344, 714, -151, 2644, 20256 },
    { 46, -117, 289, -310, 634, -17, 2188, 19985 },
    { 46, -114, 273, -275, 553, 117, 1758, 19675 },
    { 44, -108, 255, -237, 471, 247, 1356, 19327 },
    { 43, -103, 237, -199, 390, 373, 981, 18944 },
    { 42, -98, 218, -160, 310, 495, 633, 18527 },
    { 40, -91, 198, -121, 231, 611, 314, 18078 },
    { 38, -84, 178, -81, 153, 722, 22, 17599 },
    { 36, -76, 157, -43, 80, 824, -241, 17092 },
    { 34, -68, 135, -3, 8, 919, -476, 16558 },
    { 32, -61, 115, 34, -60, 1006, -683, 16001 },
    { 29, -52, 94, 70, -123, 1083, -862, 15422 },
    { 27, -44, 73, 106, -184, 1152, -1015, 14824 },
    { 25, -36, 53, 139, -239, 1211, -1142, 14210 },
    { 22, -27, 34, 170, -290, 1261, -1244, 13582 },
    { 20, -20, 16, 199, -335, 1301, -1322, 12942 },
    { 18, -12, -3, 226, -375, 1331, -1376, 12293 },
    { 15, -4, -19, 250, -410, 1351, -1408, 11638 },
    { 13, 3, -35, 272, -439, 1361, -1419, 10979 },
    { 11, 9, -49, 292, -464, 1362, -1410, 10319 },
    { 9, 16, -63, 309, -483, 1354, -1383, 9660 },
    { 7, 22, -75, 322, -496, 1337, -1339, 9005 },
    { 6, 26, -85, 333, -504, 1312, -1280, 8355 },
    { 4, 31, -94, 341, -507, 1278, -1205, 7713 },
    { 3, 35, -102, 347, -506, 1238, -1119, 7082 },
    { 1, 40, -110, 350, -499, 1190, -1021, 6464 },
    { 0, 43, -115, 350, -488, 1136, -914, 5861 },
};

void blep_reset(Blep *b)
{
    b->offset = CLK_NUM / 2;
    b->integrator = 0;
    b->clocks = 0;
    for (int i = 0; i < SYNTH_MAX_FRAMES + SYNTH_BUF_EXTRA; i++)
        b->buf[i] = 0;
}

void blep_begin(Blep *b, int frames)
{
    uint32_t needed = (uint32_t)frames * CLK_DEN;
    b->clocks = needed < b->offset
                    ? 0
                    : (int32_t)((needed - b->offset + CLK_NUM - 1) / CLK_NUM);
}

/* A step of `delta` at clock `time` of the frame. |delta| stays under
 * 2^16 (a voice is 16000 at most, a layer four times that), which is
 * what keeps every product below in 32 bits. */
static void blep_add(Blep *b, uint32_t time, int32_t delta)
{
    uint32_t at = time * CLK_NUM + b->offset;
    uint32_t fr = (at % CLK_DEN) * PHASE_COUNT; /* fraction of a sample, x32 */
    int32_t *out = b->buf + at / CLK_DEN;
    uint32_t phase = fr / CLK_DEN;
    const int16_t *in = bl_step[phase], *in2 = bl_step[phase + 1];
    const int16_t *rev = bl_step[PHASE_COUNT - phase],
                  *rev2 = bl_step[PHASE_COUNT - phase - 1];
    int32_t interp = (int32_t)((fr % CLK_DEN) * 4096u / CLK_DEN); /* Q12 */
    int32_t d2 = (delta * interp) >> 12;
    int32_t d1 = delta - d2;

    for (int i = 0; i < HALF_WIDTH; i++) {
        out[i] += (in[i] * d1 + in2[i] * d2) >> (15 - SUM_SHIFT);
        out[15 - i] += (rev[i] * d1 + rev2[i] * d2) >> (15 - SUM_SHIFT);
    }
}

void blep_read(Blep *b, int32_t *out, int frames)
{
    int32_t sum = b->integrator;
    int i;

    b->offset = (uint32_t)b->clocks * CLK_NUM + b->offset - (uint32_t)frames * CLK_DEN;
    for (i = 0; i < frames; i++) {
        int32_t s = sum >> SUM_SHIFT;
        sum += b->buf[i];
        out[i] = s;
        sum -= (sum >> 15) << 6; /* the high-pass: blip_buf's bass_shift 9 */
    }
    b->integrator = sum;
    for (i = 0; i < SYNTH_BUF_EXTRA; i++)
        b->buf[i] = b->buf[i + frames];
    for (; i < SYNTH_MAX_FRAMES + SYNTH_BUF_EXTRA; i++)
        b->buf[i] = 0;
}

/* ---------------------------------------------------------- engines --- */
#define PULSE_AMP 16000
#define NOISE_AMP 12000
#define NOISE_MIN_STEP 181 /* 4 MHz / (FS / 2): above that it is not noise */

/* Clocks per cycle of `note` bent by `cents`, Q8, 1 Hz at the lowest. */
static uint32_t cycle_q8(int note, int cents)
{
    int32_t t = note * 100 + cents, oct = 0;
    uint32_t p;
    while (t < 0) {
        t += 1200;
        oct--;
    }
    while (t >= 1200) {
        t -= 1200;
        oct++;
    }
    p = PERIOD_Q12[t];
    if (oct < -4)
        return 0x3D090000u; /* 4e6 << 8: the 1 Hz floor, as moondaw's */
    p >>= oct > 27 ? 31 : oct + 4;
    return p > 0x3D090000u ? 0x3D090000u : p;
}

static int32_t scaled(const Osc *o, int32_t delta)
{
    return (delta * (int32_t)o->gain) >> 12;
}

static void recompute(Osc *o)
{
    uint32_t c = cycle_q8(o->note, o->cents);
    if (o->engine == ENGINE_PULSE) {
        static const uint8_t eighths[4] = { 1, 2, 4, 6 };
        int32_t high = (int32_t)(((c >> 3) * eighths[o->timbre & 3]) >> 8);
        int32_t low;
        if (high < 1)
            high = 1;
        low = (int32_t)(c >> 8) - high;
        o->a = high;
        o->b = low < 1 ? 1 : low;
    } else {
        /* 32 steps to the cycle for a wave. The noise register shifts 32
         * times per cycle of the written note: that is its "five octaves
         * above", and the same division. */
        int32_t step = (int32_t)(c >> 13);
        if (o->engine == ENGINE_NOISE && step < NOISE_MIN_STEP)
            step = NOISE_MIN_STEP;
        o->a = step < 1 ? 1 : step;
    }
}

void osc_init(Osc *o)
{
    o->engine = ENGINE_PULSE;
    o->timbre = 2;
    o->note = 60;
    o->playing = false;
    o->cents = 0;
    o->gain = 4096;
    o->level = 0;
    o->until = 0;
    o->a = o->b = 1;
    o->lfsr = 1;
    o->idx = 0;
}

void osc_note_off(Osc *o, Blep *b)
{
    if (o->level != 0) {
        blep_add(b, 0, -o->level);
        o->level = 0;
    }
    o->playing = false;
}

void osc_select(Osc *o, Blep *b, uint8_t engine)
{
    if (engine == o->engine)
        return;
    osc_note_off(o, b);
    o->engine = engine;
    o->until = 0;
    o->idx = 0;
    o->timbre = engine == ENGINE_PULSE ? 2 : 0;
}

void osc_set_timbre(Osc *o, uint8_t timbre)
{
    if (o->engine == ENGINE_PULSE)
        timbre &= 3;
    else if (o->engine == ENGINE_NOISE)
        timbre &= 1;
    else if (timbre > 4)
        timbre = 0;
    if (timbre == o->timbre)
        return;
    o->timbre = timbre;
    if (o->engine == ENGINE_PULSE)
        recompute(o);
}

void osc_set_cents(Osc *o, int cents)
{
    if (cents == o->cents)
        return;
    o->cents = (int16_t)cents;
    recompute(o);
}

void osc_note_on(Osc *o, uint8_t note)
{
    o->note = note;
    recompute(o);
    if (!o->playing) {
        if (o->engine == ENGINE_PULSE)
            o->level = 0;
        if (o->engine == ENGINE_WAVE)
            o->idx = 0;
        o->until = 1; /* the first edge at once */
        o->playing = true;
    }
}

void osc_draw(Osc *o, Blep *b)
{
    int32_t clocks = b->clocks, t = 0;

    if (!o->playing || !engine_sounds(o->engine))
        return;
    switch (o->engine) {
    case ENGINE_PULSE:
        while (t + o->until < clocks) {
            int32_t amp = scaled(o, PULSE_AMP);
            t += o->until;
            blep_add(b, (uint32_t)t, o->level ? -o->level : amp);
            o->level = o->level ? 0 : amp;
            o->until = o->level ? o->a : o->b;
        }
        break;
    case ENGINE_NOISE:
        while (t + o->until < clocks) {
            unsigned tap = o->timbre ? 6u : 1u;
            unsigned fb = (o->lfsr ^ (o->lfsr >> tap)) & 1u;
            int32_t lvl;
            t += o->until;
            o->lfsr = (uint16_t)((o->lfsr >> 1) | (fb << 14));
            lvl = o->lfsr & 1u ? scaled(o, NOISE_AMP) : 0;
            if (lvl != o->level) {
                blep_add(b, (uint32_t)t, lvl - o->level);
                o->level = lvl;
            }
            o->until = o->a;
        }
        break;
    default: /* ENGINE_WAVE */
        while (t + o->until < clocks) {
            int32_t lvl;
            t += o->until;
            o->idx = (uint8_t)((o->idx + 1) & 31);
            lvl = scaled(o, WAVE_SHAPES[o->timbre][o->idx]);
            if (lvl != o->level) {
                blep_add(b, (uint32_t)t, lvl - o->level);
                o->level = lvl;
            }
            o->until = o->a;
        }
        break;
    }
    o->until -= clocks - t;
}
