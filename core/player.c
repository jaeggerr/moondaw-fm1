/* SPDX-License-Identifier: GPL-3.0-only */
#include "player.h"

#include "fx.h"
#include "synth.h"
#include "tables.h"

#define FS SYNTH_FS
#define TRACK_MAX_VOICES (STEP_MAX_NOTES * (1 + INSTR_MAX_LAYERS))
#define VEL_FULL 127

static int32_t mul_q(int32_t a, int32_t b, int q)
{
    return (int32_t)(((int64_t)a * b) >> q);
}

static int32_t sine_q15(uint16_t phase)
{
    int32_t a = SINE_Q15[phase >> 8], b = SINE_Q15[(phase >> 8) + 1];
    return a + (((b - a) * (int32_t)(phase & 255)) >> 8);
}

/* ============================================================ chain ===
 * One per track, downstream of its voices: DC block, filter, envelope,
 * velocity and instrument gain, crush, the fader, the pan.
 *
 * The signal is Q15 with 4 guard bits below (CH_SHIFT); the envelope is
 * Q30; the filter coefficients are Q20. */
#define CH_SHIFT 4
#define ENV_ONE (1 << 30)
#define MIN_RAMP_SAMPLES 32
#define COEF_CHUNK 32

enum { ENV_IDLE, ENV_PUNCH, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    uint8_t stage;
    int32_t env, sustain, atk_inc, dec_inc, rel_inc;
    uint8_t vel;          /* 0..127 */
    uint32_t igain;       /* Q16, the instrument's gain */
    uint32_t out_gain;    /* Q16, the instrument's level */
    uint32_t play;        /* Q16 = vel * igain: what crush sees */
    uint32_t vol;         /* Q15, the fader as the track has it */
    int32_t trem, trem_target; /* Q24 */
    int8_t pan;           /* -64..63 */
    uint8_t cutoff, reso, filt_env, filt_vel;
    bool filtering;
    int32_t dc_x1, dc_y1;
    int32_t ic1, ic2, a1, a2, a3;
    uint32_t crush_levels, crush_step, crush_hold, crush_phase;
    int32_t crush_held;
} Chain;

static void chain_bypass(Chain *c)
{
    c->filtering = !(c->cutoff >= FILTER_OPEN && c->reso == 0 &&
                     c->filt_env == FILTER_ENV_NONE &&
                     c->filt_vel == FILTER_ENV_NONE);
}

static void chain_init(Chain *c)
{
    for (uint32_t i = 0; i < sizeof(*c); i++)
        ((uint8_t *)c)[i] = 0;
    c->igain = c->out_gain = 65536;
    c->vel = VEL_FULL;
    c->trem = c->trem_target = 1 << 24;
    c->sustain = ENV_ONE;
    c->atk_inc = c->dec_inc = c->rel_inc = ENV_ONE;
    c->cutoff = FILTER_OPEN;
    c->filt_env = c->filt_vel = FILTER_ENV_NONE;
    c->a1 = 1 << 20;
}

static uint32_t db_gain(int db)
{
    return DB_GAIN_Q16[(db < -64 ? -64 : db > 24 ? 24 : db) + 64];
}

/* Times 0..255 on a quadratic curve, v*v/8 ms: 55 is 378 ms, 255 is 8 s. */
static int32_t seg_inc(uint8_t v, int32_t span)
{
    int32_t n = (((int32_t)v * v) / 8) * FS / 1000;
    if (n < MIN_RAMP_SAMPLES)
        n = MIN_RAMP_SAMPLES;
    return span / n;
}

static void chain_set_volume(Chain *c, uint8_t vol)
{
    c->vol = ((uint32_t)vol << 15) / 127u;
    c->play = (uint32_t)(((uint64_t)c->igain * c->vel) / 127u);
}

static void chain_set_crush(Chain *c, uint8_t crs, uint8_t rat)
{
    c->crush_levels = crs ? CRUSH_LEVELS_Q8[crs] : 0;
    c->crush_step = crs ? CRUSH_STEP_Q8[crs] : 0;
    c->crush_hold = CRUSH_HOLD_Q8[rat];
}

static void note_params(Chain *c, uint8_t vol, uint8_t vel,
                        const InstrumentDef *in, uint8_t cutoff, uint8_t reso)
{
    c->igain = db_gain(in->gain);
    c->out_gain = db_gain(in->level);
    c->vel = vel > 127 ? 127 : vel;
    chain_set_volume(c, vol);
    c->cutoff = cutoff;
    c->reso = reso;
    c->filt_env = in->filt_env;
    c->filt_vel = in->filt_vel;
    chain_bypass(c);
    c->sustain = in->sustain == 255 ? ENV_ONE : (int32_t)in->sustain * (ENV_ONE / 255);
    c->atk_inc = seg_inc(in->attack, ENV_ONE);
    c->dec_inc = seg_inc(in->decay, ENV_ONE - c->sustain);
    c->rel_inc = seg_inc(in->release, ENV_ONE);
}

static void chain_note_on(Chain *c, uint8_t vol, uint8_t vel,
                          const InstrumentDef *in, uint8_t cutoff, uint8_t reso)
{
    note_params(c, vol, vel, in, cutoff, reso);
    /* A note over one still sounding: a fast fall first, so the attack
     * that follows is heard. */
    c->stage = c->env > 0 ? ENV_PUNCH : ENV_ATTACK;
}

/* A tied note: the envelope carries on. */
static void chain_note_legato(Chain *c, uint8_t vol, uint8_t vel,
                              const InstrumentDef *in, uint8_t cutoff,
                              uint8_t reso)
{
    if (c->stage == ENV_IDLE || c->stage == ENV_RELEASE)
        chain_note_on(c, vol, vel, in, cutoff, reso);
    else
        note_params(c, vol, vel, in, cutoff, reso);
}

static void chain_note_off(Chain *c)
{
    if (c->stage != ENV_IDLE)
        c->stage = ENV_RELEASE;
}

static void chain_retrigger(Chain *c)
{
    c->env = 0;
    c->stage = ENV_ATTACK;
}

/* Cutoff 0..255 is 30 Hz to 16 kHz over 9.06 octaves; the envelope and
 * the velocity each move it by up to 6 octaves either way. A TPT state
 * variable filter, low-pass out. */
static void update_coefs(Chain *c)
{
    enum { K = 43401 }; /* 6 / 9.06, Q16 */
    int32_t cut = (int32_t)c->cutoff * 257;
    int32_t g, k, d;

    if (c->filt_env != FILTER_ENV_NONE)
        cut += mul_q(((int32_t)c->filt_env - FILTER_ENV_NONE) * K / 63,
                     c->env >> 14, 16);
    if (c->filt_vel != FILTER_ENV_NONE)
        cut += mul_q(((int32_t)c->filt_vel - FILTER_ENV_NONE) * K / 63,
                     ((int32_t)c->vel - 127) * 65536 / 127, 16);
    cut = cut < 0 ? 0 : cut > 65536 ? 65536 : cut;
    g = (int32_t)SVF_G_Q20[cut >> 6];
    k = 2097152 - (1992294 * (int32_t)c->reso) / 255; /* 2.0 .. 0.1 */
    d = (1 << 20) + mul_q(g, g + k, 20);
    c->a1 = (int32_t)(0x80000000u / (uint32_t)(d >> 9));
    c->a2 = mul_q(g, c->a1, 20);
    c->a3 = mul_q(g, c->a2, 20);
}

/* In 32 bits with the carry, not in 64: the same sums, and on the FM-1 a
 * fifth of the instructions -- it runs twice a sample on every track. */
static int32_t sat_add(int32_t a, int32_t b)
{
    int32_t s;
    if (__builtin_add_overflow(a, b, &s))
        return a < 0 ? -0x7FFFFFFF - 1 : 0x7FFFFFFF;
    return s;
}

/* p x a pan gain (0..64), >> 10: in 32 bits while the product fits, which
 * is always but for a sample past 2^25; the same result either way. */
static int32_t pan_q(int32_t p, int32_t g)
{
    if (p < (1 << 25) && p > -(1 << 25))
        return (p * g) >> 10;
    return mul_q(p, g, 6 + CH_SHIFT);
}

/* mono: Q15, one track's voices summed. out: stereo, Q15, accumulated.
 *
 * The state that changes every sample is held in locals for the block and
 * written back after it: through `c` the compiler had to reload and store
 * each field around every write to `out`, which might have been one of
 * them. The same arithmetic in the same order: the same samples (28/10). */
static void chain_process(Chain *c, const int32_t *mono, int32_t *out, int frames)
{
    int32_t lg = c->pan <= 0 ? 64 : 64 - c->pan;
    int32_t rg = c->pan >= 0 ? 64 : 64 + c->pan;
    int chunk = 0;
    uint8_t stage = c->stage;
    int32_t env = c->env, trem = c->trem, dc_x1 = c->dc_x1, dc_y1 = c->dc_y1;
    int32_t ic1 = c->ic1, ic2 = c->ic2, a1 = c->a1, a2 = c->a2, a3 = c->a3;
    const int32_t sustain = c->sustain, atk_inc = c->atk_inc, dec_inc = c->dec_inc, rel_inc = c->rel_inc;
    const int32_t trem_target = c->trem_target, play = (int32_t)c->play;
    const uint32_t vol = c->vol, out_gain = c->out_gain;
    const uint32_t crush_levels = c->crush_levels, crush_step = c->crush_step, crush_hold = c->crush_hold;
    uint32_t crush_phase = c->crush_phase;
    int32_t crush_held = c->crush_held;
    const bool filtering = c->filtering;

    for (int i = 0; i < frames; i++) {
        int32_t x, y, p, post;

        switch (stage) {
        case ENV_PUNCH:
            env -= ENV_ONE / MIN_RAMP_SAMPLES;
            if (env <= 0) {
                env = 0;
                stage = ENV_ATTACK;
            }
            break;
        case ENV_ATTACK:
            env += atk_inc;
            if (env >= ENV_ONE) {
                env = ENV_ONE;
                stage = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            env -= dec_inc;
            if (env <= sustain) {
                env = sustain;
                stage = sustain > 0 ? ENV_SUSTAIN : ENV_IDLE;
            }
            break;
        case ENV_RELEASE:
            env -= rel_inc;
            if (env <= 0) {
                env = 0;
                stage = ENV_IDLE;
            }
            break;
        default:
            break;
        }
        /* The tremolo's gain is slewed over 4 ms: it is set once a tick. */
        trem += (((trem_target - trem) >> 8) * 185) >> 7;

        x = mono[i] * (1 << CH_SHIFT);
        {   /* The DC block. Its leak is at least one unit while anything
             * is left: a shift alone stalls under 1024 and a positive
             * offset would stand there, 64 steps of DC behind every note. */
            int32_t leak = dc_y1 >> 10;
            if (leak == 0 && dc_y1 > 0)
                leak = 1;
            y = x - dc_x1 + dc_y1 - leak;
        }
        dc_x1 = x;
        dc_y1 = y;
        if (filtering) {
            int32_t v1, v2, v3;
            if (chunk == 0) { /* the coefficients follow the envelope: c sees it as it is now */
                c->env = env;
                update_coefs(c);
                a1 = c->a1, a2 = c->a2, a3 = c->a3;
                chunk = COEF_CHUNK;
            }
            chunk--;
            v3 = y - ic2;
            v1 = (int32_t)(((int64_t)a1 * ic1 + (int64_t)a2 * v3) >> 20);
            v2 = ic2 + (int32_t)(((int64_t)a2 * ic1 + (int64_t)a3 * v3) >> 20);
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            y = v2;
        }
        /* The note as PLAYED: envelope, velocity, instrument gain. */
        p = mul_q(mul_q(y, env >> 14, 16), play, 16);
        if (crush_hold > 256) {
            crush_phase += 256;
            if (crush_phase >= crush_hold) {
                crush_phase -= crush_hold;
                crush_held = p;
            }
            p = crush_held;
        }
        if (crush_levels) {
            int32_t q = (int32_t)(((int64_t)p * crush_levels + (1 << 26)) >> 27);
            p = (int32_t)(((int64_t)q * crush_step) >> (8 - CH_SHIFT));
        }
        /* Then the fader, the tremolo and the instrument's own level. */
        post = mul_q((int32_t)((vol * (uint32_t)(trem >> 9)) >> 15), (int32_t)out_gain, 15);
        p = mul_q(p, post, 16);
        out[2 * i] = sat_add(out[2 * i], pan_q(p, lg));
        out[2 * i + 1] = sat_add(out[2 * i + 1], pan_q(p, rg));
    }
    c->stage = stage;
    c->env = env;
    c->trem = trem;
    c->dc_x1 = dc_x1, c->dc_y1 = dc_y1;
    c->ic1 = ic1, c->ic2 = ic2, c->a1 = a1, c->a2 = a2, c->a3 = a3;
    c->crush_phase = crush_phase;
    c->crush_held = crush_held;
}

/* ============================================================ state === */
typedef struct {
    Osc osc;
    bool active;   /* note held: what a note-off ends */
    bool sounding; /* still drawing: it outlives the hold by the release */
    uint8_t wave;  /* 0 = the note itself, 1.. = its layers */
    int8_t vox;    /* the VOX voice it sings through, -1 none (below) */
    int16_t detune;
} Voice;

typedef struct {
    int32_t val, inc; /* Q16 */
    uint16_t left;
    uint8_t to;
} Ramp;

enum { RAMP_VOL, RAMP_VEL, RAMP_PAN, RAMP_REV, RAMP_ECH, RAMP_FLT, RAMP_RES,
       RAMP_CRS, RAMP_RAT, RAMP_COUNT };

typedef struct {
    uint8_t volume, velocity, send, echo;
    int8_t duty, shape;
    int16_t cutoff, reso;
    uint8_t crs, rat;
    uint8_t delay_tick;
    int16_t cut_tick;
    uint8_t retrig;
    bool pending;
    uint8_t gate, gate_left;
    uint8_t strum, strum_gap, strum_left, strum_next;
    Step held;
    bool has_held;
    int16_t transpose;
    uint16_t glide;
    uint8_t vib, trem;
    int16_t sweep;
    uint8_t scoop;
    Ramp ramp[RAMP_COUNT];
    uint8_t arp, arp_idx, arp_count;
    int32_t glide_off;
    uint16_t glide_rate;
    int32_t bend;
    uint16_t vib_phase, trem_phase;
    uint8_t last_note;
    char syl[VOX_SYL_LEN]; /* what the next VOX note sings: a setter */
    bool syl_written;      /* the step being played wrote it in a cell */
    bool anticipated;      /* the next row's note was fired early */
} TrackFx;

typedef struct {
    Chain chain;
    Blep blep;
    Voice voices[TRACK_MAX_VOICES];
    uint8_t last_inst;
    bool used;   /* has played since playback started */
    bool silent; /* was skipped by the mix on the previous block */
    TrackFx fx;
    TrackInit seen;
    uint8_t sec_level;
} Track;

/* One more than the song has: the last is the audition, a track no
 * phrase plays -- a key under the hand, on the instrument being set. */
#define AUDITION SONG_MAX_TRACKS
static Track tracks[SONG_MAX_TRACKS + 1]
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;
static Song *song;

/* The VOX voices. moondaw gives each of a track's 20 voices a voice
 * state of its own; here eight are shared by every track and lent to a
 * voice when it sings, ~1 KB each. A voice keeps the one it was lent
 * after its note -- the syllable runs to its end, the coda included, as
 * moondaw's does -- until another voice needs it: a free one first,
 * then one that has finished singing, then one whose note is no longer
 * held. A ninth note held at once is not sung. */
#define VOX_VOICES 8
static Vox vox_pool[VOX_VOICES]
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;
static int16_t vox_owner[VOX_VOICES]; /* track * TRACK_MAX_VOICES + voice, -1 */

static bool playing, song_mode, song_once;
static uint16_t arr_pos, arr_play, ref_nth, entry_step;
static uint8_t tick_in_step, tick_parity;
static uint16_t bpm;
static int32_t to_next_tick;
static int32_t master = 1024; /* Q10 */

/* For the screen: single words, written by the audio side only. */
static volatile uint8_t ui_playing;
static volatile uint16_t ui_row, ui_entry;
static volatile uint8_t mute_mask, solo_mask;
static volatile uint32_t loop_word; /* from | len << 16: one word, so the audio side never sees half of it */

/* Main loop -> audio. One writer, one reader, eight slots. */
enum { CMD_PLAY_SONG = 1, CMD_PLAY_ENTRY, CMD_STOP, CMD_NOTE_ON, CMD_NOTE_OFF };
static volatile struct { uint8_t type, a, b; } cmd_q[8];
static volatile uint8_t cmd_w, cmd_r;

static void cmd_push(uint8_t type, uint8_t a, uint8_t b)
{
    uint8_t w = cmd_w;
    if ((uint8_t)(w - cmd_r) >= 8)
        return;
    cmd_q[w & 7].type = type;
    cmd_q[w & 7].a = a;
    cmd_q[w & 7].b = b;
    cmd_w = (uint8_t)(w + 1);
}

/* =========================================================== voices === */
static int instr_waves(const InstrumentDef *in)
{
    if (!engine_sounds(in->engine))
        return 1;
    return 1 + (in->nlayers > INSTR_MAX_LAYERS ? INSTR_MAX_LAYERS : in->nlayers);
}

static void vox_unbind(Voice *v)
{
    if (v->vox < 0)
        return;
    vox_reset(&vox_pool[v->vox]);
    vox_owner[v->vox] = -1;
    v->vox = -1;
}

static Vox *vox_bind(int t, int vi)
{
    Voice *v = &tracks[t].voices[vi];
    int16_t id = (int16_t)(t * TRACK_MAX_VOICES + vi);
    int pick = -1;

    if (v->vox >= 0)
        return &vox_pool[v->vox];
    for (int k = 0; k < VOX_VOICES && pick < 0; k++)
        if (vox_owner[k] < 0)
            pick = k;
    for (int k = 0; k < VOX_VOICES && pick < 0; k++)
        if (!vox_pool[k].playing)
            pick = k;
    for (int k = 0; k < VOX_VOICES && pick < 0; k++)
        if (!tracks[vox_owner[k] / TRACK_MAX_VOICES].voices[vox_owner[k] % TRACK_MAX_VOICES].active)
            pick = k;
    if (pick < 0)
        return 0;
    if (vox_owner[pick] >= 0)
        tracks[vox_owner[pick] / TRACK_MAX_VOICES].voices[vox_owner[pick] % TRACK_MAX_VOICES].vox = -1;
    vox_owner[pick] = id;
    v->vox = (int8_t)pick;
    vox_reset(&vox_pool[pick]);
    return &vox_pool[pick];
}

/* A note-off: a VOX voice closes its syllable and runs on to its end. */
static void voice_off(Track *tr, Voice *v)
{
    osc_note_off(&v->osc, &tr->blep);
    if (v->vox >= 0)
        vox_note_off(&vox_pool[v->vox]);
}

static void voice_stop(Track *tr, Voice *v)
{
    voice_off(tr, v);
    v->active = false;
    v->sounding = false;
}

/* Changing engine leaves the VOX voice behind, put away clean. */
static void voice_select(Track *tr, Voice *v, uint8_t engine)
{
    if (engine != ENGINE_VOX)
        vox_unbind(v);
    osc_select(&v->osc, &tr->blep, engine);
}

static void track_release(Track *tr)
{
    for (int v = 0; v < TRACK_MAX_VOICES; v++)
        tr->voices[v].active = false;
    chain_note_off(&tr->chain);
}

static void track_silence(Track *tr)
{
    for (int v = 0; v < TRACK_MAX_VOICES; v++)
        voice_stop(tr, &tr->voices[v]);
    chain_note_off(&tr->chain);
}

/* Once the envelope has closed, what was only ringing stops drawing. */
static void track_reap(Track *tr)
{
    if (tr->chain.stage != ENV_IDLE || tr->chain.env > 0)
        return;
    for (int v = 0; v < TRACK_MAX_VOICES; v++)
        if (tr->voices[v].sounding && !tr->voices[v].active)
            voice_stop(tr, &tr->voices[v]);
}

static uint8_t track_level(const Track *tr)
{
    unsigned v = (unsigned)(tr->seen.volume * tr->fx.volume + VOL_FULL / 2) / VOL_FULL;
    return (uint8_t)((v * tr->sec_level + VOL_FULL / 2) / VOL_FULL);
}

static uint8_t voice_timbre(const Track *tr, const InstrumentDef *in)
{
    if (in->engine == ENGINE_PULSE && tr->fx.duty >= 0)
        return (uint8_t)tr->fx.duty;
    if (in->engine == ENGINE_WAVE && tr->fx.shape >= 0)
        return (uint8_t)tr->fx.shape;
    return in->timbre;
}

/* `tied` is SLI's tie, the chain's own expression: reached by a glide,
 * a VOX vowel carries on instead of striking again. */
static void voice_start(Track *tr, const InstrumentDef *in, uint8_t timbre,
                        int i, uint8_t note, bool tied)
{
    int waves = instr_waves(in);
    Voice *v = &tr->voices[i * waves];

    voice_select(tr, v, in->engine);
    v->wave = 0;
    v->detune = 0;
    v->osc.gain = 4096;
    osc_set_timbre(&v->osc, timbre);
    osc_note_on(&v->osc, note);
    if (in->engine == ENGINE_VOX) {
        /* The syllable and the voice cannot travel with the note, so
         * they are handed over beside it, as moondaw does. */
        Vox *x = vox_bind((int)(tr - tracks), i * waves);
        if (x) {
            vox_set_syllable(x, tr->fx.syl, tied, tr->fx.syl_written);
            vox_set_voice(x, in->tract, in->accent, tr->fx.vib == 0);
            vox_set_timbre(x, timbre);
            vox_note_on(x, note);
        }
    }
    v->active = v->sounding = true;
    for (int l = 0; l < waves - 1; l++) {
        const Layer *ly = &in->layers[l];
        Voice *lv = &tr->voices[i * waves + 1 + l];
        int n = (int)note + ly->transpose;
        uint32_t g = db_gain(ly->gain) >> 4;
        n = n < 0 ? 0 : n > 127 ? 127 : n;
        voice_select(tr, lv, ly->engine);
        lv->wave = (uint8_t)(1 + l);
        lv->detune = ly->detune;
        /* Four times unity at most: the steps of a whole track share one
         * 32-bit buffer. moondaw lets a layer go to +24 dB. */
        lv->osc.gain = (uint16_t)(g > 16384 ? 16384 : g);
        osc_set_timbre(&lv->osc, ly->timbre);
        osc_note_on(&lv->osc, (uint8_t)n);
        lv->active = lv->sounding = true;
    }
}

static void trigger_step(int t, const Step *st)
{
    Track *tr = &tracks[t];
    uint8_t inst = st->instrument != INSTR_NONE ? st->instrument : tr->last_inst;
    const InstrumentDef *in = &song->instruments[inst % SONG_MAX_INSTR];
    uint8_t timbre, first, semis;
    int waves = instr_waves(in), maxn = TRACK_MAX_VOICES / waves, first_idx = -1;
    bool started[TRACK_MAX_VOICES] = { false }, sounded = false;
    /* SLI's tie, computed as the chain's below: the two must agree. */
    bool tied = (tr->fx.scoop & 0x0F) == 0 && tr->fx.glide > 0 && tr->used;

    tr->last_inst = inst;
    timbre = voice_timbre(tr, in);
    for (int i = 0; i < STEP_MAX_NOTES; i++) {
        uint8_t n = st->notes[i];
        /* Under ARP one voice sounds; under STR the first comes now and
         * the others in their turn. */
        bool want = n <= 127 && i < maxn && (tr->fx.arp == 0 || !sounded);
        if (want && !(sounded && tr->fx.strum > 0)) {
            voice_start(tr, in, timbre, i, n, tied);
            for (int w = 0; w < waves; w++)
                started[i * waves + w] = true;
            if (!sounded)
                first_idx = i;
            sounded = true;
        }
    }
    for (int v = 0; v < TRACK_MAX_VOICES; v++)
        if (!started[v] && tr->voices[v].sounding)
            voice_stop(tr, &tr->voices[v]);

    tr->fx.strum_gap = tr->fx.arp == 0 ? tr->fx.strum : 0;
    tr->fx.strum_next = (uint8_t)(first_idx + 1);
    tr->fx.strum_left = 0;
    if (tr->fx.strum_gap > 0)
        for (int i = tr->fx.strum_next; i < STEP_MAX_NOTES; i++)
            if (st->notes[i] <= 127) {
                tr->fx.strum_left = tr->fx.strum_gap;
                break;
            }
    tr->fx.arp_idx = 0;
    tr->fx.arp_count = 0;

    /* Where the pitch starts from: a scoop from below, a glide from the
     * note before, or the note itself. A glide ties the envelope. */
    first = st->notes[0] <= 127 ? st->notes[0] : tr->fx.last_note;
    semis = tr->fx.scoop & 0x0F;
    tied = false;
    if (semis > 0) {
        tr->fx.glide_off = -(int32_t)semis * 100;
        tr->fx.glide_rate = (uint16_t)(((tr->fx.scoop >> 4) + 1) * 16);
    } else if (tr->fx.glide > 0 && tr->used) {
        tr->fx.glide_off = ((int32_t)tr->fx.last_note - (int32_t)first) * 100;
        tr->fx.glide_rate = tr->fx.glide;
        tied = true;
    } else {
        tr->fx.glide_off = 0;
        tr->fx.glide_rate = 0;
    }
    tr->fx.last_note = first;
    tr->fx.bend = 0;
    if (in->vib_phase != VIB_PHASE_FREE)
        tr->fx.vib_phase = (uint16_t)((uint16_t)in->vib_phase << 8);

    {
        uint8_t cutoff = tr->fx.cutoff >= 0 ? (uint8_t)tr->fx.cutoff : in->cutoff;
        uint8_t reso = tr->fx.reso >= 0 ? (uint8_t)tr->fx.reso : in->reso;
        if (tied)
            chain_note_legato(&tr->chain, track_level(tr), tr->fx.velocity, in,
                              cutoff, reso);
        else
            chain_note_on(&tr->chain, track_level(tr), tr->fx.velocity, in,
                          cutoff, reso);
    }
    tr->fx.gate_left = tr->fx.gate;
    tr->used = true;
}

static void retrigger_step(int t)
{
    Track *tr = &tracks[t];
    for (int i = 0; i < TRACK_MAX_VOICES; i++)
        if (tr->voices[i].sounding)
            voice_off(tr, &tr->voices[i]);
    chain_retrigger(&tr->chain);
    trigger_step(t, &tr->fx.held);
}

static void strum_advance(int t)
{
    Track *tr = &tracks[t];
    TrackFx *fx = &tr->fx;
    const InstrumentDef *in = &song->instruments[tr->last_inst % SONG_MAX_INSTR];
    int maxn = TRACK_MAX_VOICES / instr_waves(in), i;

    if (maxn > STEP_MAX_NOTES)
        maxn = STEP_MAX_NOTES;
    while (fx->strum_next < maxn && fx->held.notes[fx->strum_next] > 127)
        fx->strum_next++;
    fx->strum_left = 0;
    if (fx->strum_next >= maxn)
        return;
    i = fx->strum_next++;
    voice_start(tr, in, voice_timbre(tr, in), i, fx->held.notes[i], false);
    for (int j = fx->strum_next; j < STEP_MAX_NOTES; j++)
        if (fx->held.notes[j] <= 127) {
            fx->strum_left = fx->strum_gap;
            break;
        }
}

/* ============================================================ pitch === */
#define PITCH_LIMIT 9600 /* 8 octaves either way: a runaway sweep stops */

static int clamp_pitch(int c)
{
    return c > PITCH_LIMIT ? PITCH_LIMIT : c < -PITCH_LIMIT ? -PITCH_LIMIT : c;
}

static void apply_pitch(Track *tr)
{
    TrackFx *fx = &tr->fx;
    int arp = 0, vibrato = 0, cents;
    uint8_t depth = fx->vib & 0x0F;

    if (fx->glide_off > 0) {
        fx->glide_off -= fx->glide_rate;
        if (fx->glide_off < 0)
            fx->glide_off = 0;
    } else if (fx->glide_off < 0) {
        fx->glide_off += fx->glide_rate;
        if (fx->glide_off > 0)
            fx->glide_off = 0;
    }
    fx->bend = clamp_pitch(fx->bend + fx->sweep);

    if (fx->arp > 0 && fx->has_held) {
        uint8_t list[STEP_MAX_NOTES];
        int count = 0;
        for (int i = 0; i < STEP_MAX_NOTES; i++)
            if (fx->held.notes[i] <= 127)
                list[count++] = fx->held.notes[i];
        if (count > 1) {
            if (++fx->arp_count >= fx->arp) {
                fx->arp_count = 0;
                fx->arp_idx = (uint8_t)((fx->arp_idx + 1) % count);
            }
            arp = ((int)list[fx->arp_idx] - (int)list[0]) * 100;
        }
    }
    if (depth > 0) {
        fx->vib_phase = (uint16_t)(fx->vib_phase + ((fx->vib >> 4) + 1) * 1024);
        vibrato = sine_q15(fx->vib_phase) * depth * 8 / 32768;
    }
    cents = clamp_pitch(fx->transpose + (int)fx->glide_off + (int)fx->bend +
                        vibrato + arp);
    for (int v = 0; v < TRACK_MAX_VOICES; v++) {
        osc_set_cents(&tr->voices[v].osc, clamp_pitch(cents + tr->voices[v].detune));
        if (tr->voices[v].vox >= 0)
            vox_set_cents(&vox_pool[tr->voices[v].vox], clamp_pitch(cents + tr->voices[v].detune));
    }
}

/* The lopsided throb of a bias tremolo: a quick rounded dip, then the
 * sound lingers near the top. The phase runs free. */
static void apply_trem(Track *tr)
{
    TrackFx *fx = &tr->fx;
    uint8_t depth = fx->trem & 0x0F;
    int32_t w;

    if (depth == 0) {
        tr->chain.trem_target = 1 << 24;
        return;
    }
    fx->trem_phase = (uint16_t)(fx->trem_phase + ((fx->trem >> 4) + 1) * 1024);
    w = (32768 - sine_q15((uint16_t)(fx->trem_phase + 16384))) >> 1;
    w = (w * w) >> 15;
    tr->chain.trem_target = (32768 - depth * w / 15) << 9;
}

/* ============================================================ ramps === */
static int ramp_current(const Track *tr, int which)
{
    switch (which) {
    case RAMP_VOL: return tr->fx.volume;
    case RAMP_VEL: return tr->fx.velocity;
    case RAMP_PAN: return tr->chain.pan + 64;
    case RAMP_REV: return tr->fx.send;
    case RAMP_ECH: return tr->fx.echo;
    case RAMP_FLT: return tr->fx.cutoff;
    case RAMP_RES: return tr->fx.reso;
    case RAMP_CRS: return tr->fx.crs;
    case RAMP_RAT: return tr->fx.rat;
    default: return -1;
    }
}

static void ramp_apply(Track *tr, int which, uint8_t v)
{
    switch (which) {
    case RAMP_VOL:
        tr->fx.volume = v;
        chain_set_volume(&tr->chain, track_level(tr));
        break;
    case RAMP_VEL:
        tr->fx.velocity = v;
        break;
    case RAMP_PAN:
        tr->chain.pan = (int8_t)((int)v - 64);
        break;
    case RAMP_REV:
        tr->fx.send = v;
        break;
    case RAMP_ECH:
        tr->fx.echo = v;
        break;
    case RAMP_FLT:
        tr->fx.cutoff = v;
        tr->chain.cutoff = v;
        chain_bypass(&tr->chain);
        break;
    case RAMP_RES:
        tr->fx.reso = v;
        tr->chain.reso = v;
        chain_bypass(&tr->chain);
        break;
    case RAMP_CRS:
        tr->fx.crs = v;
        chain_set_crush(&tr->chain, tr->fx.crs, tr->fx.rat);
        break;
    case RAMP_RAT:
        tr->fx.rat = v;
        chain_set_crush(&tr->chain, tr->fx.crs, tr->fx.rat);
        break;
    default:
        break;
    }
}

/* Arms a ramp to `to` over `steps`, or says the value should jump. */
static bool ramp_start(Track *tr, int which, uint8_t to, int steps)
{
    Ramp *r = &tr->fx.ramp[which];
    int from = ramp_current(tr, which);
    uint32_t ticks = (uint32_t)steps * (song->ticks_per_step ? song->ticks_per_step : 1);

    if (steps <= 0 || from < 0 || ticks == 0 || from == to) {
        r->left = 0;
        return false;
    }
    if (ticks > 0xFFFF)
        ticks = 0xFFFF;
    r->val = from << 16;
    r->to = to;
    r->left = (uint16_t)ticks;
    r->inc = (((int32_t)to - from) << 16) / (int32_t)ticks;
    return true;
}

/* ====================================================== rows and fx === */
static void apply_fx(int t, const Step *st)
{
    Track *tr = &tracks[t];
    int ramp_steps = 0;

    for (int i = 0; i < STEP_MAX_FX; i++) {
        uint8_t cmd = st->fx[i].cmd;
        uint8_t v = fx_clamp_val(cmd, song->ticks_per_step, st->fx[i].val);
        int slot = fx_ramp_slot(cmd);

        if (slot >= 0) {
            /* A continuous setter: ramped when an RMP stands to its left. */
            if (!ramp_start(tr, slot, v, ramp_steps))
                ramp_apply(tr, slot, v);
            continue;
        }
        switch (cmd) {
        case FX_DLY: tr->fx.delay_tick = v; break;
        case FX_CUT: tr->fx.cut_tick = v; break;
        case FX_GAT: tr->fx.gate = v; break;
        case FX_RET: tr->fx.retrig = v; break;
        case FX_STR: tr->fx.strum = v; break;
        case FX_RMP: ramp_steps = v; break;
        case FX_DUT:
            tr->fx.duty = (int8_t)v;
            for (int j = 0; j < TRACK_MAX_VOICES; j++)
                if (tr->voices[j].wave == 0 && tr->voices[j].osc.engine == ENGINE_PULSE)
                    osc_set_timbre(&tr->voices[j].osc, v);
            break;
        case FX_WAV:
            tr->fx.shape = (int8_t)v;
            for (int j = 0; j < TRACK_MAX_VOICES; j++)
                if (tr->voices[j].wave == 0 && tr->voices[j].osc.engine == ENGINE_WAVE)
                    osc_set_timbre(&tr->voices[j].osc, v);
            break;
        case FX_BPM: bpm = v < 20 ? 20 : v; break;
        case FX_TSP: tr->fx.transpose = (int16_t)(((int)v - 64) * 100); break;
        case FX_SLI: tr->fx.glide = (uint16_t)(v * 16); break;
        case FX_VIB: tr->fx.vib = v; break;
        case FX_TRM: tr->fx.trem = v; break;
        case FX_SWP: tr->fx.sweep = (int16_t)(((int)v - 64) * 32); break;
        case FX_SCP: tr->fx.scoop = v; break;
        case FX_ARP:
            if (v > 0 && tr->fx.arp == 0) {
                /* The step goes monophonic: only the first note's waves stay. */
                int keep = instr_waves(&song->instruments[tr->last_inst % SONG_MAX_INSTR]);
                for (int j = keep; j < TRACK_MAX_VOICES; j++)
                    if (tr->voices[j].sounding)
                        voice_stop(tr, &tr->voices[j]);
            }
            tr->fx.arp = v;
            tr->fx.arp_idx = 0;
            tr->fx.arp_count = 0;
            break;
        default:
            break;
        }
    }
}

/* The instrument in force at `row` of a block: the last one written at
 * or above it. A retrigger or a strum replays the held step and must
 * find the instrument the row inherited. */
static uint8_t block_instrument(int t, int pat, int row)
{
    for (int r = row; r >= 0; r--) {
        uint8_t in = song_step(song, t, pat, r)->instrument;
        if (in != INSTR_NONE)
            return in;
    }
    return INSTR_NONE;
}

static void syl_copy(char *dst, const char *src)
{
    int i = 0;
    for (; i < VOX_SYL_LEN - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void process_row(const SongEntry *e)
{
    for (int t = 0; t < song->num_tracks; t++) {
        Track *tr = &tracks[t];
        const Step *st;
        int pat = e->pattern[t], len, row;
        bool any = false;

        /* The one-shots end with their step. */
        tr->fx.delay_tick = 0;
        tr->fx.cut_tick = -1;
        tr->fx.retrig = 0;
        tr->fx.pending = false;
        tr->fx.gate = 0;
        tr->fx.strum = 0;
        tr->fx.scoop = 0;
        if (pat >= TRACK_MAX_PATTERNS || song->patterns[t][pat].length == 0) {
            tr->fx.anticipated = false;
            tr->fx.has_held = false;
            tr->fx.strum_left = 0;
            continue;
        }
        len = song->patterns[t][pat].length;
        row = entry_step % len;
        st = song_step(song, t, pat, row);
        /* The syllable, copied: the table may be edited while this plays. */
        tr->fx.syl_written = false;
        for (int i = 0; i < STEP_MAX_FX; i++)
            if (st->fx[i].cmd == FX_SYL && song_syl(song, st->fx[i].val)[0]) {
                syl_copy(tr->fx.syl, song_syl(song, st->fx[i].val));
                tr->fx.syl_written = true;
            }
        apply_fx(t, st);
        if (st->notes[0] == NOTE_OFF) {
            tr->fx.anticipated = false;
            track_release(tr);
            tr->fx.has_held = false;
            tr->fx.strum_left = 0;
            continue;
        }
        for (int i = 0; i < STEP_MAX_NOTES; i++)
            any |= st->notes[i] <= 127;
        if (!any) {
            tr->fx.anticipated = false; /* the note it aimed at is gone */
            continue; /* empty step: the previous note carries on */
        }
        if (tr->fx.anticipated) {
            /* Fired early so the vowel lands here: the strike is done,
             * the row keeps its fx, applied above. */
            tr->fx.anticipated = false;
            tr->fx.strum_left = 0;
            continue;
        }
        tr->fx.held = *st;
        tr->fx.held.instrument = block_instrument(t, pat, row);
        tr->fx.has_held = true;
        tr->fx.pending = true;
        tr->fx.strum_left = 0;
    }
}

/* A fader moved while the song plays is heard at once, and takes back
 * whatever a step had written over it. */
static void track_follow_mixer(Track *tr, const TrackInit *ti)
{
    if (ti->volume != tr->seen.volume) {
        tr->seen.volume = ti->volume;
        chain_set_volume(&tr->chain, track_level(tr));
    }
    if (ti->pan != tr->seen.pan) {
        tr->seen.pan = ti->pan;
        tr->fx.ramp[RAMP_PAN].left = 0;
        tr->chain.pan = ti->pan;
    }
    if (ti->send != tr->seen.send) {
        tr->seen.send = ti->send;
        tr->fx.ramp[RAMP_REV].left = 0;
        tr->fx.send = ti->send;
    }
    if (ti->echo != tr->seen.echo) {
        tr->seen.echo = ti->echo;
        tr->fx.ramp[RAMP_ECH].left = 0;
        tr->fx.echo = ti->echo;
    }
}

/* ========================================================== singing ===
 * moondaw's two look-aheads for the voice, decided several steps out:
 * the mouth closes what it sings before the next row takes the voice
 * (close_syllable), and a consonant is formed BEFORE the beat so the
 * vowel lands on it (anticipate_vox). Both are quantized to the tick,
 * an fDLY on the row is the hand taking the timing back, and a phrase
 * boundary is not crossed in song mode. */
#define VOX_BEAT_LEAD_MS 50

static int32_t tick_samples(void);
static bool loop_bounds(uint16_t cycle, uint16_t *lo, uint16_t *hi);

/* ms in ticks, rounded: (int)(ms * FS / tick + .5) for ms >= 0. */
static int ms_ticks(int ms, int32_t tick)
{
    return (int)(((int32_t)ms * 441 + 5 * tick) / (10 * tick));
}

static int track_block(const SongEntry *e, int t)
{
    int pat = e->pattern[t];
    return pat < TRACK_MAX_PATTERNS && song->patterns[t][pat].length > 0 ? pat : -1;
}

/* The row d steps ahead, NULL past the end of what plays. A loop's seam
 * is crossed, as moondaw's step_ahead does it. */
static const Step *step_ahead(const SongEntry *e, int t, int pat, int d, int *row)
{
    uint16_t cycle = song_entry_cycle(song, e), lo, hi;
    uint32_t next = (uint32_t)entry_step + (uint32_t)d;
    if (song_mode) {
        if (next >= song_play_steps(song, arr_pos, arr_play))
            return 0;
    } else if (loop_bounds(cycle, &lo, &hi)) {
        uint16_t len = hi > lo ? (uint16_t)(hi - lo) : 1;
        if (next >= hi || next < lo)
            next = lo + (next - lo) % len;
    } else if (next >= cycle) {
        next = 0;
    }
    *row = (int)(next % song->patterns[t][pat].length);
    return song_step(song, t, pat, *row);
}

/* The row one step ahead, as moondaw's peek_next: a loop goes back to
 * its first row. */
static const Step *peek_next(const SongEntry *e, int t, int pat, int *row)
{
    uint16_t cycle = song_entry_cycle(song, e), lo, hi;
    uint32_t next = (uint32_t)entry_step + 1;
    if (song_mode) {
        if (next >= song_play_steps(song, arr_pos, arr_play))
            return 0;
    } else if (loop_bounds(cycle, &lo, &hi)) {
        if (next >= hi || next < lo)
            next = lo;
    } else if (next >= cycle) {
        next = 0;
    }
    *row = (int)(next % song->patterns[t][pat].length);
    return song_step(song, t, pat, *row);
}

/* What a row ahead will sing: its own cell, else the track's setter. */
static const char *row_syl(const Track *tr, const Step *st)
{
    const char *syl = tr->fx.syl;
    for (int i = 0; i < STEP_MAX_FX; i++)
        if (st->fx[i].cmd == FX_SYL && song_syl(song, st->fx[i].val)[0])
            syl = song_syl(song, st->fx[i].val);
    return syl;
}

static bool row_has(const Step *st, uint8_t cmd)
{
    for (int i = 0; i < STEP_MAX_FX; i++)
        if (st->fx[i].cmd == cmd)
            return true;
    return false;
}

static bool row_has_note(const Step *st)
{
    for (int i = 0; i < STEP_MAX_NOTES; i++)
        if (st->notes[i] <= 127)
            return true;
    return false;
}

/* Whether that row will be TIED: a glide reaches it, its own SLI and SCP
 * winning over the track's, since its fx have not been applied yet. */
static bool row_tied(const Track *tr, const Step *st, uint16_t *glide, uint8_t *scoop)
{
    *glide = tr->fx.glide;
    *scoop = 0;
    for (int i = 0; i < STEP_MAX_FX; i++) {
        if (st->fx[i].cmd == FX_SLI)
            *glide = (uint16_t)(fx_clamp_val(FX_SLI, song->ticks_per_step, st->fx[i].val) * 16);
        if (st->fx[i].cmd == FX_SCP)
            *scoop = fx_clamp_val(FX_SCP, song->ticks_per_step, st->fx[i].val);
    }
    return (*scoop & 0x0F) == 0 && *glide > 0 && tr->used;
}

/* Ends the syllable being sung without touching the envelope. */
static void release_vox_engines(Track *tr)
{
    for (int v = 0; v < TRACK_MAX_VOICES; v++)
        if (tr->voices[v].active && tr->voices[v].vox >= 0)
            vox_note_off(&vox_pool[tr->voices[v].vox]);
}

/* A coda sings at the END of its note: the voice is released early by
 * the coda's length, plus the onset of a next note that will itself be
 * fired early, and the wait may span several steps. */
static void close_syllable(int t)
{
    Track *tr = &tracks[t];
    const SongEntry *e = &song->arrangement[arr_play];
    int32_t tick;
    int coda, pat, nc, span;

    if (tr->fx.anticipated || tr->fx.pending)
        return;
    coda = vox_coda_ms(tr->fx.syl);
    if (coda <= 0 || (pat = track_block(e, t)) < 0)
        return;
    tick = tick_samples();
    nc = ms_ticks(coda, tick);
    span = nc / song->ticks_per_step + 2;
    for (int d = 1; d <= span; d++) {
        int row;
        const Step *st = step_ahead(e, t, pat, d, &row);
        bool note;
        if (!st)
            return;
        note = row_has_note(st);
        if (st->notes[0] != NOTE_OFF && !note)
            continue;
        if (row_has(st, FX_DLY))
            return;
        if (note) {
            uint16_t glide;
            uint8_t scoop, inst = block_instrument(t, pat, row);
            int lead;
            if (row_tied(tr, st, &glide, &scoop) && row_syl(tr, st) == tr->fx.syl)
                return; /* a melisma: the vowel runs on, no coda */
            if (inst == INSTR_NONE)
                inst = tr->last_inst;
            lead = vox_onset_ms(row_syl(tr, st), song->instruments[inst % SONG_MAX_INSTR].accent) -
                   VOX_BEAT_LEAD_MS;
            if (lead > 0)
                nc += ms_ticks(lead, tick);
        }
        if (d * song->ticks_per_step - (int)tick_in_step <= nc)
            release_vox_engines(tr);
        return;
    }
}

/* The singer's anticipation: when the next row will be sung, its note is
 * fired early by the length of its unvoiced head less 50 ms, so the
 * note WRITTEN on the tick is the vowel HEARD on it. */
static void anticipate_vox(int t)
{
    Track *tr = &tracks[t];
    const SongEntry *e = &song->arrangement[arr_play];
    const Step *st;
    const char *syl;
    const InstrumentDef *in;
    uint16_t glide;
    uint8_t scoop, inst;
    int pat, row, lead, n;
    bool wrote, tied;

    if (tr->fx.anticipated || tr->fx.pending || (pat = track_block(e, t)) < 0)
        return;
    st = peek_next(e, t, pat, &row);
    if (!st || st->notes[0] == NOTE_OFF || !row_has_note(st))
        return;
    inst = block_instrument(t, pat, row);
    if (inst == INSTR_NONE)
        inst = tr->last_inst;
    in = &song->instruments[inst % SONG_MAX_INSTR];
    if (in->engine != ENGINE_VOX || row_has(st, FX_DLY))
        return;
    syl = row_syl(tr, st);
    wrote = syl != tr->fx.syl;
    tied = row_tied(tr, st, &glide, &scoop);
    if (tied && !wrote)
        return; /* a melisma forms nothing to be early for */
    lead = vox_onset_ms(syl, in->accent) - VOX_BEAT_LEAD_MS;
    if (lead <= 0)
        return;
    n = ms_ticks(lead, tick_samples());
    if (n <= 0)
        return;
    if (n > song->ticks_per_step)
        n = song->ticks_per_step;
    if ((int)tick_in_step != (int)song->ticks_per_step - n)
        return;

    /* Fired as process_row would have, with what comes along early: the
     * syllable, VEL and GAT, and the pitch fx the note-on reads. */
    tr->fx.syl_written = wrote;
    if (wrote)
        syl_copy(tr->fx.syl, syl);
    tr->fx.glide = glide;
    tr->fx.scoop = scoop;
    for (int i = 0; i < STEP_MAX_FX; i++) {
        uint8_t v = fx_clamp_val(st->fx[i].cmd, song->ticks_per_step, st->fx[i].val);
        if (st->fx[i].cmd == FX_VEL)
            tr->fx.velocity = v;
        if (st->fx[i].cmd == FX_GAT)
            tr->fx.gate = v;
    }
    tr->fx.held = *st;
    tr->fx.held.instrument = inst;
    tr->fx.has_held = true;
    tr->fx.strum_left = 0;
    trigger_step(t, &tr->fx.held);
    tr->fx.anticipated = true;
}

static void process_fx_tick(void)
{
    const SongEntry *se = &song->arrangement[arr_play];

    for (int t = 0; t < song->num_tracks; t++) {
        Track *tr = &tracks[t];
        TrackFx *fx = &tr->fx;

        track_follow_mixer(tr, &song->track_init[t]);
        if (se->level[t] != tr->sec_level) {
            tr->sec_level = se->level[t];
            chain_set_volume(&tr->chain, track_level(tr));
        }
        for (int r = 0; r < RAMP_COUNT; r++) {
            Ramp *rp = &fx->ramp[r];
            if (rp->left == 0)
                continue;
            if (--rp->left == 0) {
                ramp_apply(tr, r, rp->to);
            } else {
                rp->val += rp->inc;
                ramp_apply(tr, r, (uint8_t)((rp->val + 0x8000) >> 16));
            }
        }
        if (fx->gate_left > 0 && --fx->gate_left == 0)
            track_release(tr);
        if (fx->strum_left > 0 && --fx->strum_left == 0)
            strum_advance(t);
        if (fx->pending && tick_in_step >= fx->delay_tick) {
            trigger_step(t, &fx->held);
            fx->pending = false;
        }
        close_syllable(t);
        anticipate_vox(t);
        if (fx->retrig > 0 && fx->has_held && tick_in_step > 0 &&
            tick_in_step % fx->retrig == 0)
            retrigger_step(t);
        if (fx->cut_tick >= 0 && tick_in_step == fx->cut_tick)
            track_release(tr);
        apply_pitch(tr);
        apply_trem(tr);
    }
}

/* ======================================================== transport === */
static void entry_enter(uint16_t nth)
{
    int at = song_entry_resolve(song, arr_pos, nth);
    ref_nth = at >= 0 ? nth : 0;
    arr_play = at >= 0 ? (uint16_t)at : arr_pos;
}

/* A track the new phrase leaves out is released: that is how a part
 * stops. A holding cell (`on`) is not left out. */
static void release_unmapped(void)
{
    const SongEntry *e = &song->arrangement[arr_play];
    for (int t = 0; t < song->num_tracks; t++)
        if (e->pattern[t] == PATTERN_NONE)
            track_release(&tracks[t]);
}

static void advance_entry(void)
{
    if (song_entry_resolve(song, arr_pos, ref_nth + 1) >= 0) {
        entry_enter((uint16_t)(ref_nth + 1));
        release_unmapped();
        return;
    }
    for (int guard = 0; guard < song->arrange_len; guard++) {
        if (++arr_pos >= song->arrange_len)
            arr_pos = 0; /* the song loops */
        entry_enter(0);
        if (song_entry_resolve(song, arr_pos, ref_nth) >= 0) {
            release_unmapped();
            return;
        }
    }
    entry_enter(0);
    release_unmapped();
}

static void start_play(bool as_song, int entry)
{
    bpm = song->bpm;
    song_mode = as_song;
    arr_pos = (uint16_t)(entry >= 0 && entry < song->arrange_len ? entry : 0);
    entry_step = 0;
    entry_enter(0);
    tick_in_step = 0;
    for (int t = 0; t < SONG_MAX_TRACKS; t++) {
        Track *tr = &tracks[t];
        TrackFx *fx = &tr->fx;
        track_silence(tr);
        tr->last_inst = 0;
        tr->used = false;
        tr->chain.pan = song->track_init[t].pan;
        tr->seen = song->track_init[t];
        for (uint32_t i = 0; i < sizeof(*fx); i++)
            ((uint8_t *)fx)[i] = 0;
        fx->volume = VOL_FULL;
        tr->sec_level = VOL_FULL;
        fx->velocity = VEL_FULL;
        fx->send = song->track_init[t].send;
        fx->echo = song->track_init[t].echo;
        fx->duty = -1;
        fx->shape = -1;
        fx->cutoff = -1;
        fx->reso = -1;
        fx->cut_tick = -1;
        fx->syl[0] = 'A'; /* a VOX track that never wrote a syllable sings */
        chain_set_crush(&tr->chain, 0, 0);
    }
    tick_parity = 0;
    to_next_tick = 0; /* the tick lands on this very block */
    playing = true;
    ui_playing = 1;
    ui_row = 0;
    ui_entry = arr_pos;
}

static void stop_play(void)
{
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        track_silence(&tracks[t]);
    playing = false;
    ui_playing = 0;
}

static void handle_cmds(void)
{
    while (cmd_r != cmd_w) {
        uint8_t type = cmd_q[cmd_r & 7].type, a = cmd_q[cmd_r & 7].a,
                b = cmd_q[cmd_r & 7].b;
        cmd_r = (uint8_t)(cmd_r + 1);
        if (type == CMD_PLAY_SONG) {
            start_play(true, a);
            song_once = b != 0;
        } else if (type == CMD_PLAY_ENTRY) {
            start_play(false, a);
            song_once = false;
        } else if (type == CMD_STOP) {
            stop_play();
        } else if (type == CMD_NOTE_ON) {
            /* The audition: one note and its layers, at the level moondaw
             * previews at, outside the song and its sends. */
            Track *tr = &tracks[AUDITION];
            const InstrumentDef *in = &song->instruments[a % SONG_MAX_INSTR];
            for (int v = 0; v < TRACK_MAX_VOICES; v++)
                if (tr->voices[v].sounding)
                    voice_stop(tr, &tr->voices[v]);
            voice_start(tr, in, in->timbre, 0, b, false);
            for (int v = 0; v < TRACK_MAX_VOICES; v++)
                osc_set_cents(&tr->voices[v].osc, tr->voices[v].detune);
            tr->chain.pan = 0;
            chain_note_on(&tr->chain, 100, VEL_FULL, in, in->cutoff, in->reso);
            tr->used = true;
        } else if (type == CMD_NOTE_OFF) {
            track_release(&tracks[AUDITION]);
        }
    }
}

/* The steps a looped phrase is kept to, when there is a loop and it
 * still falls inside the phrase. */
static bool loop_bounds(uint16_t cycle, uint16_t *lo, uint16_t *hi)
{
    uint32_t w = loop_word, from = w & 0xFFFFu, len = w >> 16;
    if (song_mode || len == 0 || from >= cycle)
        return false;
    *lo = (uint16_t)from;
    *hi = (uint16_t)(from + len > cycle ? cycle : from + len);
    return true;
}

static void player_tick(void)
{
    uint16_t lo, hi;
    const SongEntry *e;
    uint16_t cycle;

    if (!playing)
        return;
    /* The song may have been edited under the playhead. */
    if (arr_pos >= song->arrange_len) {
        arr_pos = 0;
        entry_step = 0;
        entry_enter(0);
    } else if (arr_play >= song->arrange_len ||
               song_entry_resolve(song, arr_pos, ref_nth) != (int)arr_play) {
        entry_enter(ref_nth);
    }
    e = &song->arrangement[arr_play];
    cycle = song_entry_cycle(song, e);
    if (tick_in_step == 0 && loop_bounds(cycle, &lo, &hi) && (entry_step < lo || entry_step >= hi))
        entry_step = lo; /* PLAY with a loop starts in it; a loop set while playing takes at once */
    tick_parity = (uint8_t)(entry_step & 1);
    if (tick_in_step == 0) {
        process_row(e);
        ui_row = (uint16_t)(entry_step % cycle);
        ui_entry = arr_pos;
    }
    process_fx_tick();
    if (++tick_in_step < song->ticks_per_step)
        return;
    tick_in_step = 0;
    entry_step++;
    if (song_mode) {
        if (entry_step >= song_play_steps(song, arr_pos, arr_play)) {
            entry_step = 0;
            if (song_once && arr_pos + 1 >= song->arrange_len &&
                song_entry_resolve(song, arr_pos, ref_nth + 1) < 0) {
                for (int t = 0; t < SONG_MAX_TRACKS; t++)
                    track_release(&tracks[t]);
                playing = false;
                song_once = false;
                ui_playing = 0;
            } else {
                advance_entry();
            }
        }
    } else if (loop_bounds(cycle, &lo, &hi)) {
        if (entry_step >= hi)
            entry_step = lo;
    } else if (entry_step >= cycle) {
        entry_step = 0; /* a phrase loops indefinitely */
        if (song_entry_resolve(song, arr_pos, ref_nth + 1) >= 0) {
            entry_enter((uint16_t)(ref_nth + 1));
            release_unmapped();
        } else if (ref_nth != 0) {
            entry_enter(0);
            release_unmapped();
        }
    }
}

/* Samples in a tick: FS * 60 / (bpm * 4 steps * ticks), which is
 * FS * 15 / (bpm * ticks). Swing stretches the even step of each pair
 * and shrinks the odd one by the same amount. */
static int32_t tick_samples(void)
{
    uint32_t num = (uint32_t)FS * 15u, den = (uint32_t)bpm * song->ticks_per_step;
    int32_t n;
    if (playing && song->swing > SWING_MIN && song->swing <= SWING_MAX) {
        num *= tick_parity == 0 ? song->swing : 100u - song->swing;
        den *= 50u;
    }
    n = (int32_t)(num / (den ? den : 1));
    return n > 0 ? n : 1;
}

/* =========================================================== render === */
uint32_t (*player_clock)(void);
static uint32_t prof_now[PROF_N], prof_worst[PROF_N + 1];
static uint32_t prof_t;
/* The time since the last mark goes to stage k. */
static void prof(int k)
{
    uint32_t t;
    if (!player_clock)
        return;
    t = player_clock();
    prof_now[k] += t - prof_t;
    prof_t = t;
}
static void prof_mark(void)
{
    if (player_clock)
        prof_t = player_clock();
}

void player_profile(uint32_t out[PROF_N + 1])
{
    for (int k = 0; k <= PROF_N; k++)
        out[k] = prof_worst[k];
}

void player_profile_reset(void)
{
    for (int k = 0; k <= PROF_N; k++)
        prof_worst[k] = 0;
}

static int32_t tmono[SYNTH_MAX_FRAMES];
static int32_t tstereo[SYNTH_MAX_FRAMES * 2];
static int32_t sendbus[SYNTH_MAX_FRAMES], echobus[SYNTH_MAX_FRAMES];
static int32_t mix[SYNTH_MAX_FRAMES * 2];

static int32_t tick_samples(void);

/* Voices whose envelope has closed are stopped once a tick, not once a
 * block: a noise register keeps shifting until then, so WHEN it stops
 * decides the hiss of the next hit, and a tick is the one boundary that
 * does not move with the size of the audio buffer. */
static bool reap_due;

/* The VOX voices a track has lent, added to what its oscillators drew. */
static void track_sing(Track *tr, int frames)
{
    for (int v = 0; v < TRACK_MAX_VOICES; v++)
        if (tr->voices[v].vox >= 0)
            vox_render(&vox_pool[tr->voices[v].vox], tmono, frames);
}

static void render_tracks(int32_t *out, int frames)
{
    bool fed = false, fed_echo = false;

    for (int t = 0; t < song->num_tracks; t++) {
        Track *tr = &tracks[t];
        /* A solo anywhere silences every track that is not one; else the mutes speak. */
        bool muted = solo_mask ? !((solo_mask >> t) & 1u) : (mute_mask >> t) & 1u;

        if (muted) {
            if (!tr->silent) {
                track_silence(tr);
                for (int v = 0; v < TRACK_MAX_VOICES; v++)
                    vox_unbind(&tr->voices[v]); /* not rendered: put away */
                tr->silent = true;
            }
            continue;
        }
        tr->silent = false;
        if (reap_due)
            track_reap(tr);
        if (!tr->used && tr->chain.env == 0)
            continue;
        prof_mark();
        blep_begin(&tr->blep, frames);
        for (int v = 0; v < TRACK_MAX_VOICES; v++)
            osc_draw(&tr->voices[v].osc, &tr->blep);
        blep_read(&tr->blep, tmono, frames);
        prof(PROF_OSC);
        track_sing(tr, frames);
        prof(PROF_VOX);
        if (tr->fx.send == 0 && tr->fx.echo == 0) {
            chain_process(&tr->chain, tmono, out, frames);
            prof(PROF_CHAIN);
            continue;
        }
        /* A track that sends is mixed apart, so that what goes to the
         * room is what the track puts out: post-fader, and mono -- the
         * pan's own gain taken back out, or a track panned hard would
         * send half of what a centred one does. */
        {
            int32_t lg = tr->chain.pan <= 0 ? 64 : 64 - tr->chain.pan;
            int32_t rg = tr->chain.pan >= 0 ? 64 : 64 + tr->chain.pan;
            int32_t g = (int32_t)tr->fx.send * 2097152 / (127 * (lg + rg));
            int32_t ge = (int32_t)tr->fx.echo * 2097152 / (127 * (lg + rg));

            for (int i = 0; i < frames * 2; i++)
                tstereo[i] = 0;
            chain_process(&tr->chain, tmono, tstereo, frames);
            if (tr->fx.send != 0 && !fed) {
                for (int i = 0; i < frames; i++)
                    sendbus[i] = 0;
                fed = true;
            }
            if (tr->fx.echo != 0 && !fed_echo) {
                for (int i = 0; i < frames; i++)
                    echobus[i] = 0;
                fed_echo = true;
            }
            for (int i = 0; i < frames; i++) {
                int32_t mono = tstereo[2 * i] + tstereo[2 * i + 1];
                out[2 * i] = sat_add(out[2 * i], tstereo[2 * i]);
                out[2 * i + 1] = sat_add(out[2 * i + 1], tstereo[2 * i + 1]);
                if (g)
                    sendbus[i] += mul_q(mono, g, 15);
                if (ge)
                    echobus[i] += mul_q(mono, ge, 15);
            }
            prof(PROF_CHAIN);
        }
    }
    {   /* the audition, dry */
        Track *tr = &tracks[AUDITION];
        if (reap_due)
            track_reap(tr);
        if (tr->used) {
            blep_begin(&tr->blep, frames);
            for (int v = 0; v < TRACK_MAX_VOICES; v++)
                osc_draw(&tr->voices[v].osc, &tr->blep);
            blep_read(&tr->blep, tmono, frames);
            track_sing(tr, frames);
            chain_process(&tr->chain, tmono, out, frames);
            /* Once it has closed it is put away CLEAN. A song's track
             * keeps running between notes and its filters settle; this
             * one stops being rendered, and what its DC block and its
             * filter still held would come out under the next key. */
            if (tr->chain.stage == ENV_IDLE && tr->chain.env == 0) {
                for (int v = 0; v < TRACK_MAX_VOICES; v++) {
                    voice_stop(tr, &tr->voices[v]);
                    vox_unbind(&tr->voices[v]);
                }
                blep_reset(&tr->blep);
                tr->chain.dc_x1 = tr->chain.dc_y1 = tr->chain.ic1 = tr->chain.ic2 = 0;
                tr->used = false;
            }
        }
    }
    /* The returns run on after the last note: a tail is not a track. */
    prof_mark();
    if (fed || reverb_ringing()) {
        reverb_set(song->reverb_size, song->reverb_damp);
        reverb_process(fed ? sendbus : 0, out, frames);
    }
    prof(PROF_REV);
    if (fed_echo || echo_ringing()) {
        echo_set(song->delay_time, song->delay_fb, tick_samples());
        echo_process(fed_echo ? echobus : 0, out, frames);
    }
    prof(PROF_ECHO);
}

void player_render_raw(int32_t *out, int frames)
{
    handle_cmds();
    for (int i = 0; i < frames * 2; i++)
        out[i] = 0;
    while (frames > 0) {
        int n;
        if (to_next_tick == 0) {
            prof_mark();
            player_tick();
            prof(PROF_TICK);
            to_next_tick = tick_samples();
            reap_due = true;
        }
        n = frames < to_next_tick ? frames : to_next_tick;
        if (n > SYNTH_MAX_FRAMES)
            n = SYNTH_MAX_FRAMES;
        render_tracks(out, n);
        reap_due = false;
        out += n * 2;
        frames -= n;
        to_next_tick -= n;
    }
}

void player_render(int16_t *out, int frames)
{
    uint32_t t0 = player_clock ? player_clock() : 0, all = 0;
    for (int k = 0; k < PROF_N; k++)
        prof_now[k] = 0;
    while (frames > 0) {
        int n = frames < SYNTH_MAX_FRAMES ? frames : SYNTH_MAX_FRAMES;
        player_render_raw(mix, n);
        for (int i = 0; i < n * 2; i++) {
            int32_t s = mul_q(mix[i], master, 10);
            out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        out += n * 2;
        frames -= n;
    }
    if (player_clock && (all = player_clock() - t0) > prof_worst[PROF_N]) {
        for (int k = 0; k < PROF_N; k++)
            prof_worst[k] = prof_now[k];
        prof_worst[PROF_N] = all;
    }
}

/* ============================================================== api === */
void player_init(Song *s)
{
    song = s;
    fx_init();
    for (int t = 0; t <= AUDITION; t++) {
        Track *tr = &tracks[t];
        chain_init(&tr->chain);
        blep_reset(&tr->blep);
        for (int v = 0; v < TRACK_MAX_VOICES; v++) {
            osc_init(&tr->voices[v].osc);
            tr->voices[v].active = tr->voices[v].sounding = false;
            tr->voices[v].wave = 0;
            tr->voices[v].vox = -1;
            tr->voices[v].detune = 0;
        }
        tr->used = false;
        tr->silent = false;
    }
    for (int k = 0; k < VOX_VOICES; k++) {
        vox_init(&vox_pool[k]);
        vox_owner[k] = -1;
    }
    tracks[AUDITION].fx.syl[0] = 'A';
    tracks[AUDITION].fx.syl[1] = '\0';
    bpm = s->bpm;
    playing = false;
    to_next_tick = 0;
    cmd_r = cmd_w;
    ui_playing = 0;
    ui_row = ui_entry = 0;
    mute_mask = solo_mask = 0;
    loop_word = 0;
}

void player_play_song(int entry, bool once) { cmd_push(CMD_PLAY_SONG, (uint8_t)entry, once); }
void player_play_entry(int entry) { cmd_push(CMD_PLAY_ENTRY, (uint8_t)entry, 0); }
void player_stop(void) { cmd_push(CMD_STOP, 0, 0); }
void player_set_loop(int from, int len)
{
    loop_word = from < 0 || len <= 0 ? 0 : ((uint32_t)from & 0xFFFFu) | ((uint32_t)len & 0xFFFFu) << 16;
}
void player_note_on(int instr, uint8_t note) { cmd_push(CMD_NOTE_ON, (uint8_t)instr, note); }
void player_note_off(void) { cmd_push(CMD_NOTE_OFF, 0, 0); }
void player_toggle_mute(int track)
{
    if (track >= 0 && track < SONG_MAX_TRACKS)
        mute_mask ^= (uint8_t)(1u << track);
}
void player_set_master(int pot)
{
    master = pot < 0 ? 0 : pot >= 1023 ? 1024 : pot;
}
bool player_playing(void) { return ui_playing != 0; }
uint16_t player_row(void) { return ui_row; }
uint16_t player_entry(void) { return ui_entry; }
bool player_muted(int track) { return (mute_mask >> track) & 1u; }
void player_toggle_solo(int track)
{
    if (track >= 0 && track < SONG_MAX_TRACKS)
        solo_mask ^= (uint8_t)(1u << track);
}
bool player_soloed(int track) { return (solo_mask >> track) & 1u; }
