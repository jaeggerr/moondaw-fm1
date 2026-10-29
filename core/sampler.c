/* SPDX-License-Identifier: GPL-3.0-only */
#include "sampler.h"

#include "tables.h"

#define PAYLOAD (SMP_BLOCK * 16u) /* a store sector's: 4096 - its 16-byte header */
#define INC_MAX (8u << 16)        /* four octaves over the bank's rate: a runaway note stops climbing */
#define PITCH_LIMIT 9600

/* IMA ADPCM, the standard's two tables. */
static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
    449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
static const int8_t INDEX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* The bank as the voices see it, written by the main loop only while no
 * voice can read it: smp_hold first. */
static const uint8_t *sec[SMP_MAX_SECTORS];
static uint32_t nsec;
static struct {
    uint32_t at, frames;
} map[SONG_SMP_MAX];
static volatile uint8_t gen;

int16_t smp_decode(int16_t *pred, uint8_t *idx, uint8_t code)
{
    int32_t step = STEP[*idx], diff = step >> 3, p;
    int i;

    if (code & 4)
        diff += step;
    if (code & 2)
        diff += step >> 1;
    if (code & 1)
        diff += step >> 2;
    p = *pred + (code & 8 ? -diff : diff);
    p = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
    i = *idx + INDEX[code & 7];
    *idx = (uint8_t)(i < 0 ? 0 : i > 88 ? 88 : i);
    *pred = (int16_t)p;
    return (int16_t)p;
}

void smp_hold(void)
{
    gen++;
    nsec = 0;
    for (int i = 0; i < SONG_SMP_MAX; i++)
        map[i].frames = 0;
}

void smp_attach(const uint8_t *const *payloads, int n)
{
    nsec = 0;
    for (int i = 0; i < n && i < SMP_MAX_SECTORS; i++)
        sec[i] = payloads[i];
    nsec = (uint32_t)(n < SMP_MAX_SECTORS ? n : SMP_MAX_SECTORS);
}

void smp_map(int i, uint32_t at, uint32_t frames)
{
    if (i < 0 || i >= SONG_SMP_MAX)
        return;
    map[i].frames = 0; /* never half of one entry and half of another */
    map[i].at = at;
    map[i].frames = frames;
}

void smp_init(SmpVoice *v)
{
    for (uint32_t i = 0; i < sizeof(*v); i++)
        ((uint8_t *)v)[i] = 0;
}

/* The next frame of the sample, 0 past its end (counted in `tail`). A
 * block begins with its predictor, which is its first frame. */
static int16_t next(SmpVoice *v)
{
    uint8_t c;

    if (v->left == 0) {
        v->tail++;
        return 0;
    }
    v->left--;
    if (v->codes == 0) {
        uint32_t s = v->at / PAYLOAD;
        const uint8_t *b;
        if (s >= nsec) { /* the bank is not what it was: silence */
            v->left = 0;
            v->tail++;
            return 0;
        }
        b = sec[s] + (v->at - s * PAYLOAD);
        v->pred = (int16_t)(b[0] | b[1] << 8);
        v->idx = b[2] > 88 ? 88 : b[2];
        v->p = b + 3;
        v->odd = 0;
        v->codes = SMP_BLOCK_FRAMES - 1;
        v->at += SMP_BLOCK;
        return v->pred;
    }
    c = v->odd ? (uint8_t)(*v->p++ >> 4) : (uint8_t)(*v->p & 15);
    v->odd ^= 1;
    v->codes--;
    return smp_decode(&v->pred, &v->idx, c);
}

static void recompute(SmpVoice *v)
{
    int32_t c = v->base + v->cents, oct, r;
    uint32_t inc;

    c = c > PITCH_LIMIT ? PITCH_LIMIT : c < -PITCH_LIMIT ? -PITCH_LIMIT : c;
    oct = c >= 0 ? c / 1200 : -((-c + 1199) / 1200);
    r = c - oct * 1200;
    inc = SMP_INC_Q16[r];
    if (oct >= 4)
        inc = INC_MAX;
    else if (oct >= 0)
        inc <<= oct;
    else
        inc = -oct >= 16 ? 1 : inc >> -oct;
    v->inc = inc > INC_MAX ? INC_MAX : inc ? inc : 1;
}

bool smp_start(SmpVoice *v, int smp, uint8_t note, uint8_t root, int tune, uint32_t gain_q16)
{
    v->playing = false;
    if (smp < 0 || smp >= SONG_SMP_MAX || map[smp].frames == 0)
        return false;
    v->gen = gen;
    v->at = map[smp].at;
    v->left = map[smp].frames;
    v->codes = 0;
    v->tail = 0;
    v->frac = 0;
    v->base = (int16_t)(((int)note - (int)root) * 100 + tune);
    recompute(v);
    /* moondaw plays a full-scale sample at 16000 / 32768 of full scale */
    v->gain = (int32_t)(((gain_q16 >> 4) * 16000u) >> 15);
    v->s0 = next(v);
    v->s1 = next(v);
    v->playing = true;
    return true;
}

void smp_set_cents(SmpVoice *v, int cents)
{
    if (cents == v->cents)
        return;
    v->cents = (int16_t)cents;
    recompute(v);
}

void smp_stop(SmpVoice *v)
{
    v->playing = false;
}

void smp_render(SmpVoice *v, int32_t *out, int frames)
{
    const int32_t g = v->gain;
    const uint32_t inc = v->inc;
    uint32_t frac = v->frac;

    if (!v->playing)
        return;
    if (v->gen != gen) { /* the bank was put away under it */
        v->playing = false;
        return;
    }
    for (int i = 0; i < frames; i++) {
        int32_t y = v->s0 + (((v->s1 - v->s0) * (int32_t)(frac >> 4)) >> 12);
        out[i] += (y * g) >> 12;
        frac += inc;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s0 = v->s1;
            v->s1 = next(v);
        }
        if (v->tail >= 2) { /* both neighbours past the end: it has rung out */
            v->playing = false;
            break;
        }
    }
    v->frac = frac;
}

/* ------------------------------------------------- the player's pool --- */
static SmpVoice pool[SMP_VOICES]
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;
static int8_t *owner[SMP_VOICES];
static uint32_t struck[SMP_VOICES], strikes;

void smp_pool_init(void)
{
    for (int k = 0; k < SMP_VOICES; k++) {
        smp_init(&pool[k]);
        owner[k] = 0;
    }
}

void smp_free(int8_t *slot)
{
    if (*slot < 0 || *slot >= SMP_VOICES)
        return;
    pool[*slot].playing = false;
    owner[*slot] = 0;
    *slot = -1;
}

static SmpVoice *take(int8_t *slot)
{
    int pick = -1;

    if (*slot >= 0 && *slot < SMP_VOICES)
        return &pool[*slot];
    for (int k = 0; k < SMP_VOICES && pick < 0; k++)
        if (!owner[k] || !pool[k].playing)
            pick = k;
    if (pick < 0) /* the oldest note gives its voice */
        for (int k = pick = 0; k < SMP_VOICES; k++)
            if (strikes - struck[k] > strikes - struck[pick])
                pick = k;
    if (owner[pick])
        *owner[pick] = -1;
    owner[pick] = slot;
    *slot = (int8_t)pick;
    smp_init(&pool[pick]);
    return &pool[pick];
}

void smp_strike(int8_t *slot, const Song *s, int inst, int fsmp, uint8_t note)
{
    SmpVoice *v = take(slot);
    const Zone *z;
    bool on = false;

    struck[*slot] = ++strikes;
    if (fsmp >= 0)
        on = smp_start(v, fsmp, note, 60, 0, 65536);
    else if ((z = song_zone(s, inst, note)) != 0)
        on = smp_start(v, z->smp, note, z->root, z->tune,
                       DB_GAIN_Q16[(z->gain < -64 ? -64 : z->gain > 24 ? 24 : z->gain) + 64]);
    if (!on)
        smp_free(slot);
}

void smp_cents(int8_t slot, int cents)
{
    if (slot >= 0 && slot < SMP_VOICES)
        smp_set_cents(&pool[slot], cents);
}

void smp_mix(int8_t slot, int32_t *out, int frames)
{
    if (slot >= 0 && slot < SMP_VOICES)
        smp_render(&pool[slot], out, frames);
}

bool smp_sounds(int8_t slot)
{
    return slot >= 0 && slot < SMP_VOICES && pool[slot].playing;
}
