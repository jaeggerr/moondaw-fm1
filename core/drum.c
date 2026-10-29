/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM: the engine around Felucca's drum voices. Its GM map, its kits'
 * names and the swaps of their lanes are Felucca's (firmware/src/
 * eng_drum.c, GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita, Hugelton
 * Instruments); the pool of lanes and the blocks are ours. */
#include "drum.h"

#include "drum_tables.h"
#include "drum_dsp.h"
#include "drum_voice.inc"

/* Felucca renders its drums by blocks of CTL samples: the slow envelopes
 * step once a block. A render here is any length (it stops at every
 * tick), so each lane renders a block ahead and hands it out sample by
 * sample; a hit starts a fresh block at the next sample. */
typedef struct {
    dv_coef_t c;
    dv_voice_t v;
    dv_metal_t mb;
    int16_t buf[CTL];
    uint8_t pos;    /* in buf; CTL: a block is due */
    int8_t track;   /* -1: free */
    uint8_t lane;   /* DV_KICK .. DV_BELL */
} Lane;

static Lane pool[DRUM_VOICES]
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;

/* the timbres: Felucca's own kit with either kick, then its model kits
 * (DV_KIT 1..8) */
static const char *const KIT_NAME[DRUM_KITS] = {
    "STD", "ROUND", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC",
};

/* General MIDI notes 35..81 -> the drum (DVT_*) and semitones from its
 * designed pitch: eng_drum.c's DRUM_GM */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};
/* a model kit's lanes that play another piece than their name: 1 TOM ->
 * CONGA, 2 RIM -> CLAVE, 4 BELL -> CYM (eng_drum.c's DK_SWAP) */
static const uint8_t DK_SWAP[DV_NKIT] = {0, 4, 1, 0, 2 | 4, 4, 0, 1 | 2};

static uint32_t gm_note(uint32_t note)
{
    return note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
}

void drum_init(void)
{
    for (int i = 0; i < DRUM_VOICES; i++) {
        dv_init(&pool[i].v, 0);
        pool[i].pos = CTL;
        pool[i].track = -1;
    }
}

static bool rings(const Lane *l) { return l->v.live || l->v.trig; }

/* The lane a hit plays on: its own if it has one, a free one, else the
 * quietest of those ringing. */
static Lane *lane_for(int track, uint32_t lane)
{
    Lane *pick = 0;
    for (int i = 0; i < DRUM_VOICES; i++)
        if (pool[i].track == track && pool[i].lane == lane)
            return &pool[i];
    for (int i = 0; i < DRUM_VOICES && !pick; i++)
        if (pool[i].track < 0 || !rings(&pool[i]))
            pick = &pool[i];
    if (pick)
        return pick;
    pick = &pool[0];
    for (int i = 1; i < DRUM_VOICES; i++)
        if (pool[i].v.q[0] < pick->v.q[0])
            pick = &pool[i];
    return pick;
}

void drum_hit(int track, uint8_t kit, uint8_t note, uint8_t vel, const int8_t knob[DRUM_KNOBS])
{
    uint32_t n = gm_note(note), t = (uint32_t)DRUM_GM[n - 35u][0], lane = DV_TYPE_LANE[t], role;
    int32_t st = DRUM_GM[n - 35u][1];
    dv_param_t k;
    Lane *l;

    if (kit >= DRUM_KITS)
        kit = 0;
    if (kit >= 2)
        role = DV_KTYPE(kit - 1u, lane);
    else
        role = t == DVT_PUNCH && kit == 1 ? DVT_ROUND : t;
    dv_default(&k, role);
    k.tune = (int16_t)(st * 16 + knob[DRUM_TUNE] * 3);   /* +-12 semitones */
    k.tone = (uint8_t)clamp(k.tone + knob[DRUM_TONE], 0, 127);
    k.decay = (uint8_t)clamp(k.decay + knob[DRUM_DECAY], 0, 127);
    k.extra = (uint8_t)clamp(k.extra + knob[DRUM_SNAP], 0, 127);
    k.accent = (uint8_t)clamp((100 * vel * 4) >> 9, 0, 127); /* Felucca's ACC at its 100 */

    l = lane_for(track, lane);
    if (l->track != track || l->v.type != dv_run_type(role)) /* another drum here: from rest */
        dv_init(&l->v, role);
    l->track = (int8_t)track;
    l->lane = (uint8_t)lane;
    dv_setup(&l->c, &k);
    if (dv_uses_metal(l->c.type))
        dv_metal_tune(&l->mb, &l->c);
    dv_trigger(&l->v);
    l->pos = CTL;
    if (lane == DV_HATC) /* a closed hat chokes the open one */
        for (int i = 0; i < DRUM_VOICES; i++)
            if (pool[i].track == track && pool[i].lane == DV_HATO && pool[i].v.live)
                dv_choke(&pool[i].v);
}

static void lane_block(Lane *l)
{
    int32_t y[CTL], m[CTL];
    bool metal = dv_uses_metal(l->c.type) != 0;
    if (metal)
        dv_metal_run(&l->mb, m, CTL);
    dv_run(&l->c, &l->v, metal ? m : 0, y, CTL);
    for (int i = 0; i < CTL; i++)
        l->buf[i] = (int16_t)y[i]; /* under full scale: dv_out's knee */
    l->pos = 0;
}

void drum_render(int track, int32_t *mono, int frames)
{
    for (int k = 0; k < DRUM_VOICES; k++) {
        Lane *l = &pool[k];
        if (l->track != track)
            continue;
        for (int i = 0; i < frames; i++) {
            if (l->pos >= CTL) {
                if (!rings(l))
                    break;
                lane_block(l);
            }
            /* -6 dB: Felucca's drums peak near -6 dBFS each, and a kit's
             * lanes add up; here a hit sits as high as a PULSE voice */
            mono[i] += l->buf[l->pos] >> 1;
            l->pos++;
        }
    }
}

void drum_silence(int track)
{
    for (int i = 0; i < DRUM_VOICES; i++)
        if (pool[i].track == track) {
            pool[i].v.live = pool[i].v.trig = 0;
            pool[i].pos = CTL;
            pool[i].track = -1;
        }
}

bool drum_rings(int track)
{
    for (int i = 0; i < DRUM_VOICES; i++)
        if (pool[i].track == track && (rings(&pool[i]) || pool[i].pos < CTL))
            return true;
    return false;
}

const char *drum_kit_name(uint8_t kit) { return KIT_NAME[kit < DRUM_KITS ? kit : 0]; }

const char *drum_note_abbr(uint8_t kit, uint8_t note)
{
    static const char *const N[DV_NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    uint32_t n = gm_note(note), t = (uint32_t)DRUM_GM[n - 35u][0], lane = DV_TYPE_LANE[t];
    uint32_t swap = kit >= 2 && kit < DRUM_KITS ? DK_SWAP[kit - 2u] : 0u;
    if (kit < 2) /* Felucca's own: the GM note's own piece */
        return t == DVT_CONGA ? "CG" : t == DVT_CLAVE ? "CL" : t == DVT_CYM ? "CY" : N[lane];
    if (lane == DV_TOM && (swap & 1u))
        return "CG";
    if (lane == DV_RIM && (swap & 2u))
        return "CL";
    return lane == DV_BELL && (swap & 4u) ? "CY" : N[lane];
}
