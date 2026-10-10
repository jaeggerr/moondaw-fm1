/* SPDX-License-Identifier: GPL-3.0-only */
#include "edit.h"

#include "app.h"

static Step *step_w(Song *s, int track, int pat, int row)
{
    Step *st = song_step_w(s, track, pat, row);
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
    Step *st;

    if (v < NOTE_MIN && d > 0)
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
