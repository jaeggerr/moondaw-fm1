/* SPDX-License-Identifier: GPL-3.0-only */
#include "song.h"

static const Step EMPTY_STEP = {
    { NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE }, INSTR_NONE,
    { { FX_NONE, 0 }, { FX_NONE, 0 }, { FX_NONE, 0 } },
};

static void zero(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = 0;
}

static bool str_word(const char *s, const char *w)
{
    while (*w)
        if (*s++ != *w++)
            return false;
    return *s == '\0' || *s == ' ' || *s == '\t';
}

static union { uint32_t align; uint8_t b[SCRATCH_BYTES]; } scratch
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;
typedef char scratch_holds_a_song[sizeof(Song) <= SCRATCH_BYTES ? 1 : -1];

void *song_scratch(void) { return scratch.b; }

void song_copy(Song *dst, const Song *src)
{
    const uint8_t *a = (const uint8_t *)src;
    uint8_t *b = (uint8_t *)dst;
    for (uint32_t n = sizeof(Song); n--;)
        *b++ = *a++;
}

/* One instrument slot, neutral: the sustained pulse under everything.
 * What a slot taken fresh starts from -- and what an `inst` line starts
 * from too, so that a file says what its instrument is whatever the slot
 * held before it was read. Since song_init seeds the first slots with
 * presets, that matters: the writer leaves out a LEVEL of 0 and an empty
 * name, and without this ground those two would be inherited from the
 * preset instead of read from the file. */
void song_instr_default(InstrumentDef *in)
{
    zero(in, sizeof *in);
    in->timbre = 2;
    in->sustain = 255;
    in->release = 10;
    in->cutoff = FILTER_OPEN;
    in->filt_env = FILTER_ENV_NONE;
    in->filt_vel = FILTER_ENV_NONE;
}

void song_instr_init(Song *s, int i)
{
    if (i < 0 || i >= SONG_MAX_INSTR)
        return;
    song_instr_default(&s->instruments[i]);
}

/* --------------------------------------------------------- presets --- */
/* The ten the first slots start with, so a track taken fresh sounds like
 * an instrument instead of the neutral pulse underneath it. A row writes
 * over only the fields that make a sound what it is -- eighteen bytes,
 * not a whole InstrumentDef -- and song_instr_init lays the rest.
 *
 * The numbers were auditioned through core/player.c and measured, not
 * guessed. Times are the envelope's own curve, v*v/8 ms: 34 is 144 ms,
 * 110 is a second and a half. A cutoff of FILTER_OPEN and a filt_env of
 * FILTER_ENV_NONE mean the filter stands aside. LEVEL holds the ten
 * within a few dB of each other -- the ones meant to be held as chords
 * sit lower, VOX and DRUM come up from their quieter paths. PAD sits
 * lowest of all at -7, because its own phrase strikes a triad: three
 * notes at once are three times the energy, and at -4 it came through
 * the chooser at twice the level of everything beside it. That number
 * follows the phrase -- one note at a time, it would want -4 back. They were
 * set against the phrase CHOOSE INSTRUMENT plays on PLAY, since that is
 * where a sound is met first, and checked again on a held triad so that
 * three notes of one preset do not lean on the master limiter.
 *
 * PAD's attack was the one number a preview changed: at 800 ms it never
 * arrived inside the 250 ms note the chooser used to play for everything,
 * and the sound seemed broken exactly where it is met. Now that a preset
 * says how long its own notes are (`demos` below) and PAD's are 1100 ms,
 * the swell has room again -- 512 ms, which arrives at about half the
 * note and then holds, so it reads as a pad and not as a slow fault. */
typedef struct {
    uint8_t engine, timbre;
    uint8_t attack, decay, sustain, release;
    uint8_t cutoff, reso, filt_env;
    int8_t level;
    char name[INSTR_NAME_LEN];
} Preset;

#define OPEN FILTER_OPEN
#define NOENV FILTER_ENV_NONE
static const Preset presets[] = {
    /* engine        tim   A    D    S    R   cut  res   env  lvl  name  */
    { ENGINE_PULSE, 1,   0,  34, 170,  14,  150,  60,   84,  0, "BASS"  },
    { ENGINE_WAVE,  2,   2,  48, 200,  24, OPEN,   0, NOENV,  0, "SUB"   },
    { ENGINE_PULSE, 0,   6,  48, 190,  24,  200,  40,   72,  0, "LEAD"  },
    { ENGINE_WAVE,  1,   0,  55,  90,  28,  170,  50,   90, -4, "KEYS"  },
    { ENGINE_WAVE,  0,   0,  40,   0,  34,  140,  70,  100,  0, "HARP"  },
    { ENGINE_WAVE,  3,  64,  70, 200, 110,  120,  30,   76, -7, "PAD"   },
    { ENGINE_PULSE, 2,   0,   0, 255,   6, OPEN,   0, NOENV, -3, "ORGAN" },
    { ENGINE_WAVE,  2,   0, 110,   0,  96, OPEN,   0, NOENV, -4, "BELL"  },
    { ENGINE_VOX,   2,   4,  30, 230,  30, OPEN,   0, NOENV,  3, "VOICE" },
    { ENGINE_DRUM,  0,   0,   0, 255,  20, OPEN,   0, NOENV,  6, "KIT"   },
};
#undef OPEN
#undef NOENV

/* What PLAY plays on each of them. One note is too short to judge a
 * sound by, and the same four notes are too short to judge ten: a bass
 * wants to be heard low and a bell wants room to ring, so each preset
 * says its own register, its own rhythm and its own note length. The
 * phrase belongs to the sound, not to the screen -- INSTRUMENT and
 * CHOOSE INSTRUMENT both ask for it here.
 *
 * Lengths were read off the rendered sound: long enough that a slow
 * attack arrives, short enough that a plucked one does not sit in
 * silence waiting. A 0 is a rest, and the run comes round again. */
static const Demo demos[] = {
    /* BASS   low, and an octave off it, the way a bass line moves */
    { 170,  6, 0, { 36, 36, 48, 43, 36, 0 } },
    /* SUB    down to the bottom in three steps, slowly: weight, and
     *        nothing else, which is the whole of what a sub is */
    { 520,  4, 0, { 40, 36, 33, 0 } },
    /* LEAD   up where a lead cuts, with the octave at the end */
    { 190,  6, 0, { 72, 79, 77, 72, 84, 0 } },
    /* KEYS   a triad out and back, struck */
    { 220,  6, 0, { 60, 64, 67, 64, 60, 0 } },
    /* HARP   a run up, no rest: each note is left to decay under the next */
    { 120,  6, 0, { 60, 64, 67, 72, 76, 79 } },
    /* PAD    two triads, swelling into each other: a pad held one note at
     *        a time was never a pad, only a slow note */
    { 1600, 6, 3, { 48, 55, 60, 45, 52, 57 } },
    /* ORGAN  held, and let go cleanly, which is all an organ does */
    { 320,  4, 0, { 48, 55, 60, 55 } },
    /* BELL   the Westminster quarters, the quarter-past phrase the tower
     *        strikes on its four bells: G#4 F#4 E4 B3, and then room to
     *        ring. Not a tune chosen for being pretty -- it is the one a
     *        bell is known by. */
    { 560,  6, 0, { 68, 66, 64, 59, 0, 0 } },
    /* VOICE  three notes a voice can carry, long enough to be sung */
    { 580,  4, 0, { 57, 64, 60, 0 } },
    /* KIT    kick, closed hat, snare, open hat, clap, two toms, a rest */
    { 125, 10, 0, { 36, 42, 38, 46, 36, 39, 48, 45, 0, 0 } },
};
typedef char a_demo_for_every_preset[sizeof demos / sizeof *demos ==
                                     sizeof presets / sizeof *presets ? 1 : -1];

/* For a slot no preset speaks for: what INSTRUMENT and the chooser have
 * always played -- C E G C an octave on, a note every 250 ms, a rest --
 * and a kit's drums wherever the engine is a kit, since on a drum map it
 * is the engine and not the slot that decides what the notes mean. */
static const Demo demo_plain = { 250, 6, 0, { 60, 64, 67, 72, 0, 0 } };
static const Demo demo_kit = { 125, 10, 0, { 36, 42, 38, 46, 36, 39, 48, 45, 0, 0 } };

int song_preset_count(void) { return (int)(sizeof presets / sizeof *presets); }

int song_demo_steps(const Demo *d)
{
    int grp = d->chord > 1 ? d->chord : 1;
    int steps = d->n / grp;
    return steps > 0 ? steps : 1;
}

int song_demo_notes(const Demo *d, int at, uint8_t *out)
{
    int stride = d->chord > 1 ? d->chord : 1; /* where the next strike begins */
    int take = stride;                        /* and how much of it is played */
    int n = 0;

    if (at < 0 || at >= song_demo_steps(d))
        return 0;
    /* A chord wider than a step can hold is cut, not folded: the stride
     * has to stay what the table laid out or every strike after this one
     * would read from the middle of its neighbour. */
    if (take > STEP_MAX_NOTES)
        take = STEP_MAX_NOTES;
    for (int i = 0; i < take; i++) {
        int k = at * stride + i;
        uint8_t v = k < d->n ? d->note[k] : 0;
        if (v)                  /* a 0 inside a chord is simply one note fewer */
            out[n++] = v;
    }
    return n;
}

/* Its preset's phrase while the slot is still that preset -- same engine,
 * same name; rename it or turn its engine and you have taken it over, and
 * the phrase stops claiming to show it off. Then a kit's drums for any
 * other kit, and the plain four notes for everything else. */
const Demo *song_instr_demo(const Song *s, int i)
{
    const InstrumentDef *in;

    if (i < 0 || i >= SONG_MAX_INSTR)
        return &demo_plain;
    in = &s->instruments[i];
    if (i < song_preset_count() && in->engine == presets[i].engine) {
        int c = 0;
        while (c < INSTR_NAME_LEN && in->name[c] == presets[i].name[c])
            c++;
        /* step_ms is a divisor on the way out: a row that lost it would
         * take the screen down rather than sound wrong. */
        if (c == INSTR_NAME_LEN && demos[i].step_ms && demos[i].n)
            return &demos[i];
    }
    return in->engine == ENGINE_DRUM ? &demo_kit : &demo_plain;
}



/* Preset `i` over slot `i`'s neutral ground. */
static void song_preset_apply(Song *s, int i)
{
    const Preset *p = &presets[i];
    InstrumentDef *in = &s->instruments[i];

    in->engine = p->engine;
    in->timbre = p->timbre;
    in->attack = p->attack;
    in->decay = p->decay;
    in->sustain = p->sustain;
    in->release = p->release;
    in->cutoff = p->cutoff;
    in->reso = p->reso;
    in->filt_env = p->filt_env;
    in->level = p->level;
    for (int c = 0; c < INSTR_NAME_LEN; c++)
        in->name[c] = p->name[c];
}

void song_init(Song *s)
{
    zero(s, sizeof(*s));
    s->bpm = 120;
    s->ticks_per_step = 6;
    s->steps_per_beat = 4;
    s->beats_per_bar = 4;
    s->num_tracks = SONG_DEF_TRACKS;
    s->reverb_size = 64;
    s->reverb_damp = 64;
    s->delay_time = 18;
    s->delay_fb = 60;
    s->swing = SWING_MIN;
    for (int t = 0; t < SONG_MAX_TRACKS; t++) {
        s->track_init[t].volume = 100;
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
            s->patterns[t][p].length = PATTERN_DEF_STEPS;
            for (int c = 0; c < PATTERN_CHUNKS; c++)
                s->patterns[t][p].chunk[c] = CHUNK_NONE;
        }
    }
    /* A neutral sustained pulse everywhere, then the presets over the
     * first slots: PRESET on the INS column opens CHOOSE INSTRUMENT over
     * them, and a slot taken past them still starts clean. */
    for (int i = 0; i < SONG_MAX_INSTR; i++)
        song_instr_init(s, i);
    for (int i = 0; i < song_preset_count() && i < SONG_MAX_INSTR; i++)
        song_preset_apply(s, i);
    for (int i = 0; i < SONG_ARRANGE_LEN; i++) {
        SongEntry *e = &s->arrangement[i];
        for (int t = 0; t < SONG_MAX_TRACKS; t++) {
            e->pattern[t] = PATTERN_NONE;
            e->level[t] = VOL_FULL;
        }
        e->section = SECTION_NONE;
    }
    s->arrange_len = 1;
    for (int i = 0; i < SONG_ZONE_MAX; i++)
        s->zones[i].inst = INSTR_NONE;
}

const Zone *song_zone(const Song *s, int inst, uint8_t note)
{
    for (int i = 0; i < s->nzones && i < SONG_ZONE_MAX; i++) {
        const Zone *z = &s->zones[i];
        if (z->inst == inst && note >= z->lo && note <= z->hi)
            return z;
    }
    return 0;
}

/* ------------------------------------------------------------ pool --- */
static bool addr_ok(int track, int pat, int row)
{
    return track >= 0 && track < SONG_MAX_TRACKS && pat >= 0 &&
           pat < TRACK_MAX_PATTERNS && row >= 0 && row < PATTERN_MAX_STEPS;
}

const Step *song_step(const Song *s, int track, int pat, int row)
{
    if (!addr_ok(track, pat, row))
        return &EMPTY_STEP;
    uint16_t c = s->patterns[track][pat].chunk[row / CHUNK_STEPS];
    return c == CHUNK_NONE ? &EMPTY_STEP : &s->pool[c][row % CHUNK_STEPS];
}

Step *song_step_w(Song *s, int track, int pat, int row)
{
    if (!addr_ok(track, pat, row))
        return 0;
    uint16_t *c = &s->patterns[track][pat].chunk[row / CHUNK_STEPS];
    if (*c == CHUNK_NONE) {
        int k = 0;
        while (k < POOL_CHUNKS && (s->pool_used[k / 8] >> (k % 8)) & 1u)
            k++;
        if (k == POOL_CHUNKS)
            return 0;
        s->pool_used[k / 8] |= (uint8_t)(1u << (k % 8));
        for (int i = 0; i < CHUNK_STEPS; i++)
            s->pool[k][i] = EMPTY_STEP;
        *c = (uint16_t)k;
    }
    return &s->pool[*c][row % CHUNK_STEPS];
}

bool step_is_empty(const Step *st)
{
    for (int i = 0; i < STEP_MAX_NOTES; i++)
        if (st->notes[i] != NOTE_NONE)
            return false;
    if (st->instrument != INSTR_NONE)
        return false;
    for (int i = 0; i < STEP_MAX_FX; i++)
        if (st->fx[i].cmd != FX_NONE)
            return false;
    return true;
}

static bool chunk_empty(const Song *s, uint16_t c)
{
    for (int i = 0; i < CHUNK_STEPS; i++)
        if (!step_is_empty(&s->pool[c][i]))
            return false;
    return true;
}

static void chunk_release(Song *s, uint16_t *c)
{
    if (*c == CHUNK_NONE)
        return;
    s->pool_used[*c / 8] &= (uint8_t)~(1u << (*c % 8));
    *c = CHUNK_NONE;
}

void song_pattern_clear(Song *s, int track, int pat)
{
    if (!addr_ok(track, pat, 0))
        return;
    Pattern *p = &s->patterns[track][pat];
    for (int c = 0; c < PATTERN_CHUNKS; c++)
        chunk_release(s, &p->chunk[c]);
    p->length = PATTERN_DEF_STEPS;
}

/* Blank as moondaw means it: nothing written within the block's length.
 * What lies past the length is kept in memory and never saved. */
bool song_pattern_blank(const Song *s, int track, int pat)
{
    const Pattern *p = &s->patterns[track][pat];
    for (int r = 0; r < p->length; r++)
        if (!step_is_empty(song_step(s, track, pat, r)))
            return false;
    return true;
}

void song_pool_compact(Song *s)
{
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++)
            for (int c = 0; c < PATTERN_CHUNKS; c++) {
                uint16_t *k = &s->patterns[t][p].chunk[c];
                if (*k != CHUNK_NONE && chunk_empty(s, *k))
                    chunk_release(s, k);
            }
}

int song_pool_free(const Song *s)
{
    int n = 0;
    for (int k = 0; k < POOL_CHUNKS; k++)
        n += !((s->pool_used[k / 8] >> (k % 8)) & 1u);
    return n;
}

/* ------------------------------------------------------- syllables --- */
static bool syl_named(const Song *s, uint8_t idx)
{
    for (int k = 0; k < POOL_CHUNKS; k++) {
        if (!((s->pool_used[k / 8] >> (k % 8)) & 1u))
            continue;
        for (int i = 0; i < CHUNK_STEPS; i++)
            for (int f = 0; f < STEP_MAX_FX; f++)
                if (s->pool[k][i].fx[f].cmd == FX_SYL && s->pool[k][i].fx[f].val == idx)
                    return true;
    }
    return false;
}

int song_syl_intern(Song *s, const char *text)
{
    int free_at = -1;

    if (!text || !text[0])
        return -1;
    for (int i = 0; i < SONG_SYL_MAX; i++) {
        int k = 0;
        while (k < SONG_SYL_LEN - 1 && text[k] && s->syl[i][k] == text[k])
            k++;
        if (s->syl[i][0] && (k == SONG_SYL_LEN - 1 || (!text[k] && !s->syl[i][k])))
            return i;
        if (free_at < 0 && !s->syl[i][0])
            free_at = i;
    }
    for (int i = 0; i < SONG_SYL_MAX && free_at < 0; i++)
        if (!syl_named(s, (uint8_t)i)) {
            s->syl[i][0] = '\0';
            free_at = i;
        }
    if (free_at < 0)
        return -1;
    for (int k = 0; k < SONG_SYL_LEN; k++)
        s->syl[free_at][k] = '\0';
    for (int k = 0; k < SONG_SYL_LEN - 1 && text[k]; k++)
        s->syl[free_at][k] = text[k];
    return free_at;
}

const char *song_syl(const Song *s, uint8_t idx)
{
    return idx < SONG_SYL_MAX ? s->syl[idx] : "";
}

/* -------------------------------------------------------- sections --- */
/* Renumbers the sections in the order they appear and drops the ones no
 * phrase carries; a section broken into two runs keeps its first. As
 * moondaw's song_sections_normalize. */
void song_sections_normalize(Song *s)
{
    static uint8_t map[SONG_MAX_SECTIONS];
    static char kept[SONG_MAX_SECTIONS][SONG_NAME_LEN + 1];
    uint8_t n = 0, open = SECTION_NONE;

    for (int i = 0; i < SONG_MAX_SECTIONS; i++)
        map[i] = SECTION_NONE;
    zero(kept, sizeof(kept));
    for (int i = 0; i < s->arrange_len; i++) {
        SongEntry *e = &s->arrangement[i];
        uint8_t old = e->section;
        if (old >= s->section_count ||
            (old != open && map[old] != SECTION_NONE)) {
            e->section = SECTION_NONE;
            open = SECTION_NONE;
            continue;
        }
        if (map[old] == SECTION_NONE) {
            map[old] = n;
            for (int k = 0; k <= SONG_NAME_LEN; k++)
                kept[n][k] = s->sections[old][k];
            n++;
        }
        e->section = map[old];
        open = old;
    }
    for (int i = 0; i < SONG_MAX_SECTIONS; i++)
        for (int k = 0; k <= SONG_NAME_LEN; k++)
            s->sections[i][k] = kept[i][k];
    s->section_count = n;
    /* The references, second: a section target moves with the
     * renumbering, and a reference whose target is gone -- or is itself
     * a reference, a chain -- becomes an empty phrase rather than a
     * dangling number. Which entries WERE references is read from a
     * snapshot, since this pass turns some of them into phrases. */
    static bool was_ref[SONG_ARRANGE_LEN];
    for (int i = 0; i < s->arrange_len; i++)
        was_ref[i] = s->arrangement[i].kind == ENTRY_REF;
    for (int i = 0; i < s->arrange_len; i++) {
        SongEntry *e = &s->arrangement[i];
        bool dead;
        if (!was_ref[i])
            continue;
        if (e->ref_kind == REF_SECTION) {
            uint8_t t = e->ref_target;
            dead = t >= SONG_MAX_SECTIONS || map[t] == SECTION_NONE;
            if (!dead)
                e->ref_target = map[t];
        } else {
            dead = e->ref_target >= s->arrange_len || e->ref_target == i ||
                   was_ref[e->ref_target];
        }
        if (dead) {
            e->kind = ENTRY_PHRASE;
            e->ref_kind = REF_PHRASE;
            e->ref_target = 0;
            e->steps = 0;
            for (int t = 0; t < SONG_MAX_TRACKS; t++) {
                e->pattern[t] = PATTERN_NONE;
                e->level[t] = VOL_FULL;
            }
        }
    }
}

/* ------------------------------------------------------- durations --- */
uint16_t song_entry_cycle(const Song *s, const SongEntry *e)
{
    uint32_t cycle = 0;
    for (int t = 0; t < s->num_tracks; t++) {
        uint32_t len, a, b;
        if (e->pattern[t] >= TRACK_MAX_PATTERNS)
            continue;
        len = s->patterns[t][e->pattern[t]].length;
        if (len == 0)
            continue;
        if (cycle == 0) {
            cycle = len;
            continue;
        }
        for (a = cycle, b = len; b != 0;) {
            uint32_t r = a % b;
            a = b;
            b = r;
        }
        cycle = cycle / a * len;
        if (cycle >= ENTRY_CYCLE_MAX) {
            cycle = ENTRY_CYCLE_MAX;
            break;
        }
    }
    return (uint16_t)(cycle ? cycle : PATTERN_DEF_STEPS);
}

static uint16_t phrase_steps(const Song *s, int pos)
{
    const SongEntry *e;
    if (pos < 0 || pos >= s->arrange_len)
        return PATTERN_DEF_STEPS;
    e = &s->arrangement[pos];
    if (e->kind == ENTRY_REF)
        return 0; /* a reference lasts what it plays, never itself */
    return e->steps > 0 ? e->steps : song_entry_cycle(s, e);
}

bool song_section_bounds(const Song *s, int sec, int *first, int *count)
{
    int f = -1, n = 0;
    if (sec < 0 || sec >= s->section_count)
        return false;
    for (int i = 0; i < s->arrange_len; i++) {
        if (s->arrangement[i].section != sec)
            continue;
        if (f < 0)
            f = i;
        n++;
    }
    if (f < 0)
        return false;
    *first = f;
    *count = n;
    return true;
}

uint16_t song_entry_steps(const Song *s, int pos)
{
    const SongEntry *e;
    if (pos < 0 || pos >= s->arrange_len)
        return PATTERN_DEF_STEPS;
    e = &s->arrangement[pos];
    if (e->kind != ENTRY_REF)
        return phrase_steps(s, pos);
    if (e->ref_kind == REF_PHRASE && e->steps > 0)
        return e->steps;
    if (e->ref_kind == REF_SECTION) {
        int first, count;
        uint32_t total = 0;
        if (!song_section_bounds(s, e->ref_target, &first, &count))
            return 0; /* a spent reference plays nothing */
        for (int i = 0; i < count; i++)
            total += phrase_steps(s, first + i);
        return total > 0xFFFF ? 0xFFFF : (uint16_t)total;
    }
    if (e->ref_target >= s->arrange_len ||
        s->arrangement[e->ref_target].kind == ENTRY_REF)
        return 0;
    return phrase_steps(s, e->ref_target);
}

uint16_t song_play_steps(const Song *s, int pos, int play)
{
    if (pos >= 0 && pos < s->arrange_len) {
        const SongEntry *e = &s->arrangement[pos];
        if (e->kind == ENTRY_REF && e->ref_kind == REF_PHRASE)
            return song_entry_steps(s, pos);
    }
    return song_entry_steps(s, play);
}

int song_entry_resolve(const Song *s, int pos, int nth)
{
    const SongEntry *e;
    if (pos < 0 || pos >= s->arrange_len || nth < 0)
        return -1;
    e = &s->arrangement[pos];
    if (e->kind != ENTRY_REF)
        return nth == 0 ? pos : -1;
    if (e->ref_kind == REF_SECTION) {
        int first, count;
        if (!song_section_bounds(s, e->ref_target, &first, &count) ||
            nth >= count)
            return -1;
        return s->arrangement[first + nth].kind == ENTRY_REF ? -1 : first + nth;
    }
    if (nth != 0 || e->ref_target >= s->arrange_len)
        return -1;
    return s->arrangement[e->ref_target].kind == ENTRY_REF ? -1 : (int)e->ref_target;
}

/* The range of a command, ONE source of truth for the editor and the
 * player. Tick values name a moment inside the step; GAT and STR name a
 * duration and may say one whole step. */
uint8_t fx_clamp_val(uint8_t cmd, uint8_t ticks_per_step, int v)
{
    int last = ticks_per_step > 0 ? ticks_per_step - 1 : 0;
    int lo = 0, hi = 255;
    switch (cmd) {
    case FX_DLY: case FX_CUT:
        hi = last;
        break;
    case FX_RET:
        lo = 1;
        hi = last;
        break;
    case FX_GAT: case FX_STR:
        lo = 1;
        hi = ticks_per_step > 0 ? ticks_per_step : 1;
        break;
    case FX_VOL: case FX_VEL: case FX_PAN: case FX_REV: case FX_ECH:
    case FX_TSP: case FX_SLI: case FX_SWP: case FX_PRB:
        hi = 127;
        break;
    case FX_DUT:
        hi = 3;
        break;
    case FX_SMP:
        hi = SONG_SMP_MAX - 1;
        break;
    case FX_WAV:
        hi = 4;
        break;
    case FX_BPM:
        lo = 20;
        break;
    case FX_ARP:
        hi = 15;
        break;
    default:
        break;
    }
    if (lo > hi)
        lo = hi;
    return (uint8_t)(v < lo ? lo : v > hi ? hi : v);
}

/* ----------------------------------------------------------- names --- */
static const char *const fx_names[FX_COUNT] = {
    "---", "DLY", "CUT", "GAT", "RET", "STR", "VOL", "VEL", "PAN", "REV",
    "ECH", "DUT", "BPM", "TSP", "SLI", "VIB", "SWP", "SCP", "ARP", "TRM",
    "FLT", "RES", "WAV", "RMP", "CRS", "RAT", "SYL", "SMP", "PRB",
};

/* moondaw's own lines (src/sequencer/song.c), word for word: each was
 * weighed there against its neighbours in the list. PRB's is this
 * firmware's -- moondaw has no such effect. */
static const char *const fx_descs[FX_COUNT] = {
    "empty slot",         /* --- */
    "hold to tick N",     /* DLY */
    "note off, tick N",   /* CUT */
    "lasts N ticks",      /* GAT */
    "retrigger N ticks",  /* RET */
    "spread, N ticks",    /* STR */
    "fader share, 00-7F", /* VOL */
    "how hard, 00-7F",    /* VEL */
    "00 left, 7F right",  /* PAN */
    "reverb send 00-7F",  /* REV */
    "delay send 00-7F",   /* ECH */
    "pulse duty 0-3",     /* DUT */
    "tempo, all tracks",  /* BPM */
    "semitones, 40 = 0",  /* TSP */
    "glide, 00 = jump",   /* SLI */
    "speed x, depth y",   /* VIB */
    "sweep, 40 = none",   /* SWP */
    "speed x, semis y",   /* SCP */
    "N ticks per note",   /* ARP */
    "VIB for the level",  /* TRM */
    "cutoff, FF = open",  /* FLT */
    "resonance, 00 = no", /* RES */
    "wave shape 0-4",     /* WAV */
    "N steps, fx right",  /* RMP */
    "bit grit, 00=off",   /* CRS */
    "downsample 00=off",  /* RAT */
    "syllable VOX sings", /* SYL */
    "sample NN of table", /* SMP */
    "chance, 7F always",  /* PRB */
};

const char *fx_desc(uint8_t cmd)
{
    return fx_descs[cmd < FX_COUNT ? cmd : 0];
}

/* Whether a command HOLDS until it is written again. The truth is
 * core/player.c, the block that clears the one-shots at the top of every
 * step: what is cleared there dies with its step, the rest is a setter. */
bool fx_sticks(uint8_t cmd)
{
    switch (cmd) {
    case FX_NONE: case FX_DLY: case FX_CUT: case FX_GAT: case FX_RET:
    case FX_STR: case FX_SCP: case FX_RMP: case FX_SMP: case FX_PRB:
        return false;
    default:
        return true;
    }
}

const char *fx_name(uint8_t cmd)
{
    return fx_names[cmd < FX_COUNT ? cmd : 0];
}

int fx_from_name(const char *s)
{
    for (int c = 1; c < FX_COUNT; c++)
        if (s[0] == fx_names[c][0] && s[1] == fx_names[c][1] &&
            s[2] == fx_names[c][2])
            return c;
    return FX_NONE;
}

int fx_ramp_slot(uint8_t cmd)
{
    static const uint8_t ramped[] = { FX_VOL, FX_VEL, FX_PAN, FX_REV, FX_ECH,
                                      FX_FLT, FX_RES, FX_CRS, FX_RAT };
    for (unsigned i = 0; i < sizeof(ramped); i++)
        if (ramped[i] == cmd)
            return (int)i;
    return -1;
}

/* RMP reads the slots to its right, so two of one setter parted by an
 * RMP are both heard, and so are two RMP parted by a setter. Anything
 * else twice on a row is a cell that says something and never plays. */
bool fx_conflicts(const Step *st, int slot, uint8_t cmd)
{
    if (cmd == FX_NONE || slot < 0 || slot >= STEP_MAX_FX)
        return false;
    bool partable = cmd == FX_RMP || fx_ramp_slot(cmd) >= 0;
    for (int i = 0; i < STEP_MAX_FX; i++) {
        if (i == slot || st->fx[i].cmd != cmd)
            continue;
        bool parted = false;
        int lo = i < slot ? i : slot, hi = i < slot ? slot : i;
        for (int k = lo + 1; partable && k < hi; k++) {
            uint8_t c = st->fx[k].cmd;
            if (cmd == FX_RMP ? fx_ramp_slot(c) >= 0 : c == FX_RMP)
                parted = true;
        }
        if (!parted)
            return true;
    }
    return false;
}

static const char *const engine_names[ENGINE_COUNT] = {
    "PULSE", "NOISE", "WAVE", "SMPL", "VOX", "PLUCK", "DRUM",
};

const char *engine_name(uint8_t engine)
{
    return engine_names[engine < ENGINE_COUNT ? engine : 0];
}

int engine_from_name(const char *s)
{
    for (int e = 0; e < ENGINE_COUNT; e++)
        if (str_word(s, engine_names[e]))
            return e;
    return -1;
}

uint8_t engine_timbre_max(uint8_t engine)
{
    static const uint8_t max[ENGINE_COUNT] = { 3, 1, 4, 0, 4, 2, 9 };
    return max[engine < ENGINE_COUNT ? engine : 0];
}

bool engine_sounds(uint8_t engine)
{
    return engine <= ENGINE_WAVE;
}

char *note_name(uint8_t note, char buf[4])
{
    static const char names[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    int oct = note / 12 - 1;
    oct = oct < 0 ? 0 : oct > 9 ? 9 : oct;
    buf[0] = names[note % 12 * 2];
    buf[1] = names[note % 12 * 2 + 1];
    buf[2] = (char)('0' + oct);
    buf[3] = '\0';
    return buf;
}

int note_from_name(const char *t)
{
    static const int8_t semitone[7] = { 9, 11, 0, 2, 4, 5, 7 }; /* A..G */
    if (t[0] == 'O' && t[1] == 'F' && t[2] == 'F')
        return NOTE_OFF;
    char l = t[0] >= 'a' && t[0] <= 'g' ? (char)(t[0] - 32) : t[0];
    if (l < 'A' || l > 'G')
        return -1;
    int st = semitone[l - 'A'];
    if (t[1] == '#')
        st++;
    else if (t[1] != '-')
        return -1;
    if (t[2] < '0' || t[2] > '9')
        return -1;
    int n = (t[2] - '0' + 1) * 12 + st;
    return n < NOTE_MIN || n > NOTE_MAX ? -1 : n;
}
