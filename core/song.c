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
    /* A neutral sustained pulse everywhere. moondaw puts ten presets in
     * the first slots; they are not carried yet. */
    for (int i = 0; i < SONG_MAX_INSTR; i++) {
        InstrumentDef *in = &s->instruments[i];
        in->timbre = 2;
        in->sustain = 255;
        in->release = 10;
        in->cutoff = FILTER_OPEN;
        in->filt_env = FILTER_ENV_NONE;
        in->filt_vel = FILTER_ENV_NONE;
    }
    for (int i = 0; i < SONG_ARRANGE_LEN; i++) {
        SongEntry *e = &s->arrangement[i];
        for (int t = 0; t < SONG_MAX_TRACKS; t++) {
            e->pattern[t] = PATTERN_NONE;
            e->level[t] = VOL_FULL;
        }
        e->section = SECTION_NONE;
    }
    s->arrange_len = 1;
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
    case FX_TSP: case FX_SLI: case FX_SWP:
        hi = 127;
        break;
    case FX_DUT:
        hi = 3;
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
    "FLT", "RES", "WAV", "RMP", "CRS", "RAT", "SYL", "SMP",
};

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
    "PULSE", "NOISE", "WAVE", "SMPL", "VOX", "PLUCK",
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
    static const uint8_t max[ENGINE_COUNT] = { 3, 1, 4, 0, 4, 2 };
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
