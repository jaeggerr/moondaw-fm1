/* SPDX-License-Identifier: GPL-3.0-only */
/* The song, as MOONDAW has it: an arrangement of phrases, each naming
 * one block per track; blocks numbered per track; a step of up to four
 * notes, one instrument and three fx. Same model, same file (FORMAT.md
 * of the moondaw repository), sized for 512 KB of RAM.
 *
 * What the FM-1 changes, all of it here:
 *
 *   tracks        8, not 16
 *   steps         a shared pool instead of a fixed grid (below)
 *   engines       PULSE, NOISE, WAVE and VOX sound; SMPL and PLUCK keep
 *                 their name in the file and are silent
 *   syllables     one table for the whole song instead of one per block
 *                 (below)
 *   not carried   samples and zones, the reference record,
 *                 drive / amp / tone, preview tunes
 *
 * Freestanding: no libc, no allocation, no float. */
#ifndef SONG_H
#define SONG_H

#include <stdbool.h>
#include <stdint.h>

#define SONG_MAX_TRACKS    8
#define SONG_DEF_TRACKS    8
#define STEP_MAX_NOTES     4
#define STEP_MAX_FX        3
#define PATTERN_MAX_STEPS  128
#define PATTERN_DEF_STEPS  16
#define TRACK_MAX_PATTERNS 16
#define SONG_MAX_INSTR     64
#define SONG_ARRANGE_LEN   128
#define SONG_MAX_SECTIONS  SONG_ARRANGE_LEN
#define INSTR_MAX_LAYERS   4
#define INSTR_NAME_LEN     8  /* 7 + NUL */
#define SONG_NAME_LEN      12 /* phrase and section names */
#define ENTRY_CYCLE_MAX    1024

/* The step pool. moondaw gives every block its 128 steps whether it
 * uses them or not -- 16 x 16 x 128 of them, 557 KB. Here a block owns
 * CHUNKS of 16 steps, taken from one pool when a step is first written
 * and never before, so an empty block costs nothing and the bars of
 * silence inside a long one cost nothing either.
 *
 * 375 chunks = 6000 steps = 66 KB. The largest song of the repository
 * on 10/10/2026 (tremolo-chip) needs 212 of them. */
#define CHUNK_STEPS     16
#define POOL_CHUNKS     375
#define PATTERN_CHUNKS  (PATTERN_MAX_STEPS / CHUNK_STEPS)
#define CHUNK_NONE      0xFFFF

/* The syllables VOX sings. moondaw gives every block a pool of 128
 * entries of 6 bytes, a third of its grid (186 KB). Here one table
 * serves the whole song and a syllable is stored once however often it
 * is sung: an FX_SYL step's value is its index. The four songs that
 * sing use 10 to 30 distinct syllables; 255 entries are 1.5 KB.
 * Entry 255 is never handed out, so a value read off a step always
 * names a real entry or an empty one. */
#define SONG_SYL_LEN 6 /* 5 phoneme characters + NUL, the slot's width */
#define SONG_SYL_MAX 255

#define VOX_TRACT_MIN 50
#define VOX_TRACT_MAX 150
enum { VOX_ACCENT_FR = 0, VOX_ACCENT_EN };

#define SWING_MIN 50
#define SWING_MAX 75
#define BPM_MIN   20
#define BPM_MAX   400
#define METER_MAX 16
#define TICKS_MAX 24

#define NOTE_NONE    0xFF
#define NOTE_OFF     0xFE
#define NOTE_MIN     12
#define NOTE_MAX     127
#define INSTR_NONE   0xFF
#define PATTERN_NONE 0xFF /* track muted in this phrase */
#define PATTERN_HOLD 0xFE /* nothing played, nothing released */
#define SECTION_NONE 0xFF
#define VOL_FULL     127

/* The order is the file's and moondaw's: never reorder, add at the end. */
enum {
    FX_NONE = 0,
    FX_DLY, FX_CUT, FX_GAT, FX_RET, FX_STR, FX_VOL, FX_VEL, FX_PAN, FX_REV,
    FX_ECH, FX_DUT, FX_BPM,
    FX_TSP, FX_SLI, FX_VIB, FX_SWP, FX_SCP, FX_ARP, FX_TRM, FX_FLT, FX_RES,
    FX_WAV, FX_RMP, FX_CRS, FX_RAT,
    FX_SYL, /* value: an index into the song's syllable table */
    FX_SMP, /* never stored here: there is no sample table */
    FX_COUNT
};

enum {
    ENGINE_PULSE = 0, ENGINE_NOISE, ENGINE_WAVE,
    ENGINE_SAMPLE, ENGINE_VOX, ENGINE_PLUCK, /* SMPL and PLUCK: named, silent */
    ENGINE_COUNT
};

enum { ENTRY_PHRASE = 0, ENTRY_REF };
enum { REF_PHRASE = 0, REF_SECTION };

#define FILTER_OPEN     255
#define FILTER_ENV_NONE 64
#define GAIN_MIN_DB     (-64)
#define GAIN_MAX_DB     24
#define VIB_PHASE_FREE  (-1)

typedef struct {
    uint8_t cmd, val;
} StepFx;

typedef struct {
    uint8_t notes[STEP_MAX_NOTES];
    uint8_t instrument;
    StepFx fx[STEP_MAX_FX];
} Step; /* 11 bytes, as moondaw */

typedef struct {
    uint16_t length;                /* 1..PATTERN_MAX_STEPS */
    uint16_t chunk[PATTERN_CHUNKS]; /* CHUNK_NONE = nothing written there */
} Pattern;

typedef struct {
    uint8_t engine, timbre;
    int8_t transpose, detune, gain;
} Layer;

typedef struct {
    uint8_t engine, timbre;
    uint8_t attack, decay, sustain, release;
    uint8_t cutoff, reso, filt_env, filt_vel;
    int8_t gain, level; /* dB, before and after the instrument's stages */
    int16_t vib_phase;  /* 0..255, or VIB_PHASE_FREE */
    uint8_t tract;      /* VOX: tract size in percent, 0 = 100 = as tuned */
    uint8_t accent;     /* VOX: VOX_ACCENT_* */
    uint8_t nlayers;
    Layer layers[INSTR_MAX_LAYERS];
    char name[INSTR_NAME_LEN];
} InstrumentDef;

typedef struct {
    uint8_t pattern[SONG_MAX_TRACKS];
    uint8_t level[SONG_MAX_TRACKS];
    uint16_t steps; /* 0 = auto: one full cycle */
    char name[SONG_NAME_LEN + 1];
    uint8_t kind, ref_kind, ref_target;
    uint8_t section;
} SongEntry;

typedef struct {
    uint8_t volume; /* 0..127 */
    int8_t pan;     /* -64..63 */
    uint8_t send, echo;
} TrackInit;

typedef struct {
    uint16_t bpm;
    uint8_t ticks_per_step, steps_per_beat, beats_per_bar;
    uint8_t num_tracks, arrange_len, section_count;
    uint8_t reverb_size, reverb_damp, delay_time, delay_fb, swing;
    TrackInit track_init[SONG_MAX_TRACKS];
    InstrumentDef instruments[SONG_MAX_INSTR];
    SongEntry arrangement[SONG_ARRANGE_LEN];
    char sections[SONG_MAX_SECTIONS][SONG_NAME_LEN + 1];
    Pattern patterns[SONG_MAX_TRACKS][TRACK_MAX_PATTERNS];
    Step pool[POOL_CHUNKS][CHUNK_STEPS];
    uint8_t pool_used[(POOL_CHUNKS + 7) / 8];
    char syl[SONG_SYL_MAX][SONG_SYL_LEN]; /* "" = free */
} Song;

void song_init(Song *s);
void song_copy(Song *dst, const Song *src);

/* Reading never allocates: a step nobody wrote reads as the empty one. */
const Step *song_step(const Song *s, int track, int pat, int row);
/* Writing takes a chunk from the pool the first time. NULL when the
 * pool is empty or the address is not one -- the caller has to say so,
 * a note silently not written is the worst thing this machine can do. */
Step *song_step_w(Song *s, int track, int pat, int row);
void song_pattern_clear(Song *s, int track, int pat);
bool song_pattern_blank(const Song *s, int track, int pat);
bool step_is_empty(const Step *st);
/* Hands back the chunks that hold nothing any more. */
void song_pool_compact(Song *s);
int song_pool_free(const Song *s); /* chunks left */

/* 128 KB that two things take turns in: the delay's two lines while a
 * song plays, and the working copy a file is loaded through. They never
 * overlap because a load happens with the player silent and is followed
 * by player_init, which clears the delay. Without this the working copy
 * alone would be a second Song, 77 KB that exist to be used for a
 * hundred milliseconds. */
#define SCRATCH_BYTES 131072
void *song_scratch(void);

void song_sections_normalize(Song *s);

/* The syllable table. Interning finds the text first, so a lyric full
 * of repeats costs one entry per distinct syllable; when the table is
 * full, entries no step names any more are taken back (an erased
 * syllable is reclaimed lazily, as moondaw does per block). Returns the
 * index, or -1 when the text is empty or the table holds 255 syllables
 * that are all in use. Text past 5 characters is cut, as moondaw cuts. */
int song_syl_intern(Song *s, const char *text);
/* "" for an index that names nothing. */
const char *song_syl(const Song *s, uint8_t idx);

/* How long things last, in steps. A phrase's cycle is the LCM of the
 * blocks it names; a reference lasts what it plays. As moondaw's. */
uint16_t song_entry_cycle(const Song *s, const SongEntry *e);
uint16_t song_entry_steps(const Song *s, int pos);
uint16_t song_play_steps(const Song *s, int pos, int play);
/* The phrase row `pos` plays as its nth, -1 past the last: a phrase is
 * one row long, a section reference as many as its target holds. */
int song_entry_resolve(const Song *s, int pos, int nth);
bool song_section_bounds(const Song *s, int sec, int *first, int *count);
uint8_t fx_clamp_val(uint8_t cmd, uint8_t ticks_per_step, int v);

const char *fx_name(uint8_t cmd);       /* 3 letters, "---" for none */
const char *fx_desc(uint8_t cmd);       /* what it does, 18 characters at most: the only manual */
int fx_from_name(const char *s);        /* FX_NONE if unknown */
int fx_ramp_slot(uint8_t cmd);          /* -1 when the command jumps */
bool fx_conflicts(const Step *st, int slot, uint8_t cmd);
const char *engine_name(uint8_t engine);
int engine_from_name(const char *s);    /* -1 if unknown */
uint8_t engine_timbre_max(uint8_t engine);
bool engine_sounds(uint8_t engine);     /* the three chip engines; VOX sings by its own path */
char *note_name(uint8_t note, char buf[4]);
int note_from_name(const char *t);      /* NOTE_OFF, a note, or -1 */

#endif
