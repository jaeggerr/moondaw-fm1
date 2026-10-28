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
        /* The high-pass, blip_buf's bass_shift 9. blip_buf takes it off in
         * steps of 64 and so leaves up to 31 of DC standing for ever after
         * a note; taken to the unit, the buffer comes back to rest. */
        sum -= sum >> 9;
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

/* ============================================================== VOX ===
 * moondaw's instruments/vox.c, whose comments say why each number is
 * what it is: they were chosen by ear there (proto/formant.c, v9), and
 * are only translated here. Where the float code reads `x * sr`, the
 * lengths below are whole milliseconds and `ms * 441 / 10` samples. */
#define VOX_HOLD (1 << 28)
#define VOX_MODES 5
#define Q14(x) ((int16_t)((x) * 16384.0 + 0.5))
#define Q15(x) ((uint16_t)((x) * 32768.0 + 0.5))
#define Q16(x) ((uint16_t)((x) * 65536.0 + 0.5))
#define SEG(ms, vo, f1, f2, f3, a1, a2, a3, ns, cf, bw, na, fl)                         \
    { ms, ns, Q14(vo), { (f1) * 4, (f2) * 4, (f3) * 4 }, { Q14(a1), Q14(a2), Q14(a3) }, \
      (cf) * 4, bw, Q16(na), Q15(fl) }

static const uint8_t mode_glide_ms[VOX_MODES] = { 40, 0, 80, 0, 0 };
static const uint8_t mode_fade_10[VOX_MODES] = { 40, 10, 150, 40, 40 }; /* 0.1 ms */

static const struct {
    char c;
    VoxSeg s;
} VOWELS[] = {
    { 'A', SEG(0, 1, 700, 1200, 2500, 1, .7, .3, 0, 0, 0, 0, 0) },
    { 'E', SEG(0, 1, 390, 2000, 2600, 1, .8, .35, 0, 0, 0, 0, 0) },
    { '7', SEG(0, 1, 530, 1850, 2500, 1, .8, .3, 0, 0, 0, 0, 0) },
    { 'I', SEG(0, 1, 290, 2200, 2900, 1, .6, .4, 0, 0, 0, 0, 0) },
    { 'O', SEG(0, 1, 380, 750, 2400, 1, .5, .2, 0, 0, 0, 0, 0) },
    { '6', SEG(0, 1, 520, 900, 2450, 1, .6, .25, 0, 0, 0, 0, 0) },
    { 'U', SEG(0, 1, 310, 750, 2300, 1, .5, .2, 0, 0, 0, 0, 0) },
    { 'Y', SEG(0, 1, 290, 1900, 2500, 1, .6, .3, 0, 0, 0, 0, 0) },
    { '2', SEG(0, 1, 500, 1450, 2450, 1, .7, .3, 0, 0, 0, 0, 0) },
    { '9', SEG(0, 1, 550, 1350, 2450, 1, .7, .3, 0, 0, 0, 0, 0) },
    { '3', SEG(0, 1, 620, 950, 2400, 1, .8, .25, 1, 0, 0, 0, 0) },
    { '0', SEG(0, 1, 480, 800, 2400, 1, .6, .2, 1, 0, 0, 0, 0) },
    { '5', SEG(0, 1, 550, 1650, 2450, 1, .8, .3, 1, 0, 0, 0, 0) },
    { '1', SEG(0, 1, 450, 1850, 2550, 1, .7, .35, 0, 0, 0, 0, 0) },
    { '4', SEG(0, 1, 700, 1650, 2450, 1, .8, .3, 0, 0, 0, 0, 0) },
    { '8', SEG(0, 1, 460, 1100, 2400, 1, .55, .2, 0, 0, 0, 0, 0) },
};

/* A stop is a closure and a burst; everything else one segment. */
static const struct {
    char c;
    uint8_t n;
    VoxSeg s[2];
} CONSONANTS[] = {
    { 'P', 2, { SEG(30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), SEG(14, 0, 0, 0, 0, 0, 0, 0, 0, 1400, 1500, .10, 0) } },
    { 'B', 2, { SEG(25, .5, 250, 1450, 2450, .6, 0, 0, 0, 0, 0, 0, 0), SEG(10, 0, 0, 0, 0, 0, 0, 0, 0, 1200, 1500, .10, 0) } },
    { 'T', 2, { SEG(30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), SEG(32, 0, 0, 0, 0, 0, 0, 0, 0, 4500, 2000, .17, 0) } },
    { 'D', 2, { SEG(25, .5, 250, 1450, 2450, .6, 0, 0, 0, 0, 0, 0, 0), SEG(20, 0, 0, 0, 0, 0, 0, 0, 0, 2500, 2000, .12, 0) } },
    { 'K', 2, { SEG(30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), SEG(28, 0, 0, 0, 0, 0, 0, 0, 0, 2000, 1200, .20, 0) } },
    { 'G', 2, { SEG(25, .5, 250, 1450, 2450, .6, 0, 0, 0, 0, 0, 0, 0), SEG(22, 0, 0, 0, 0, 0, 0, 0, 0, 1800, 1000, .34, 0) } },
    { 'F', 1, { SEG(90, 0, 0, 0, 0, 0, 0, 0, 0, 1800, 3000, .028, .58) } },
    { 'V', 1, { SEG(80, .65, 250, 1450, 2450, .6, 0, 0, 0, 1800, 3000, .017, .58) } },
    { 'S', 1, { SEG(100, 0, 0, 0, 0, 0, 0, 0, 0, 5800, 1800, .072, 0) } },
    { 'Z', 1, { SEG(70, .65, 250, 1450, 2450, .6, 0, 0, 0, 5800, 1800, .05, 0) } },
    { 'C', 1, { SEG(100, 0, 0, 0, 0, 0, 0, 0, 0, 3000, 3500, .085, .22) } },
    { 'J', 1, { SEG(90, .65, 250, 1450, 2450, .6, 0, 0, 0, 3000, 3500, .058, .22) } },
    { 'X', 1, { SEG(80, 0, 0, 0, 0, 0, 0, 0, 0, 4000, 2500, .020, .70) } },
    { '+', 1, { SEG(70, .65, 250, 1450, 2450, .6, 0, 0, 0, 4000, 2500, .011, .70) } },
    { 'M', 1, { SEG(70, 1, 250, 1000, 2200, 1, .08, .03, 1, 0, 0, 0, 0) } },
    { 'N', 1, { SEG(70, 1, 250, 1700, 2600, 1, .12, .04, 1, 0, 0, 0, 0) } },
    { 'L', 1, { SEG(70, 1, 360, 1300, 2650, .7, .4, .2, 0, 0, 0, 0, 0) } },
    { 'R', 1, { SEG(80, .9, 480, 1100, 2300, .9, .6, .25, 0, 1400, 800, .018, 0) } },
    { 'Q', 1, { SEG(80, 1, 350, 1150, 1600, .8, .6, .5, 0, 0, 0, 0, 0) } },
    { 'H', 1, { SEG(70, 0, 0, 0, 0, 0, 0, 0, 0, 1000, 3000, .04, 0) } },
};

static const VoxSeg SEG_ZERO;

static bool vox_is_vowel(char c)
{
    for (unsigned i = 0; i < sizeof(VOWELS) / sizeof(VOWELS[0]); i++)
        if (VOWELS[i].c == c)
            return c != '\0';
    return false;
}

static VoxSeg vowel(char c)
{
    for (unsigned i = 0; i < sizeof(VOWELS) / sizeof(VOWELS[0]); i++)
        if (VOWELS[i].c == c)
            return VOWELS[i].s;
    return SEG_ZERO;
}

static int consonant(char c, VoxSeg s[2])
{
    for (unsigned i = 0; i < sizeof(CONSONANTS) / sizeof(CONSONANTS[0]); i++)
        if (CONSONANTS[i].c == c) {
            s[0] = CONSONANTS[i].s[0];
            s[1] = CONSONANTS[i].s[1];
            return CONSONANTS[i].n;
        }
    return 0;
}

static bool has_char(const char *s, char c)
{
    for (; *s; s++)
        if (*s == c)
            return true;
    return false;
}

static bool is_sonorant(char c) { return c == 'L' || c == 'M' || c == 'N' || c == 'R' || c == 'Q'; }
static bool is_unvoiced_stop(char c) { return c == 'P' || c == 'T' || c == 'K'; }
static bool starts_voiced(char c) { return vox_is_vowel(c) || c == 'W'; }

/* The velar pinch: K and G burst near the next vowel's F2, narrow. */
static void velar_burst(VoxSeg *burst, char next)
{
    VoxSeg vw = vowel(next == 'W' ? 'U' : next);
    uint32_t f = (uint32_t)vw.f[1] * 105u / 100u;
    burst->ncf = (uint16_t)(f < 1300 * 4 ? 1300 * 4 : f > 2600 * 4 ? 2600 * 4 : f);
    burst->nbw = 400;
}

/* An English stop's breath, where the next vowel's F2 will be. */
static VoxSeg aspiration_for(char next)
{
    VoxSeg a = SEG_ZERO;
    a.ms = 40;
    a.ncf = vowel(next == 'W' ? 'U' : next).f[1];
    a.nbw = 2000;
    a.na = Q16(.09);
    return a;
}

/* Toward `to` by pct percent: the unstressed vowel's walk to the schwa,
 * the English -le's to O. */
static void blend_vowel(VoxSeg *s, char to, int pct)
{
    VoxSeg t = vowel(to);
    for (int k = 0; k < 3; k++) {
        s->f[k] = (uint16_t)(s->f[k] + ((int32_t)t.f[k] - s->f[k]) * pct / 100);
        s->a[k] = (int16_t)(s->a[k] + ((int32_t)t.a[k] - s->a[k]) * pct / 100);
    }
}

static void build_plan(Vox *v)
{
    bool nucleus_seen = false, en = v->accent == VOX_ACCENT_EN;
    bool reduced = has_char(v->syl, '.');
    int last_sonorant = -1;

    v->nsegs = 0;
    v->nucleus = -1;
    for (const char *c = v->syl; *c; c++) {
        if (v->nsegs >= VOX_MAX_SEGS - 1)
            break;
        if (*c == '.')
            continue;
        if (starts_voiced(*c)) {
            VoxSeg seg = vowel(*c == 'W' ? 'U' : *c);
            bool onglide = (*c == 'I' || *c == 'Y' || *c == 'W') && vox_is_vowel(c[1]) && !nucleus_seen;
            if (onglide) {
                seg.ms = 60;
            } else if (nucleus_seen) {
                seg.ms = 70; /* off-glide */
            } else {
                nucleus_seen = true;
                v->nucleus = v->nsegs;
                if (reduced) {
                    blend_vowel(&seg, '2', 66);
                    seg.voice = (int16_t)(seg.voice * 60 / 100); /* VOX_UNSTRESSED_GAIN */
                }
                if (en && *c == '2' && has_char(c + 1, 'L'))
                    blend_vowel(&seg, 'O', 75);
            }
            v->segs[v->nsegs++] = seg;
        } else {
            VoxSeg p[2];
            int n = consonant(*c, p);
            if (!n)
                continue;
            if (en && *c == 'L' && !starts_voiced(c[1])) { /* the dark L */
                p[0].f[0] = 420 * 4;
                p[0].f[1] = 800 * 4;
            }
            if (is_sonorant(*c))
                last_sonorant = v->nsegs;
            if ((*c == 'G' || *c == 'K') && n == 2 && starts_voiced(c[1]))
                velar_burst(&p[1], c[1]);
            for (int k = 0; k < n && v->nsegs < VOX_MAX_SEGS - 1; k++)
                v->segs[v->nsegs++] = p[k];
            if (en && is_unvoiced_stop(*c) && starts_voiced(c[1]) && v->nsegs < VOX_MAX_SEGS - 1)
                v->segs[v->nsegs++] = aspiration_for(c[1]);
        }
    }
    if (v->nucleus < 0 && last_sonorant >= 0)
        v->nucleus = (int8_t)last_sonorant;
    v->segs[v->nsegs] = SEG_ZERO; /* the closing silence */
    v->segs[v->nsegs++].ms = 10;
}

static void enter_seg(Vox *v, int i)
{
    v->seg = (int8_t)i;
    v->pos = 0;
    v->coef_left = 0;
    if (i >= v->nsegs) {
        v->playing = false;
        return;
    }
    if (i == v->nucleus)
        v->seglen = v->held ? VOX_HOLD : 2205; /* 50 ms floor */
    else
        v->seglen = (int32_t)v->segs[i].ms * 441 / 10;
    if (v->seglen < 1)
        v->seglen = 1;
}

static void recompute_f0(Vox *v)
{
    int32_t c = (int32_t)v->note * 100 + v->cents;
    int32_t oct = c >= 0 ? c / 1200 : -((-c + 1199) / 1200);
    uint32_t base = VOX_INC_Q40[c - oct * 1200];
    uint32_t inc;

    if (oct >= 8 + 15)
        inc = 0x80000000u;
    else if (oct >= 8)
        inc = base > (0x80000000u >> (oct - 8)) ? 0x80000000u : base << (oct - 8);
    else
        inc = 8 - oct >= 32 ? 0 : base >> (8 - oct);
    if (inc < 97391u) /* 1 Hz */
        inc = 97391u;
    v->inc = inc;
    v->f0 = (int32_t)(((uint64_t)inc * 44100u) >> 16);
    v->w = (v->f0 - 180 * 65536) / 120;
    v->w = v->w < 0 ? 0 : v->w > 32768 ? 32768 : v->w;
    v->bws = 65536 + (v->f0 - 180 * 65536) / 360;
    v->bws = v->bws < 65536 ? 65536 : v->bws > 163840 ? 163840 : v->bws;
}

/* cos(2 pi u), u a fraction of a cycle in Q32, Q30. */
static int32_t vcos(uint32_t u)
{
    uint32_t q = u >> 30, x = u & 0x3FFFFFFFu, i, f;
    int32_t c;
    if (q & 1)
        x = 0x40000000u - x;
    i = x >> 20;
    f = x & 0xFFFFFu;
    c = VOX_COS_Q30[i] + (int32_t)(((int64_t)(VOX_COS_Q30[i + 1] - VOX_COS_Q30[i]) * f) >> 20);
    return q == 1 || q == 2 ? -c : c;
}

/* exp(-pi bw / FS), bw in Hz Q8, Q30: the series, which needs no table
 * at these sizes (pi bw / FS stays under 0.3). */
static int32_t vdecay(uint32_t bw_q8)
{
    int64_t x = ((int64_t)bw_q8 * VOX_BW8_TO_X) >> 16, e = 1 << 30;
    for (int k = 7; k >= 1; k--)
        e = (1 << 30) - ((x * e) >> 30) / k;
    return (int32_t)e;
}

static uint32_t isqrt64(uint64_t n)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* n / d without the 64-bit division helper: once a coefficient update. */
static uint32_t udiv64(uint64_t n, uint32_t d)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t)1 << i;
        }
    }
    return q > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)q;
}

static uint32_t hz_to_u(uint32_t f_q8)
{
    return (uint32_t)(((uint64_t)f_q8 * VOX_HZ8_TO_U) >> 16);
}

/* Two-pole resonator, unit gain at the centre. res_coef. */
static void res_set(VoxRes *r, uint32_t f_q8, uint32_t bw_q8)
{
    uint32_t u = hz_to_u(f_q8);
    int64_t rr, cw, c2w, sw, s2w, c1, s1;
    if (f_q8 < 256) {
        r->a1 = r->a2 = r->g = 0;
        return;
    }
    rr = vdecay(bw_q8);
    cw = vcos(u);
    c2w = vcos(2 * u);
    sw = vcos(u - 0x40000000u);
    s2w = vcos(2 * u - 0x40000000u);
    r->a1 = (int32_t)((2 * rr * cw) >> 30);
    r->a2 = (int32_t)(-((rr * rr) >> 30));
    c1 = ((1 << 30) - ((r->a1 * cw) >> 30) - ((r->a2 * c2w) >> 30)) >> 2;
    s1 = (((r->a1 * sw) >> 30) + ((r->a2 * s2w) >> 30)) >> 2;
    r->g = (int32_t)isqrt64((uint64_t)(c1 * c1 + s1 * s1)) << 2;
}

static int32_t res_run(const VoxRes *r, int32_t *y1, int32_t *y2, int32_t x)
{
    int32_t y = (int32_t)(((int64_t)r->a1 * *y1 + (int64_t)r->a2 * *y2 + (int64_t)r->g * x) >> 30);
    *y2 = *y1;
    *y1 = y;
    return y;
}

/* The cascade's resonator, unity gain at DC: a = 1 - b - c. */
static void kres_coef(uint32_t f_q8, uint32_t bw_q8, int32_t *a, int32_t *b, int32_t *c)
{
    int64_t ce = vdecay(bw_q8);
    *b = (int32_t)((2 * ce * vcos(hz_to_u(f_q8))) >> 30);
    *c = (int32_t)(-((ce * ce) >> 30));
    *a = (1 << 30) - *b - *c;
}

static int32_t kres(int32_t a, int32_t b, int32_t c, int32_t *y1, int32_t *y2, int32_t x)
{
    int32_t y = (int32_t)(((int64_t)a * x + (int64_t)b * *y1 + (int64_t)c * *y2) >> 30);
    *y2 = *y1;
    *y1 = y;
    return y;
}

/* Rounded, not floored: a floored lag settles a few hundred units under
 * its target, and on the pitch that is a drift. */
static int32_t lag(int32_t cur, int32_t target, int32_t k_q30)
{
    return cur + (int32_t)(((int64_t)(target - cur) * k_q30 + (1 << 29)) >> 30);
}

static uint32_t tract_q16(const Vox *v)
{
    int t = v->tract == 0 ? 100 : v->tract;
    t = t < 50 ? 50 : t > 150 ? 150 : t;
    return 6553600u / (uint32_t)t;
}

static uint32_t scale(uint32_t f, uint32_t ts_q16)
{
    return (uint32_t)(((uint64_t)f * ts_q16) >> 16);
}

/* How far the formants stand toward the next segment, Q16: 0 until the
 * glide window at the segment's end, then a linear walk. seg_mix. */
static int32_t seg_mix(const Vox *v)
{
    int64_t gl10 = (int64_t)mode_glide_ms[v->mode] * 441, fl10, num;
    if (gl10 <= 0 || v->seg + 1 >= v->nsegs || v->segs[v->seg + 1].voice <= 0)
        return 0;
    fl10 = (int64_t)v->seglen * 8;
    if (fl10 > gl10)
        fl10 = gl10;
    num = ((int64_t)v->pos - v->seglen) * 10 + fl10;
    if (num <= 0)
        return 0;
    return (int32_t)(((uint32_t)num << 16) / (uint32_t)fl10);
}

static bool seg_sounds(const VoxSeg *s) { return s->voice > 0 || s->na > 0; }

/* The burst's decay and the 0.4 ms-to-1.5 ms ramps against silence. Q15. */
static int32_t seg_env(const Vox *v, const VoxSeg *s, bool against_neighbours, uint32_t fade100)
{
    int32_t env = 32768;
    uint32_t left = (uint32_t)(v->seglen - v->pos);
    bool burst = s->na > 0 && s->voice <= 0 && s->ms <= 45;
    bool prev_sounds = against_neighbours && v->seg > 0 && seg_sounds(&v->segs[v->seg - 1]);
    bool next_sounds = against_neighbours && v->seg + 1 < v->nsegs && seg_sounds(&v->segs[v->seg + 1]);
    if (burst) {
        uint32_t t = ((uint32_t)v->pos << 16) / (uint32_t)v->seglen, i = t >> 8;
        env = VOX_BURST_Q15[i] + (((VOX_BURST_Q15[i + 1] - VOX_BURST_Q15[i]) * (int32_t)(t & 255)) >> 8);
    }
    if (!burst && !prev_sounds && (uint32_t)v->pos * 100u < fade100)
        env = (int32_t)((uint64_t)v->pos * 3276800u / fade100);
    if (!next_sounds && v->seglen < VOX_HOLD && left * 100u < fade100)
        env = (int32_t)((uint64_t)left * 3276800u / fade100);
    return env;
}

/* The frication, one path for both families: pre-emphasis for the high
 * bands, two resonators in series, the flat share, the voiced chop. */
static int32_t frication(Vox *v, const VoxSeg *s, int32_t white)
{
    int32_t din = s->ncf >= 3000 * 4 ? white - v->pw : white, flat = white - v->pw, nb, noise;
    v->pw = white;
    nb = res_run(&v->nr, &v->ny1a, &v->ny2a, din);
    noise = res_run(&v->nr, &v->ny1b, &v->ny2b, nb);
    noise = (int32_t)(((int64_t)noise + (((int64_t)s->nflat * flat) >> 15)) * s->na >> 16);
    if (s->voice > 0 && s->na > 0 && v->phase >= 0x80000000u)
        noise /= 5;
    return noise;
}

static void render_classic(Vox *v, int32_t *out, int frames)
{
    uint32_t ts = tract_q16(v), fade100 = (uint32_t)mode_fade_10[v->mode] * 441u;

    for (int i = 0; i < frames && v->playing; i++) {
        const VoxSeg *s = &v->segs[v->seg];
        const VoxSeg *nx = v->seg + 1 < v->nsegs ? &v->segs[v->seg + 1] : s;
        int32_t mix = seg_mix(v), pulse = 0, voiced = 0, white, noise, env;
        uint32_t old = v->phase;
        int64_t smp;

        if (v->coef_left <= 0) {
            v->coef_left = 64;
            for (int k = 0; k < 3; k++) {
                int32_t fc = s->f[k] > 0 ? s->f[k] : nx->f[k];
                int32_t fn = nx->f[k] > 0 ? nx->f[k] : fc;
                int32_t f = fc + (int32_t)(((int64_t)(fn - fc) * mix) >> 16);
                res_set(&v->fr[k], scale((uint32_t)f << 6, ts),
                        (uint32_t)(((int64_t)(60 + 20 * k) * v->bws) >> 8));
            }
            res_set(&v->nr, scale((uint32_t)s->ncf << 6, ts), (uint32_t)s->nbw << 8);
        }
        v->coef_left--;

        /* The source: impulses, the naive train faded into a band-limited
         * one above 180 Hz, then a -6 dB/oct tilt. */
        v->phase += v->inc;
        if (v->phase < old) {
            uint32_t inc = v->inc, ph = v->phase, idx;
            pulse += (32768 - v->w) << 13;
            while (inc >= 1u << 25) {
                inc >>= 1;
                ph >>= 1;
            }
            idx = (ph << 7) / inc;
            if (idx > 127)
                idx = 127;
            for (int k = 0; k < 32; k++)
                v->ring[(v->ring_pos + k) & 63] += VOX_SINC_Q14[idx][k] * 16384;
        }
        pulse += (int32_t)(((int64_t)v->w * v->ring[v->ring_pos]) >> 15);
        v->ring[v->ring_pos] = 0;
        v->ring_pos = (uint8_t)((v->ring_pos + 1) & 63);
        v->glot = (int32_t)(((int64_t)v->glot * VOX_TILT_Q30 + (int64_t)pulse * ((1 << 30) - VOX_TILT_Q30)) >> 30);

        for (int k = 0; k < 3; k++) {
            int32_t amp = s->a[k] + (int32_t)(((int64_t)(nx->a[k] - s->a[k]) * mix) >> 16);
            int32_t y = res_run(&v->fr[k], &v->fy1[k], &v->fy2[k], v->glot);
            int32_t t = (int32_t)(((int64_t)amp * y) >> 14);
            voiced += k == 1 ? -t : t;
        }

        v->rng = v->rng * 1664525u + 1013904223u;
        white = (int32_t)v->rng >> 3;
        noise = frication(v, s, white);
        env = seg_env(v, s, true, fade100);

        /* 60 and 2.6: VOX_VOICE_GAIN and VOX_NOISE_GAIN. */
        smp = ((int64_t)s->voice * voiced >> 14) * 60 + (int64_t)noise * 26 / 10;
        out[i] += (int32_t)((smp * env) >> (15 + 13));

        if (++v->pos >= v->seglen)
            enter_seg(v, v->seg + 1);
    }
}

/* Rosenberg glottal pulse; `soft` rounds the closing corner. Q30. */
static int32_t glottal(uint32_t phase, int32_t soft_q15)
{
    int64_t ph = phase >> 2, x, hard, smooth;
    const int64_t tp = 450971566, tn = 193273528; /* .42, .18 */
    if (ph < tp) {
        x = (ph * 2556528152u) >> 30; /* / .42 */
        return (int32_t)((3 * ((x * x) >> 30)) - 2 * ((((x * x) >> 30) * x) >> 30));
    }
    if (ph < tp + tn) {
        x = ((ph - tp) * 5965232356u) >> 30; /* / .18 */
        hard = (1 << 30) - ((x * x) >> 30);
        smooth = (1 << 30) - ((((x * x) >> 30) * (((int64_t)3 << 30) - 2 * x)) >> 30);
        return (int32_t)(hard + (((smooth - hard) * soft_q15) >> 15));
    }
    return 0;
}

/* MELO and SINGR: a Klatt cascade, a Rosenberg source, lagged
 * articulation. The voiced path runs in Q24. */
static void render_cascade(Vox *v, int32_t *out, int frames)
{
    uint32_t ts = tract_q16(v), fade100 = (uint32_t)mode_fade_10[4] * 441u;
    bool sing = v->mode == 4;

    for (int i = 0; i < frames && v->playing; i++) {
        const VoxSeg *s = &v->segs[v->seg];
        int32_t nas_t = sing ? s->nasal : 0, white, noise, env, f0norm, soft, g, src, y, smp, rad;
        int32_t f0t = v->f0;

        if (s->f[0] > 0) {
            v->f1c = lag(v->f1c, (int32_t)s->f[0] << 14, VOX_KART_Q30);
            v->f2c = lag(v->f2c, (int32_t)s->f[1] << 14, VOX_KART_Q30);
            v->f3c = lag(v->f3c, (int32_t)s->f[2] << 14, VOX_KART_Q30);
        }
        v->voicec = lag(v->voicec, (int32_t)s->voice << 16, VOX_KFAST_Q30);
        v->nasalc = lag(v->nasalc, nas_t << 30, VOX_KART_Q30);
        v->zfc = lag(v->zfc, (nas_t > 0 ? 330 : 280) << 16, VOX_KART_Q30);

        if (v->coef_left <= 0) {
            int32_t bws = 65536 + (v->f0c - 180 * 65536) / 360, a, b, c;
            uint32_t za;
            bws = bws < 65536 ? 65536 : bws > 163840 ? 163840 : bws;
            v->coef_left = 64;
            kres_coef(scale((uint32_t)v->f1c >> 8, ts), (uint32_t)(80 * bws) >> 8, &v->ka[0], &v->kb[0], &v->kc[0]);
            kres_coef(scale((uint32_t)v->f2c >> 8, ts), (uint32_t)(90 * bws) >> 8, &v->ka[1], &v->kb[1], &v->kc[1]);
            kres_coef(scale((uint32_t)v->f3c >> 8, ts), (uint32_t)(120 * bws) >> 8, &v->ka[2], &v->kb[2], &v->kc[2]);
            kres_coef(scale(3400 << 8, ts), 250 << 8, &v->ka[3], &v->kb[3], &v->kc[3]);
            kres_coef(scale(4500 << 8, ts), 300 << 8, &v->ka[4], &v->kb[4], &v->kc[4]);
            kres_coef(scale(280 << 8, ts), 120 << 8, &v->npa, &v->npb, &v->npc);
            /* The anti-resonator: the inverse filter, its gain 1 / a. */
            kres_coef(scale((uint32_t)v->zfc >> 8, ts),
                      (uint32_t)(100 * 256 + ((((int64_t)200 * 256) * ((1 << 30) - v->nasalc)) >> 30)), &a, &b, &c);
            za = a > 0 ? (uint32_t)a : 1;
            v->nza = (int32_t)udiv64((uint64_t)1 << 46, za);
            v->nzb = (int32_t)(-(((int64_t)b * v->nza) >> 30));
            v->nzc = (int32_t)(-(((int64_t)c * v->nza) >> 30));
            res_set(&v->nr, scale((uint32_t)s->ncf << 6, ts), (uint32_t)s->nbw << 8);
        }
        v->coef_left--;

        /* The pitch rounds its steps through a lag; SINGR's vibrato waits
         * 180 ms and comes in over 250, unless the track wrote an fVIB. */
        if (sing && v->allow_vib) {
            int32_t depth = v->t_note < 7938 ? 0 : v->t_note >= 7938 + 11025 ? 32768
                          : (int32_t)((v->t_note - 7938) * 32768 / 11025);
            int32_t sn = vcos(v->vib_phase - 0x40000000u);
            int32_t dev = (int32_t)(((int64_t)((15032386 * (int64_t)depth) >> 15) * sn) >> 30); /* .014 */
            v->vib_phase += VOX_VIB_INC;
            f0t = (int32_t)(((int64_t)f0t * ((1 << 30) + dev)) >> 30);
        }
        v->f0c = lag(v->f0c, f0t, VOX_KF0_Q30);
        if (v->t_note < 0xFFFFFFFFu)
            v->t_note++;
        v->phase += (uint32_t)(((uint64_t)(uint32_t)v->f0c * VOX_HZ16_TO_U) >> 31);

        v->rng = v->rng * 1664525u + 1013904223u;
        white = (int32_t)v->rng >> 3;

        /* The source, normalized against the pitch (110 Hz = 1). */
        {
            int32_t f4 = v->f0c >> 12;
            f0norm = (110 << 18) / (f4 > 0 ? f4 : 1);
            f0norm = f0norm > 32768 ? 32768 : f0norm < 4096 ? 4096 : f0norm;
        }
        soft = (v->f0c - 180 * 65536) / 360;
        soft = soft < 0 ? 0 : soft > 32768 ? 32768 : soft;
        g = glottal(v->phase, soft);
        src = (int32_t)((((int64_t)v->voicec * g) >> 30) * f0norm >> 20);
        if (sing) {
            int64_t shape = 429496730 + (((int64_t)644245094 * g) >> 30); /* .4 + .6 g */
            int64_t b = ((((int64_t)26843546 * v->voicec) >> 30) * (white >> 4)) >> 30; /* .025 */
            src += (int32_t)(((b * shape) >> 30) * f0norm >> 14);
        }

        y = kres(v->npa, v->npb, v->npc, &v->npy1, &v->npy2, src);
        {
            int32_t z = (int32_t)(((int64_t)v->nza * y + (int64_t)v->nzb * v->nzx1 +
                                   (int64_t)v->nzc * v->nzx2) >> 16);
            v->nzx2 = v->nzx1;
            v->nzx1 = y;
            y = z;
        }
        for (int k = 0; k < 5; k++)
            y = kres(v->ka[k], v->kb[k], v->kc[k], &v->ky1[k], &v->ky2[k], y);

        noise = frication(v, s, white);
        env = seg_env(v, s, false, fade100);

        /* 5.4 and 2.7: VOX2_VOICE_GAIN and VOX2_NOISE_GAIN. Radiation is
         * the first difference of the voiced path. */
        smp = (int32_t)((int64_t)y * 54 / 10);
        rad = smp - v->prev_raw;
        v->prev_raw = smp;
        out[i] += (rad >> 9) + (int32_t)(((int64_t)noise * env * 27 / 10) >> (15 + 13));

        if (++v->pos >= v->seglen)
            enter_seg(v, v->seg + 1);
    }
}

void vox_render(Vox *v, int32_t *out, int frames)
{
    if (!v->playing)
        return;
    if (v->mode >= 3)
        render_cascade(v, out, frames);
    else
        render_classic(v, out, frames);
}

void vox_note_on(Vox *v, uint8_t note)
{
    v->note = note;
    recompute_f0(v);
    /* Reached by a glide: the vowel carries on, only the pitch moves --
     * unless the tied step wrote a syllable, which is then sung. */
    if (v->tied && v->playing && v->held && !v->resyl) {
        v->tied = false;
        return;
    }
    v->tied = false;
    v->resyl = false;
    if (!v->playing) { /* a fresh phrase: the articulators from rest */
        v->f1c = 500 << 16;
        v->f2c = 1450 << 16;
        v->f3c = 2450 << 16;
        v->zfc = 280 << 16;
        v->voicec = 0;
        v->nasalc = 0;
        v->f0c = v->f0;
        v->prev_raw = 0;
    }
    v->t_note = 0;
    build_plan(v);
    v->held = true;
    v->playing = true;
    enter_seg(v, 0);
}

void vox_note_off(Vox *v)
{
    if (!v->held)
        return;
    v->held = false;
    /* The nucleus gets one glide window to finish its walk. */
    if (v->playing && v->seg == v->nucleus) {
        int ms = mode_glide_ms[v->mode] > 0 ? mode_glide_ms[v->mode] : 10;
        v->seglen = v->pos + ms * 441 / 10;
    }
}

void vox_set_timbre(Vox *v, uint8_t mode)
{
    v->mode = mode < VOX_MODES ? mode : 0;
}

void vox_set_cents(Vox *v, int cents)
{
    if (cents == v->cents)
        return;
    v->cents = (int16_t)cents;
    recompute_f0(v);
}

void vox_reset(Vox *v)
{
    char syl[VOX_SYL_LEN];
    uint8_t mode = v->mode, tract = v->tract, accent = v->accent;
    bool allow_vib = v->allow_vib;
    for (int i = 0; i < VOX_SYL_LEN; i++)
        syl[i] = v->syl[i];
    for (uint32_t i = 0; i < sizeof(*v); i++)
        ((uint8_t *)v)[i] = 0;
    v->mode = mode;
    v->tract = tract;
    v->accent = accent;
    v->allow_vib = allow_vib;
    for (int i = 0; i < VOX_SYL_LEN; i++)
        v->syl[i] = syl[i];
    v->rng = 1;
    v->note = 60;
    recompute_f0(v);
}

void vox_init(Vox *v)
{
    for (uint32_t i = 0; i < sizeof(*v); i++)
        ((uint8_t *)v)[i] = 0;
    v->rng = 1;
    v->note = 60;
    v->syl[0] = 'A'; /* an instrument that never got a syllable sings */
    recompute_f0(v);
}

void vox_set_voice(Vox *v, uint8_t tract, uint8_t accent, bool allow_vib)
{
    v->tract = tract;
    v->accent = accent == VOX_ACCENT_EN ? VOX_ACCENT_EN : VOX_ACCENT_FR;
    v->allow_vib = allow_vib;
}

void vox_set_syllable(Vox *v, const char *syl, bool tied, bool written)
{
    if (syl && syl[0]) {
        int i = 0;
        for (; i < VOX_SYL_LEN - 1 && syl[i]; i++)
            v->syl[i] = syl[i];
        v->syl[i] = '\0';
    }
    v->tied = tied;
    v->resyl = tied && written;
}

int vox_onset_ms(const char *syl, uint8_t accent)
{
    int t = 0;
    if (!syl)
        return 0;
    for (const char *c = syl; *c; c++) {
        VoxSeg p[2];
        int n;
        if (*c == '.')
            continue;
        if (starts_voiced(*c))
            return t; /* glide or nucleus: the beat lands here */
        n = consonant(*c, p);
        for (int k = 0; k < n; k++) {
            if (p[k].voice > 0)
                return t; /* voiced is pitched: the note has begun */
            t += p[k].ms;
        }
        if (n && accent == VOX_ACCENT_EN && is_unvoiced_stop(*c) && starts_voiced(c[1]))
            t += 40;
    }
    return t;
}

int vox_coda_ms(const char *syl)
{
    int t = 0;
    bool nucleus_seen = false;
    if (!syl)
        return 0;
    for (const char *c = syl; *c; c++) {
        if (*c == '.')
            continue;
        if (starts_voiced(*c)) {
            bool onglide = (*c == 'I' || *c == 'Y' || *c == 'W') && vox_is_vowel(c[1]) && !nucleus_seen;
            if (onglide)
                continue;
            if (!nucleus_seen) {
                nucleus_seen = true;
                continue;
            }
            t += 70;
        } else if (nucleus_seen) {
            VoxSeg p[2];
            int n = consonant(*c, p);
            for (int k = 0; k < n; k++)
                t += p[k].ms;
        }
    }
    return t > 0 ? t + 45 : 0;
}
