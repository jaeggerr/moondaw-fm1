/* SPDX-License-Identifier: GPL-3.0-only */
#include "phrase.h"

#include "../core/player.h"
#include "app.h"
#include "edit.h"
#include "gfx.h"
#include "grid.h"
#include "loop.h"
#include "ui.h"
#include "undo.h"

/* PHRASE, as validated on the panel on 28/10 (NOTES.md): SONG's grid
 * turned to steps. It was called SECTION until 31/10, when SECTION went
 * to the group of phrases the file has always called `sec` -- the screen
 * wrote SEC 07 for a phrase while SONG wrote REF SEC 02 for a group, and
 * the help said "the section" for both, eight lines apart. The word the
 * file, SONG, CHOOSE PHRASE and the help already gave this row is
 * phrase, so the screen takes it. One row a step, its number at
 * the left, white on the beat; one column a track, the note it plays on
 * that step (-- when none), five tracks a screen.
 *
 *   SELECT, KNOB 1  the step   KNOB 2  the track     the keys  write notes, OCT-/OCT+ their octave
 *   EDIT held + OP6  an end of the LOOP at the step (again: the other end, then none)
 *   SEL, EDIT  the block of that track, to edit   HOME  back to SONG   GLO  the PROJECT
 *   FX      the MIXER    ALGO  UNDO left, REDO right     SAVE  PLAY  as printed */
enum { CELL_W = 37, CELLS = 5 };

#define song (&app_song)

static int entry, row, col, top, left, steps;
static int kbd_oct;    /* octaves the keyboard is moved by */
static int chord_next; /* the note slot the next held key fills, 0 = none held */
static const char *said;
static uint32_t prev_btns, prev_keys;
static UiShift shift; /* EDIT held + MONO: the track under the cursor alone, or muted */
static bool playing;
static int play_row = -1;
static int still; /* frames the hand has not touched anything: past GRID_FOLLOW_IDLE the view follows PLAY */

/* ------------------------------------------------------------- data --- */
static int blk(int t) { return song->arrangement[entry].pattern[t]; }
static bool has(int t) { return blk(t) < TRACK_MAX_PATTERNS; }
/* A block shorter than the phrase repeats: the grid shows what plays,
 * and writing on a row writes the step of the block that plays there. */
static int prow(int t, int r) { return r % song->patterns[t][blk(t)].length; }
static const Step *cell(int t, int r) { return song_step(song, t, blk(t), prow(t, r)); }

static void measure(void)
{
    steps = song_entry_steps(song, entry);
    if (steps < 1)
        steps = 1;
    if (row >= steps)
        row = steps - 1;
    if (col >= song->num_tracks)
        col = song->num_tracks - 1;
}

/* ---------------------------------------------------------- drawing --- */
/* The beat: its first step's number is white. The meter's steps a beat
 * (PROJECT's STEP), not 4 by hand. */
static int beat(void) { return song->steps_per_beat ? song->steps_per_beat : 4; }

/* The first note; else OFF; else the step's first effect, in amber: something happens here. */
static void draw_cell(int v, int t, int r, bool band)
{
    int x = GRID_CELL_X + (t - left) * CELL_W;
    bool on = band && t == col;
    const Step *st;
    char n[4];

    if (!has(t)) {
        grid_cell(v, x, CELL_W, "--", band, on, C_DIM);
        return;
    }
    st = cell(t, r);
    for (int k = 0; k < STEP_MAX_NOTES; k++)
        if (st->notes[k] <= NOTE_MAX) {
            grid_cell(v, x, CELL_W, note_name(st->notes[k], n), band, on,
                      !grid_heard(t) ? C_DIM : r == play_row ? C_TITLE : C_TEXT); /* grey: not heard */
            return;
        }
    if (st->notes[0] == NOTE_OFF) {
        grid_cell(v, x, CELL_W, "OFF", band, on, C_DIM);
        return;
    }
    for (int k = 0; k < STEP_MAX_FX; k++)
        if (st->fx[k].cmd != FX_NONE) {
            grid_cell(v, x, CELL_W, fx_name(st->fx[k].cmd), band, on, C_FX);
            return;
        }
    grid_cell(v, x, CELL_W, "--", band, on, C_DIM);
}

static void draw(void)
{
    char s[4];

    grid_clear();
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else if (loop_state() == LOOP_SET) { /* the loop, in place of where one is: LOOP 02-05 */
        char lo[3], hi[3], range[6];
        grid_hex2(lo, loop_from()), grid_hex2(hi, loop_to());
        range[0] = lo[0], range[1] = lo[1], range[2] = '-', range[3] = hi[0], range[4] = hi[1], range[5] = 0;
        grid_corner("LOOP", C_TITLE, range, C_TITLE);
    } else if (loop_state() == LOOP_ONE_END)
        grid_corner("LOOP", C_TITLE, grid_hex2(s, loop_from()), C_TEXT);
    else
        grid_corner("PHR", C_DIM, grid_hex2(s, entry), C_TITLE);
    for (int c = 0; c < CELLS && left + c < song->num_tracks; c++) /* green alone, red muted */
        grid_track_head(GRID_CELL_X + c * CELL_W, CELL_W, song, left + c, left + c == col);
    for (int v = 0; v < GRID_ROWS && top + v < steps; v++) {
        int r = top + v;
        bool band = r == row;
        if (band)
            grid_band(v, PLAT_W - 4 - (steps > GRID_ROWS ? 6 : 0));
        if (r == play_row)
            grid_playing(v, band);
        if (loop_has(r)) /* the loop: a cyan rule at the left edge, as PATTERN's */
            gfx_fill(0, grid_y(v) + (r == loop_from() ? 3 : 0), 3,
                     GRID_ROW - (r == loop_from() ? 3 : 0) - (r == loop_to() ? 3 : 0), C_TITLE, 255);
        grid_num(v, r, band, false,
                 r == play_row || (loop_has(r) && loop_state() == LOOP_SET) ? C_TITLE : r % beat() ? C_DIM : C_TEXT);
        for (int t = left; t < left + CELLS && t < song->num_tracks; t++)
            draw_cell(v, t, r, band);
    }
    grid_frame(top, steps);
}

/* -------------------------------------------------------------- api --- */
int phrase_entry(void) { return entry; }
int phrase_track(void) { return col; }
int phrase_row(void) { return has(col) ? prow(col, row) : 0; }

void phrase_resume(void)
{
    measure(); /* PATTERN may have changed a length, or the block */
    said = 0;
    chord_next = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

void phrase_open(int e, int track)
{
    entry = e < 0 ? 0 : e >= song->arrange_len ? song->arrange_len - 1 : e;
    col = track < 0 ? 0 : track;
    row = top = left = 0;
    phrase_resume();
}

int phrase_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
    bool edit_tap, started = false; /* started: this frame set the playhead going */
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    prev_keys = in->keys;
    playing = player_playing();
    player_audition_at(col, 0, 0); /* heard in the mix of the track under the cursor (2/11) */
    if (btns || keys || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1])
        said = 0;
    /* EDIT tapped opens the block (at the release); held, the keys are its
     * commands and write nothing -- MONO, the track under the cursor (ui.h) */
    edit_tap = ui_shift(&shift, in, btns, keys, false, col, &said);
    /* EDIT held + OP6: an end of the LOOP at the cursor's step; the second
     * press the other end, a third forgets it (loop.h) */
    if (shift.held && (keys >> UI_KEY_LOOP & 1u))
        loop_press(entry, row);
    if (shift.held || shift.mono)
        keys = 0;

    row += in->enc[PE_SELECT] + in->enc[PE_K1]; /* KNOB 1 too: one hand (29/10) */
    col += in->enc[PE_K2];
    measure();
    col = col < 0 ? 0 : col;
    row = row < 0 ? 0 : row;
    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;

    /* The keys write notes, and what was written is heard: the first one
     * is the step's first note, the ones pressed while it is held the
     * rest of the chord. */
    if (!in->keys) {
        if (chord_next)
            player_note_off();
        chord_next = 0;
    }
    for (int k = 0; has(col) && k < PLAT_KEYS; k++) {
        int n = edit_key_note(song, edit_instr_at(song, col, blk(col), prow(col, row)), kbd_oct, k);
        if (!((keys >> k) & 1u) || n < NOTE_MIN || n > NOTE_MAX || chord_next >= STEP_MAX_NOTES)
            continue;
        if (!edit_write_note(song, col, blk(col), prow(col, row), chord_next, (uint8_t)n))
            said = "POOL FULL";
        player_note_on(edit_instr_at(song, col, blk(col), prow(col, row)), (uint8_t)n);
        chord_next++;
    }
    /* A MIDI keyboard plays the track's instrument and writes nothing (ui.h) */
    if (has(col))
        ui_midi_play(in, edit_instr_at(song, col, blk(col), prow(col, row)));

    if (undo_turn(in->enc[PE_ALGO], &said)) {
        if (entry >= song->arrange_len)
            entry = song->arrange_len - 1; /* it may be older than this screen */
        measure();
    }
    if (PRESSED(PB_SAVE))
        said = ui_save_word(app_save());
    if (PRESSED(PB_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_entry(entry), started = true;
    }

    measure();
    grid_follow(&col, &left, song->num_tracks, CELLS);
    play_row = playing && player_entry() == entry ? player_row() % steps : -1;
    /* The press that STARTS the transport is not the hand busying itself:
     * the view goes with the playhead from that frame on. Counted as a
     * touch, it held the view on the cursor for GRID_FOLLOW_IDLE frames --
     * 1.5 s, fourteen steps at 138 bpm -- and the follow then jumped a page
     * at a time from wherever the playhead had got to, so a whole page
     * between the two was never shown (2/11). */
    if (started)
        still = GRID_FOLLOW_IDLE;
    else
        still = btns || keys || in->keys || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2]
                    ? 0
                    : still < GRID_FOLLOW_IDLE ? still + 1 : still;
    if (play_row >= 0 && still >= GRID_FOLLOW_IDLE) /* autoscroll: the view goes with what plays */
        grid_follow_play(&top, play_row, steps, GRID_ROWS);
    else
        grid_follow(&row, &top, steps, GRID_ROWS);
    ui_led_state(PB_PLAY, playing);
    /* SEL opens the track's block and EDIT does the same: neither is lit (ui.h) */
    ui_led_guide(1u << PB_FX | 1u << PB_GLO | 1u << PB_OCTDN | 1u << PB_OCTUP);
    ui_led_mono(col, shift.held ? 1u << UI_KEY_MONO : 0u); /* MONO: lit alone, half muted */
    gfx_frame(draw);

    if (PRESSED(PB_HOME)) {
        player_note_off();
        song_pool_compact(song);
        return PHRASE_BACK;
    }
    if (PRESSED(PB_GLO)) {
        player_note_off();
        return PHRASE_PROJECT;
    }
    if ((PRESSED(PB_SEL) || edit_tap) && has(col)) { /* EDIT tapped: what is under the cursor */
        player_note_off();
        return PHRASE_PATTERN;
    }
    if (PRESSED(PB_FX)) {
        player_note_off();
        return PHRASE_MIXER;
    }
    return PHRASE_STAY;
#undef PRESSED
}
