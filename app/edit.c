/* SPDX-License-Identifier: GPL-3.0-only */
#include "edit.h"

#include "../core/drum.h"
#include "app.h"
#include "undo.h"

static Step *step_w(Song *s, int track, int pat, int row)
{
    Step *st;
    undo_step(track, pat, row);
    st = song_step_w(s, track, pat, row);
    if (st)
        app_mark_dirty();
    return st;
}

/* The note an empty slot starts from: its neighbour on the left, else
 * the first note of the nearest step above, else middle C. */
static uint8_t default_note(const Song *s, int track, int pat, int row, int k)
{
    const Step *st = song_step(s, track, pat, row);
    if (k > 0 && st->notes[k - 1] <= NOTE_MAX)
        return st->notes[k - 1];
    for (int r = row - 1; r >= 0; r--)
        if (song_step(s, track, pat, r)->notes[0] <= NOTE_MAX)
            return song_step(s, track, pat, r)->notes[0];
    return 60;
}

bool edit_write_note(Song *s, int track, int pat, int row, int k, uint8_t note)
{
    Step *st = step_w(s, track, pat, row);
    if (!st)
        return false;
    if (st->notes[0] == NOTE_OFF)
        st->notes[0] = NOTE_NONE; /* a cut and a note cannot share a step */
    st->notes[k] = note;
    return true;
}

bool edit_turn_note(Song *s, int track, int pat, int row, int k, int d)
{
    uint8_t n = song_step(s, track, pat, row)->notes[k];
    int v = n <= NOTE_MAX ? n : n == NOTE_OFF ? NOTE_MIN - 1 : NOTE_MIN - 2;
    const InstrumentDef *in = &s->instruments[edit_instr_at(s, track, pat, row) % SONG_MAX_INSTR];
    Step *st;

    if (in->engine == ENGINE_DRUM) {
        /* On a kit a semitone means nothing: 128 notes hold 8 or 11 pieces
         * and the rest are those retuned or folded, so two notches out of
         * three used to change nothing one could hear. The knob steps from
         * one piece to the next instead, with OFF and --- under the first. */
        uint8_t notes[DRUM_MAX_DRUMS];
        int cnt = drum_drums(in->timbre, notes);
        int off = k == 0 ? 1 : 0; /* OFF is the first slot's alone, so only it has that rung:
                                   * elsewhere the knob would write OFF, the step would refuse
                                   * it and put --- back, and --- is where it would stay */
        int at = n <= NOTE_MAX ? drum_drum_of(in->timbre, n) + 1 + off : n == NOTE_OFF ? 1 : 0;
        at += d;
        at = at < 0 ? 0 : at > cnt + off ? cnt + off : at;
        v = at > off ? notes[at - 1 - off] : at ? NOTE_MIN - 1 : NOTE_MIN - 2;
    } else if (v < NOTE_MIN && d > 0)
        v = default_note(s, track, pat, row, k) + d - 1;
    else
        v += d;
    if (v > NOTE_MAX)
        v = NOTE_MAX;
    if (v < NOTE_MIN - 2)
        v = NOTE_MIN - 2;
    if (v == NOTE_MIN - 1 && k != 0)
        v = NOTE_MIN - 2;
    if (v >= NOTE_MIN)
        return edit_write_note(s, track, pat, row, k, (uint8_t)v);
    if (!(st = step_w(s, track, pat, row)))
        return false;
    if (v == NOTE_MIN - 1)
        for (int i = 0; i < STEP_MAX_NOTES; i++)
            st->notes[i] = i == 0 ? NOTE_OFF : NOTE_NONE;
    else
        st->notes[k] = NOTE_NONE;
    return true;
}

bool edit_turn_octave(Song *s, int track, int pat, int row, int k, int d)
{
    int n = song_step(s, track, pat, row)->notes[k];
    if (n > NOTE_MAX)
        return true;
    n += d * 12;
    while (n > NOTE_MAX)
        n -= 12;
    while (n < NOTE_MIN)
        n += 12;
    return edit_write_note(s, track, pat, row, k, (uint8_t)n);
}

bool edit_clear_step(Song *s, int track, int pat, int row)
{
    Step *st;
    if (step_is_empty(song_step(s, track, pat, row)))
        return true;
    if (!(st = step_w(s, track, pat, row)))
        return false;
    for (int i = 0; i < STEP_MAX_NOTES; i++)
        st->notes[i] = NOTE_NONE;
    st->instrument = INSTR_NONE;
    for (int i = 0; i < STEP_MAX_FX; i++)
        st->fx[i].cmd = st->fx[i].val = 0;
    return true;
}

int edit_instr_at(const Song *s, int track, int pat, int row)
{
    for (int r = row; r >= 0; r--)
        if (song_step(s, track, pat, r)->instrument != INSTR_NONE)
            return song_step(s, track, pat, r)->instrument;
    return 0;
}

/* A kit's keys are its drums, one a key, in the map's rising order: eight
 * of the twenty-seven, eleven for Felucca's own, and the rest play nothing
 * (the screens light the ones that do). The octave does not move them --
 * there is nothing above or below to go and find, which is the point. Note
 * 29 and its octaves used to be the first key, and gm_note() folded
 * anything under 35 back into 36..47: keys 0..5 played exactly what keys
 * 12..17 played, and a drum at its own pitch could sit out of reach. */
int edit_key_note(const Song *s, int instr, int oct, int k)
{
    const InstrumentDef *in = &s->instruments[instr % SONG_MAX_INSTR];
    if (in->engine == ENGINE_DRUM) {
        uint8_t notes[DRUM_MAX_DRUMS];
        int n = drum_drums(in->timbre, notes);
        return k < n ? notes[k] : -1;
    }
    return 53 + oct * 12 + k;
}

/* Which keys a kit's keyboard answers to, as a bit a key: the screens
 * light exactly these and leave the rest dark. 0 when the instrument is
 * not a kit -- there the whole keyboard plays. */
uint32_t edit_key_drums(const Song *s, int instr)
{
    uint8_t notes[DRUM_MAX_DRUMS];
    if (s->instruments[instr % SONG_MAX_INSTR].engine != ENGINE_DRUM)
        return 0;
    return (1u << drum_drums(s->instruments[instr % SONG_MAX_INSTR].timbre, notes)) - 1u;
}

/* ------------------------------------------------------ the clipboard --- */
static Step clip[PATTERN_MAX_STEPS];
static int clip_len, clip_mask;

void edit_clip_reset(void) { clip_len = 0; }
int edit_clip_len(void) { return clip_len; }

void edit_clip_copy(const Song *s, int track, int pat, int from, int count, int mask)
{
    int len = s->patterns[track][pat].length;
    if (count > len - from)
        count = len - from;
    if (count <= 0 || !(mask & EDIT_CLIP_ALL))
        return;
    for (int i = 0; i < count; i++)
        clip[i] = *song_step(s, track, pat, from + i);
    clip_len = count;
    clip_mask = mask & EDIT_CLIP_ALL;
}

/* A cut is the whole step's: a note beside it takes its place. */
static void settle_off(Step *st)
{
    if (st->notes[0] != NOTE_OFF)
        return;
    for (int k = 1; k < STEP_MAX_NOTES; k++)
        if (st->notes[k] <= NOTE_MAX)
            st->notes[0] = NOTE_NONE;
}

int edit_clip_paste(Song *s, int track, int pat, int row, int fx_slot)
{
    int len = s->patterns[track][pat].length, shift = 0, n = 0;

    if (!(clip_mask & (EDIT_CLIP_FX1 - 1)) && fx_slot >= 0) {
        int lo = 0, hi = STEP_MAX_FX - 1;
        while (!(clip_mask & (EDIT_CLIP_FX1 << lo)))
            lo++;
        while (!(clip_mask & (EDIT_CLIP_FX1 << hi)))
            hi--;
        if (fx_slot + (hi - lo) >= STEP_MAX_FX)
            fx_slot = STEP_MAX_FX - 1 - (hi - lo);
        shift = fx_slot - lo;
    }
    for (int i = 0; i < clip_len && row + i < len; i++, n++) {
        Step *st = step_w(s, track, pat, row + i);
        if (!st)
            return -1;
        for (int k = 0; k < STEP_MAX_NOTES; k++)
            if (clip_mask & (EDIT_CLIP_N1 << k))
                st->notes[k] = clip[i].notes[k];
        settle_off(st);
        if (clip_mask & EDIT_CLIP_INSTR)
            st->instrument = clip[i].instrument;
        for (int k = 0; k < STEP_MAX_FX; k++) {
            int to = k + shift;
            if (!(clip_mask & (EDIT_CLIP_FX1 << k)) || to < 0 || to >= STEP_MAX_FX)
                continue;
            if (fx_conflicts(st, to, clip[i].fx[k].cmd))
                continue; /* refused: the step already says it */
            st->fx[to] = clip[i].fx[k];
        }
    }
    return n;
}

bool edit_erase_range(Song *s, int track, int pat, int from, int count, int mask)
{
    int len = s->patterns[track][pat].length;
    for (int i = 0; i < count && from + i < len; i++) {
        Step *st;
        if (step_is_empty(song_step(s, track, pat, from + i)))
            continue;
        if (!(st = step_w(s, track, pat, from + i)))
            return false;
        for (int k = 0; k < STEP_MAX_NOTES; k++)
            if (mask & (EDIT_CLIP_N1 << k))
                st->notes[k] = NOTE_NONE;
        if (mask & EDIT_CLIP_INSTR)
            st->instrument = INSTR_NONE;
        for (int k = 0; k < STEP_MAX_FX; k++)
            if (mask & (EDIT_CLIP_FX1 << k))
                st->fx[k].cmd = st->fx[k].val = 0;
    }
    return true;
}
