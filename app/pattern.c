/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern.h"

#include "../core/drum.h"
#include "../core/player.h"
#include "app.h"
#include "edit.h"
#include "gfx.h"
#include "grid.h"
#include "loop.h"
#include "chords.h"
#include "syllable.h"
#include "ui.h"
#include "undo.h"

/* PATTERN, as validated on the panel on 28/10 (NOTES.md): one block of
 * one track, in three views, a button each (29/10), each SONG's grid once
 * more -- one row a step, its number at the left, white on the beat.
 *
 *   NOTES  the four notes of a chord          KNOB 3 a semitone, under the lowest OFF then empty
 *   FX     the three effects                  KNOB 3 the effect, KNOB 4 its value
 *   INS    the instrument the step names, or --, then the name of the one that plays:
 *          white where the step names it, grey where it keeps the one in force (29/10)
 *          KNOB 3 walks the song's instruments and the step is heard as it turns, so
 *          the ear chooses (30/10), as SONG's KNOB 3 does with blocks
 *
 *   SELECT, KNOB 1  the step, the column left alone   KNOB 2  the column, through the whole step and across the
 *                        views (30/10): the four notes, the instrument, the three effects,
 *                        round again; the step's number at the head of each group (KNOB 3
 *                        the block's length), walked into going left, stepped over going right
 *   the keys  write the note under the cursor in NOTES (a chord while one is held), OCT-/OCT+
 *             their octave; on the step's number, in FX and INS they only play
 *   SEQ   NOTES     FX  FX     ENV  INS; on INS, the INSTRUMENT that plays on the step
 *   EDIT  a shift, and only that (31/10): the black keys are commands while it
 *         is down (EDIT + MONO tapped: the track alone, or back to normal; held:
 *         muted). OP5 takes the SELECTION in and out and it stays, as a CAPS LOCK,
 *         OP5 lit while it is on; it was a tap of EDIT until 31/10, and the quarter
 *         of a second that told a tap from a hold could not be seen. The SELECTION:
 *         SELECT and KNOB 2 stretch it from where OP5 was pressed (left of the first
 *         column: whole steps); the black keys OP1 copy, OP2 paste at the cursor,
 *         OP3 cut, OP4 clear
 *   SEL   on FX: the step's SYLLABLE (app/syllable.h), in an empty slot or its own
 *   SEL   on INS: the INSTRUMENT screen of the one that plays on the step -- reaching it
 *         by pressing ENV twice read as counter-intuitive (30/10); that way still works
 *   PRESETS on INS: CHOOSE INSTRUMENT, the list of them for the step (PLAY plays the
 *         instrument's own preview phrase, one note being too short to judge a sound by;
 *         SEL takes the one the cursor rests on, + NEW makes one, HOME goes back)
 *   HOME  back where it was opened from (out of the SELECTION first)   GLO  the PROJECT
 *   REC   armed, or not: PLAY then counts SETTINGS' COUNT-IN in (a bar by default, or
 *         none) and records what the keys play
 *         on the nearest step (replacing it), an OFF where the last key is let go
 *   REC   armed, or not: PLAY then counts a bar in and records what the keys play
 *         on the nearest step (replacing it), an OFF where the last key is let go;
 *         armed and stopped, the keys only play
 *   ARP   on NOTES, an instrument and not a kit: CHORDS, those of the song's KEY (app/chords.h)
 *   ALGO  UNDO left, REDO right     SAVE  PLAY  as printed
 * FX is a view here, not the MIXER: FX shows the effects of where one is. */
enum { V_NOTES, V_FX, V_INS, VIEWS };
static const char *const view_name[VIEWS] = { "NOTES", "FX", "INS" };
/* The view the next press reaches, said at the top right so the three are
 * found at all: the notes, then the effects, then the instrument column,
 * round again. The effects came after the column until he had both under
 * the thumb (30/10): "la vue FX devrait venir avant instrument quand on
 * scroll avec KNOB 2 car on change plus souvent les FX que l'instrument".
 * The order follows how often the hand goes there, not how the step is
 * laid out, so the rare one sits at the far end. SEQ is what one presses to
 * come back to the notes, and it is printed on the panel; INS is the
 * column, as he calls it. */
static const char *const view_next[VIEWS] = { "FX ->", "INS ->", "SEQ ->" };
static const int view_cols[VIEWS] = { STEP_MAX_NOTES, STEP_MAX_FX, 1 };
static const int view_w[VIEWS] = { 38, 60, 180 };
/* and the order of the strip the cursor runs along (below), the marker's own. */
static const int strip_order[VIEWS] = { V_NOTES, V_FX, V_INS };
enum { STRIP_N = VIEWS + STEP_MAX_NOTES + 1 + STEP_MAX_FX }; /* a number heading each group, then its columns */

static Song *song;
static int entry, track, pat;
static int cur_at;                      /* where the cursor stands in the strip: the whole truth of it */
static int view, cur_row, cur_col, top; /* view and cur_col are cur_at read back; cur_col -1: the step's number */
static int kbd_oct;                     /* octaves the keyboard is moved by */
static bool trying;                     /* a key plays without writing (the cursor in no note column) */
static int chord_next;                  /* the note slot the next held key fills, 0 = none held */
static const char *said;
static uint32_t prev_btns, prev_keys;
static uint8_t fx_order[FX_COUNT]; /* alphabetical, --- first; SYL and SMP
                                    * are written elsewhere and left out */
static int fx_count;
static int play_row = -1;
static int still; /* frames the hand has not touched anything: past GRID_FOLLOW_IDLE the view follows PLAY */
/* CHOOSE INSTRUMENT: its rows are --, then the instruments, then + NEW.
 * While it listens, the one the cursor rests on SETTLE frames plays its
 * own preview phrase, round and round, as INSTRUMENT's PLAY does.
 * + NEW takes the first free slot of the song, puts it on the step and
 * leaves: a new sound should take no hunting for an empty number
 * (30/10). */
enum { CH_TOP = 24, CH_ROWS = 7, CH_NEW = SONG_MAX_INSTR + 1, CH_N = SONG_MAX_INSTR + 2, SETTLE = 12 };
static bool choosing, listening;
static bool writing_syl; /* the SYLLABLE screen, for the FX slot syl_slot */
static bool in_chords;   /* the CHORDS screen, for the step under the cursor */
/* SELECTION (29/10, OP5's since 31/10): the rectangle from the anchor to the
 * cursor, KNOB 2 left of the first column taking whole steps; the black
 * keys OP1-4 copy, paste, cut, clear, OP6 makes its steps the LOOP PLAY
 * goes round (again on the same forgets it); with no selection OP6 sets an
 * end at the cursor's step, a second the other end, a third none (as
 * PHRASE's). A copy lets the anchor go: the cursor is free again, the
 * clipboard's outline following it to show where OP2 puts it. */
/* The black keys by their groups under the hand: the clipboard in the first
 * three, clearing in the two after (OP5 free), listening in the next three. */
enum { OP_COPY = UI_OP_COPY, OP_PASTE = UI_OP_PASTE, OP_CUT = UI_OP_CUT, OP_CLEAR = UI_OP_CLEAR,
       OP_SEL = UI_OP_SEL, OP_LOOP = UI_KEY_LOOP, OP_MONO = UI_KEY_MONO };
static bool selecting, anchored;
static UiShift shift; /* EDIT held: the keys are commands (ui.h) */
/* REC (29/10): armed, PLAY counts a bar in, then what the keys play is
 * written where the playhead is -- the nearest step, replacing what was
 * there -- and an OFF where the last key is let go. */
static bool rec_armed, counting, recording, rec_live; /* rec_live: the player has started */
static int count_beat;
static uint32_t count_t0;
static int rec_row = -1; /* the step the keys held now were written on */
static int rec_slot;     /* the next slot of that step's chord */
static int an_row, an_col, clip_cols;
static int syl_slot;
static int ch_cur, ch_top, ch_orig, settle, sounding = -1;
static uint32_t pv_t0; /* the preview phrase: when it started, and the note it is on */
static int pv_at;
static int ch_free = -1; /* the slot + NEW would take, -1 when the song holds none */
/* The ear on the INS column (30/10): the hand rested AU_SETTLE frames on
 * a step whose instrument it has just turned, and the step sounds -- its
 * own notes, for AU_HOLD frames. The song playing, the music speaks and
 * nothing is struck over it. */
enum { AU_SETTLE = SETTLE, AU_HOLD = 24 };
static int au_settle, au_hold;
static bool au_sounding;

/* ---------------------------------------------------------- strip --- */
/* KNOB 2 runs along one strip, the whole step in the order the marker at
 * the top right promises, with the step's number at the head of each group,
 * where the screen itself puts it -- at the left of the grid:
 *
 *   #  N1 N2 N3 N4   #  FX1 FX2 FX3   #  INSTRUMENT
 *
 * Where the cursor stands in that strip is the only truth there is (30/10):
 * `view` and `cur_col` are read back from it, so the knob crosses a
 * frontier and the grid turns over under it on its own, instead of the
 * cursor being held inside the view it started in. Two states to keep
 * agreeing with each other were one too many.
 *
 * The two directions are not the same, and he asked for both in the same
 * breath (30/10). Rightwards the numbers are stepped over: the fourth note
 * reaches FX 1, the last effect reaches the instrument, and the instrument
 * comes round to the first note -- going right is going further into the
 * step, and the number lies at the far left of the screen, so landing on it
 * would read as a jump backwards. Leftwards they are walked into: FX 1
 * reaches the number, which is the left edge of the grid in front, and only
 * the turn after that crosses to the group before it, arriving at its last
 * column. So the step as a whole is still one detent from the first column
 * of wherever one is, as it was. The first number is the end of the strip
 * and the knob stops there. */
static int strip_first(int v) /* where a view's group begins: its number */
{
    int p = 0;
    for (int i = 0; i < VIEWS && strip_order[i] != v; i++)
        p += 1 + view_cols[strip_order[i]];
    return p;
}

static int strip_view(int p)
{
    for (int i = 0; i < VIEWS - 1; i++) {
        int n = 1 + view_cols[strip_order[i]];
        if (p < n)
            return strip_order[i];
        p -= n;
    }
    return strip_order[VIEWS - 1];
}

static int strip_col(int p) { return p - strip_first(strip_view(p)) - 1; }

/* the strip read back -- nothing else writes view or cur_col */
static void strip_read(void)
{
    view = strip_view(cur_at);
    cur_col = strip_col(cur_at);
}

static void strip_go(int p)
{
    cur_at = p;
    strip_read();
}

static void strip_turn(int d) /* a detent at a time: the two directions differ */
{
    for (; d > 0; d--) {
        cur_at = cur_at + 1 < STRIP_N ? cur_at + 1 : 1; /* past the last effect: the first note */
        if (strip_col(cur_at) < 0)                      /* a number is not turned into, going right */
            cur_at++;
    }
    for (; d < 0; d++)
        cur_at = cur_at > 0 ? cur_at - 1 : 0;
    strip_read();
}

/* The SELECTION is the one place the knob still stops at the view's edge:
 * half a block of notes and half of effects is nothing one could paste, and
 * the clipboard's fields are a view's (edit.h, 30/10). */
static void strip_turn_sel(int d)
{
    int lo = strip_first(view), hi = lo + view_cols[view], p = cur_at + d;
    strip_go(p < lo ? lo : p > hi ? hi : p);
}

/* ----------------------------------------------------------- data --- */
static int pat_len(void) { return song->patterns[track][pat].length; }
static const Step *step_at(int row) { return song_step(song, track, pat, row); }

static Step *step_w(int row)
{
    Step *st;
    undo_step(track, pat, row);
    st = song_step_w(song, track, pat, row);
    if (!st)
        said = "POOL FULL";
    else
        app_mark_dirty();
    return st;
}

/* The ear forgets what it was about to do; au_hush also stops what it
 * started, so no audition is left hanging. */
static void au_quiet(void)
{
    au_settle = au_hold = 0;
    au_sounding = false;
}

static void au_hush(void)
{
    if (au_sounding)
        player_note_off();
    au_quiet();
}

/* What the audition strikes on `row`: the step's own notes, all of them,
 * as the step would sound them. A step naming none is still heard in its
 * place, on the nearest note above or below it in the track's column --
 * above first, an empty step carrying on the note before it. An OFF is
 * no note, so the search goes past it. None anywhere: nothing sounds. */
static int au_notes(int row, uint8_t out[STEP_MAX_NOTES])
{
    int len = pat_len(), n = 0;
    for (int d = 0; d < len && n == 0; d++)
        for (int s = 0; s < 2 && n == 0; s++) {
            int r = s ? row + d : row - d;
            if (r < 0 || r >= len || (d == 0 && s))
                continue;
            for (int i = 0; i < STEP_MAX_NOTES; i++)
                if (step_at(r)->notes[i] <= NOTE_MAX)
                    out[n++] = step_at(r)->notes[i];
        }
    return n;
}

static void au_sound(void)
{
    uint8_t ns[STEP_MAX_NOTES];
    int n = au_notes(cur_row, ns);
    au_settle = 0;
    if (n == 0 || player_playing())
        return;
    player_note_chord(edit_instr_at(song, track, pat, cur_row), ns, n);
    au_sounding = true;
    au_hold = AU_HOLD;
}

static void turn_instr(int row, int d)
{
    const Step *rd = step_at(row);
    int v = rd->instrument == INSTR_NONE ? -1 : rd->instrument;
    Step *st;
    v += d;
    v = v < -1 ? -1 : v >= SONG_MAX_INSTR ? SONG_MAX_INSTR - 1 : v;
    if ((st = step_w(row)) != 0)
        st->instrument = v < 0 ? INSTR_NONE : (uint8_t)v;
}

/* KNOB 3 on an effect: the commands in alphabetical order, --- first. One
 * that would say the same thing twice on the row is stepped over. */
static void turn_fx_cmd(int row, int slot, int d)
{
    Step *st = step_w(row);
    int at = 0;
    if (!st)
        return;
    for (int i = 0; i < fx_count; i++)
        if (fx_order[i] == st->fx[slot].cmd)
            at = i;
    for (int n = d < 0 ? -d : d; n > 0; n--)
        for (int guard = 0; guard < fx_count; guard++) {
            at += d < 0 ? -1 : 1;
            if (at < 0 || at >= fx_count) {
                at = at < 0 ? 0 : fx_count - 1;
                break;
            }
            if (!fx_conflicts(st, slot, fx_order[at]))
                break;
        }
    if (fx_conflicts(st, slot, fx_order[at]))
        return;
    st->fx[slot].cmd = fx_order[at];
    st->fx[slot].val = st->fx[slot].cmd == FX_NONE
                           ? 0
                           : fx_clamp_val(st->fx[slot].cmd, song->ticks_per_step, st->fx[slot].val);
}

static void turn_fx_val(int row, int slot, int d)
{
    uint8_t cmd = step_at(row)->fx[slot].cmd;
    Step *st = cmd != FX_NONE && cmd != FX_SYL ? step_w(row) : 0; /* a syllable is written, not turned */
    if (st)
        st->fx[slot].val = fx_clamp_val(st->fx[slot].cmd, song->ticks_per_step, st->fx[slot].val + d);
}

static void turn_len(int d)
{
    int n = pat_len() + d;
    undo_bytes(&song->patterns[track][pat].length, sizeof(uint16_t));
    song->patterns[track][pat].length = (uint16_t)(n < 1 ? 1 : n > PATTERN_MAX_STEPS ? PATTERN_MAX_STEPS : n);
    app_mark_dirty();
}

/* --------------------------------------------------------- drawing --- */
/* The beat: its first step's number is white. The meter's steps a beat
 * (PROJECT's STEP), not 4 by hand. */
static int beat(void) { return song->steps_per_beat ? song->steps_per_beat : 4; }

/* What a step's note is called. On a kit a pitch says nothing -- knowing
 * that F#2 is the snare is a thing to learn and then to keep remembering
 * (30/10) -- so the cell names the drum the note plays: BD SD CH, the two
 * letters core/drum.c already had. A note the kit does not offer on a key
 * is one of its pieces retuned; it reads as that piece with a + or a -,
 * since the name alone could not tell it apart. */
static const char *step_note_name(int r, uint8_t n, char buf[4])
{
    const InstrumentDef *in = &song->instruments[edit_instr_at(song, track, pat, r) % SONG_MAX_INSTR];
    uint8_t notes[DRUM_MAX_DRUMS];
    int at;
    if (in->engine != ENGINE_DRUM)
        return note_name(n, buf);
    at = drum_drum_of(in->timbre, n);
    drum_drums(in->timbre, notes);
    buf[0] = drum_note_abbr(in->timbre, n)[0];
    buf[1] = drum_note_abbr(in->timbre, n)[1];
    buf[2] = n == notes[at] ? '\0' : n > notes[at] ? '+' : '-';
    buf[3] = '\0';
    return buf;
}

/* The trail (01/11): the setter under the cursor is said again, faintly,
 * down the rows it still reaches -- so the cell answers "how far does THIS
 * one go", and the slot it was written in is not the question. It ends at
 * the next writing of the SAME command, wherever on the row that sits,
 * because the slot means nothing to the player. A one-shot shows none, and
 * that silence is what tells the two kinds apart on a dense page. */
static bool fx_trail(int r, int c, StepFx *out)
{
    StepFx f;
    if (c != cur_col || r <= cur_row || selecting)
        return false;
    f = step_at(cur_row)->fx[cur_col];
    if (f.cmd == FX_NONE || !fx_sticks(f.cmd))
        return false;
    for (int i = cur_row + 1; i <= r; i++)
        for (int k = 0; k < STEP_MAX_FX; k++)
            if (step_at(i)->fx[k].cmd == f.cmd)
                return false;
    *out = f;
    return true;
}

/* Does the step strike anything -- a real note, not an OFF, which cuts
 * what was already sounding and starts nothing. */
static bool step_struck(int r)
{
    const Step *st = step_at(r);
    for (int i = 0; i < STEP_MAX_NOTES; i++)
        if (st->notes[i] <= NOTE_MAX)
            return true;
    return false;
}

static void draw_cell(int v, int r, int c, bool band)
{
    const Step *st = step_at(r);
    int w = view_w[view], x = GRID_CELL_X + c * w;
    bool on = band && c == cur_col && !selecting; /* selecting, the cursor is a frame (draw) */
    char s[4];

    if (view == V_NOTES) {
        uint8_t n = st->notes[c];
        grid_cell(v, x, w, n <= NOTE_MAX ? step_note_name(r, n, s) : n == NOTE_OFF ? "OFF" : "---", band, on,
                  n <= NOTE_MAX ? (r == play_row ? C_TITLE : C_TEXT) : C_DIM);
    } else if (view == V_FX) {
        StepFx f = st->fx[c];
        bool faint = false; /* in force from above, not written here */
        int tw, tx;
        if (f.cmd == FX_NONE) {
            if (!fx_trail(r, c, &f)) {
                grid_cell(v, x, w, "---", band, on, C_DIM);
                return;
            }
            faint = true;
        }
        if (on)
            grid_hollow(v, x, w);
        if (f.cmd == FX_SYL) { /* the syllable itself, as the file writes it */
            char q[SONG_SYL_LEN + 1] = "'";
            const char *t = song_syl(song, f.val);
            for (int i = 0; t[i] && i < SONG_SYL_LEN - 1; i++)
                q[i + 1] = t[i], q[i + 2] = 0;
            gfx_text_mid(gfx_text_w(F_COND20, q) > w - 4 ? F_COND14 : F_COND20, x, w, grid_base(v), q,
                         grid_ink(band, on, C_TEXT));
            return;
        }
        tw = gfx_text_w(F_COND20, fx_name(f.cmd)) + 4 + gfx_text_w(F_COND20, grid_hex2(s, f.val));
        tx = gfx_text(F_COND20, x + (w - tw) / 2, grid_base(v), fx_name(f.cmd),
                      grid_ink(band, on, faint ? C_FX_FAINT : ui_fx_ink(f.cmd)));
        gfx_text(F_COND20, tx + 4, grid_base(v), s, grid_ink(band, on, faint ? C_TEXT_FAINT : C_TEXT));
    } else { /* the instrument the step names, or --; then the name of the one that plays:
              * white where the step names it, grey where it keeps the one in force */
        bool named = st->instrument != INSTR_NONE, sounds = false;
        int k = edit_instr_at(song, track, pat, r), tx;
        for (int i = 0; i < STEP_MAX_NOTES; i++)
            sounds |= st->notes[i] <= NOTE_MAX;
        if (on)
            grid_hollow(v, x, w);
        tx = gfx_text(F_COND20, x + 10, grid_base(v), named ? grid_hex2(s, k) : "--",
                      grid_ink(band, on, named ? C_TEXT : C_DIM));
        if (named || sounds)
            gfx_text(F_COND20, x + 42 > tx + 10 ? x + 42 : tx + 10, grid_base(v),
                     song->instruments[k % SONG_MAX_INSTR].name, grid_ink(band, on, named ? C_TEXT : C_DIM));
    }
}

/* --------------------------------------------------------------- REC --- */
/* The step nearest to the playhead: the one playing, or the next when it
 * is past its middle. */
static int rec_nearest(void)
{
    int len = pat_len(), tps = song->ticks_per_step ? song->ticks_per_step : 1;
    int r = player_row() % len;
    return player_tick_in_step() * 2 >= tps ? (r + 1) % len : r;
}

static void rec_keys(const PlatInput *in, uint32_t keys)
{
    int instr = edit_instr_at(song, track, pat, rec_row >= 0 ? rec_row : rec_nearest());
    for (int k = 0; k < PLAT_KEYS; k++) {
        int n = edit_key_note(song, instr, kbd_oct, k);
        Step *st;
        if (!((keys >> k) & 1u) || n < NOTE_MIN || n > NOTE_MAX)
            continue;
        if (rec_row < 0) { /* the chord's first key: the step is replaced */
            rec_row = rec_nearest();
            rec_slot = 0;
            if ((st = step_w(rec_row)) != 0)
                for (int i = 0; i < STEP_MAX_NOTES; i++)
                    st->notes[i] = NOTE_NONE;
        }
        if (rec_slot < STEP_MAX_NOTES && !edit_write_note(song, track, pat, rec_row, rec_slot++, (uint8_t)n))
            said = "POOL FULL";
        player_note_on(edit_instr_at(song, track, pat, rec_row), (uint8_t)n); /* heard as played */
    }
    if (!in->keys && rec_row >= 0) { /* the last key let go: an OFF there, the note's length */
        int off = rec_nearest();
        Step *st;
        if (off == rec_row) /* let go within its own step: the OFF on the next */
            off = (off + 1) % pat_len();
        if ((st = step_w(off)) != 0) {
            for (int i = 0; i < STEP_MAX_NOTES; i++)
                st->notes[i] = NOTE_NONE;
            st->notes[0] = NOTE_OFF;
        }
        player_note_off();
        rec_row = -1;
    }
}

/* --------------------------------------------------------- selection --- */
/* The selected steps and columns of the view, inclusive; c0 -1: whole steps. */
static void sel_rect(int *r0, int *r1, int *c0, int *c1)
{
    int ar = anchored ? an_row : cur_row, ac = anchored ? an_col : cur_col;
    *r0 = ar < cur_row ? ar : cur_row;
    *r1 = ar < cur_row ? cur_row : ar;
    *c0 = ac < cur_col ? ac : cur_col;
    *c1 = ac < cur_col ? cur_col : ac;
    if (*c0 < 0)
        *c0 = -1, *c1 = view_cols[view] - 1;
}

/* The clipboard's fields for columns c0..c1 of the view (edit.h). */
static int sel_mask(int c0, int c1)
{
    int m = 0;
    if (c0 < 0)
        return EDIT_CLIP_ALL;
    for (int c = c0; c <= c1; c++)
        m |= view == V_NOTES ? EDIT_CLIP_N1 << c : view == V_FX ? EDIT_CLIP_FX1 << c : EDIT_CLIP_INSTR;
    return m;
}

static void sel_op(int op)
{
    static char word[16];
    int r0, r1, c0, c1, n, done;
    sel_rect(&r0, &r1, &c0, &c1);
    n = r1 - r0 + 1;
    if (op == OP_LOOP) { /* the selection's steps, PLAY going round them; with none, an end at the cursor */
        if (anchored)
            loop_set(entry, r0, r1);
        else
            loop_press(entry, cur_row);
        anchored = false;
        said = loop_state() == LOOP_SET ? "LOOP" : loop_state() == LOOP_ONE_END ? "LOOP: THE OTHER END" : "NO LOOP";
        return;
    }
    if (op == OP_PASTE) {
        if (!edit_clip_len())
            return;
        done = edit_clip_paste(song, track, pat, cur_row, view == V_FX && cur_col >= 0 ? cur_col : -1);
        if (done < 0) {
            said = "POOL FULL";
            return;
        }
        n = done;
    } else {
        if (op == OP_COPY || op == OP_CUT) {
            edit_clip_copy(song, track, pat, r0, n, sel_mask(c0, c1));
            clip_cols = c0 < 0 ? -1 : c1 - c0 + 1;
        }
        if ((op == OP_CUT || op == OP_CLEAR) && !edit_erase_range(song, track, pat, r0, n, sel_mask(c0, c1))) {
            said = "POOL FULL";
            return;
        }
        anchored = false;
    }
    if (op == OP_COPY && c0 < 0 && n == 1) { /* the view shows less than it took: say so */
        said = "COPIED STEP";
        return;
    }
    {
        const char *w = op == OP_COPY ? "COPIED " : op == OP_PASTE ? "PASTED " : op == OP_CUT ? "CUT " : "CLEARED ";
        char d[8];
        int k = 0;
        for (; *w; w++)
            word[k++] = *w;
        for (const char *q = ui_dec(d, n); *q; q++)
            word[k++] = *q;
        word[k] = 0;
        said = word;
    }
}

/* ---------------------------------------------------------- syllable --- */
/* SEL on an effect slot: the row's syllable, in its own slot if it has
 * one, else in this one when it is empty. Another effect is left alone. */
static void syl_open(void)
{
    const Step *st = step_at(cur_row);
    syl_slot = cur_col;
    for (int i = 0; i < STEP_MAX_FX; i++)
        if (st->fx[i].cmd == FX_SYL)
            syl_slot = i;
    if (st->fx[syl_slot].cmd != FX_NONE && st->fx[syl_slot].cmd != FX_SYL)
        return;
    syllable_open(st->fx[syl_slot].cmd == FX_SYL ? song_syl(song, st->fx[syl_slot].val) : "",
                  edit_instr_at(song, track, pat, cur_row), st->notes[0] <= NOTE_MAX ? st->notes[0] : 60, track,
                  cur_row);
    writing_syl = true;
}

static void write_syl(void)
{
    const char *t = syllable_text();
    const Step *rd = step_at(cur_row);
    int idx = t[0] ? song_syl_intern(song, t) : -1;
    Step *st;
    if (t[0] && idx < 0) {
        said = "SYL FULL";
        return;
    }
    if (!t[0] && rd->fx[syl_slot].cmd != FX_SYL)
        return; /* nothing was there, nothing is */
    if ((st = step_w(cur_row)) == 0)
        return;
    st->fx[syl_slot].cmd = t[0] ? FX_SYL : FX_NONE;
    st->fx[syl_slot].val = t[0] ? (uint8_t)idx : 0;
}

/* ------------------------------------------------------------ chords --- */
static bool chords_here(void)
{
    int k = edit_instr_at(song, track, pat, cur_row) % SONG_MAX_INSTR;
    return view == V_NOTES && !selecting && !ui_shifted(&shift) && song->instruments[k].engine != ENGINE_DRUM;
}

/* What CHORDS kept, on the step: the notes replace the step's, and the
 * arpeggio takes its own slot or an empty one -- it was offered only where
 * there is one -- or leaves when set back to --. */
static void chords_write(void)
{
    uint8_t nn[STEP_MAX_NOTES];
    int n = chords_notes(nn), arp = chords_arp(), slot = -1;
    Step *st = step_w(cur_row);
    if (!st)
        return; /* POOL FULL, said by step_w */
    for (int i = 0; i < STEP_MAX_NOTES; i++)
        st->notes[i] = i < n ? nn[i] : NOTE_NONE;
    for (int i = STEP_MAX_FX - 1; i >= 0; i--)
        if (st->fx[i].cmd == FX_NONE)
            slot = i; /* the first empty one */
    for (int i = 0; i < STEP_MAX_FX; i++)
        if (st->fx[i].cmd == FX_ARP)
            slot = i; /* its own before any */
    if (slot < 0)
        return;
    if (arp) {
        st->fx[slot].cmd = FX_ARP;
        st->fx[slot].val = (uint8_t)arp;
    } else if (st->fx[slot].cmd == FX_ARP) {
        st->fx[slot].cmd = FX_NONE;
        st->fx[slot].val = 0;
    }
}

/* ------------------------------------------------- choose instrument --- */
static int ch_instr(int k) { return k == 0 ? edit_instr_at(song, track, pat, cur_row) : k - 1; }

static void ch_hush(void)
{
    if (sounding >= 0)
        player_note_off();
    sounding = -1;
}

/* PLAY in the list plays the instrument's own preview phrase, as
 * INSTRUMENT's PLAY does: one note is too short to judge a sound by
 * (30/10). C E G C an octave on, a note every 250 ms, a rest, again; a
 * kit plays its drums instead. The phrase starts afresh on the row the
 * cursor comes to rest on; + NEW has nothing to play. */
static void ch_listen(uint32_t ms)
{
    ch_hush();
    if (ch_cur == CH_NEW)
        return;
    sounding = ch_cur;
    pv_t0 = ms;
    pv_at = -1;
}

/* The row's own phrase, the one INSTRUMENT's PLAY plays: `song_instr_demo`
 * holds it, so walking the list is walking from sound to sound and not
 * from register to register. */
static void ch_preview(uint32_t ms)
{
    int k = ch_instr(sounding);
    const Demo *d = song_instr_demo(song, k % SONG_MAX_INSTR);
    int at = (int)((ms - pv_t0) / d->step_ms % song_demo_steps(d));
    uint8_t nn[STEP_MAX_NOTES];
    int n;
    if (at == pv_at)
        return;
    pv_at = at;
    n = song_demo_notes(d, at, nn);
    if (n)
        player_note_chord(k, nn, n); /* one note or a pad's triad, struck as a step is */
    else
        player_note_off();
}

static void ch_close(bool take)
{
    Step *st;
    listening = false;
    ch_hush();
    if (take && ch_cur - 1 != ch_orig && (st = step_w(cur_row)) != 0)
        st->instrument = ch_cur == 0 ? INSTR_NONE : (uint8_t)(ch_cur - 1);
    choosing = false;
    gfx_invalidate();
}

/* The first instrument slot the song has not taken: no step of any
 * block names it and the hand has not named it. -1 when all 64 are
 * taken -- the list says so on the + NEW row rather than doing nothing
 * silently. */
static int ch_free_instr(void)
{
    bool taken[SONG_MAX_INSTR] = { false };
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
            if (song_pattern_blank(song, t, p))
                continue;
            for (int r = 0; r < song->patterns[t][p].length; r++) {
                uint8_t k = song_step(song, t, p, r)->instrument;
                if (k < SONG_MAX_INSTR)
                    taken[k] = true;
            }
        }
    for (int i = 0; i < SONG_MAX_INSTR; i++)
        if (!taken[i] && !song->instruments[i].name[0])
            return i;
    return -1;
}

/* + NEW: the free slot, back to neutral, named by the step, and the
 * list is done -- SEL on the slot then opens its INSTRUMENT screen. */
static void ch_new(void)
{
    Step *st;
    if (ch_free < 0) /* the row says FULL: nothing to take */
        return;
    if ((st = step_w(cur_row)) == 0)
        return; /* POOL FULL, said by step_w: the slot is left as it was */
    undo_bytes(&song->instruments[ch_free], sizeof song->instruments[ch_free]);
    song_instr_init(song, ch_free);
    st->instrument = (uint8_t)ch_free;
    ch_close(false);
}

static void ch_open(void)
{
    const Step *st = step_at(cur_row);
    ch_orig = st->instrument == INSTR_NONE ? -1 : st->instrument;
    ch_cur = ch_orig + 1;
    ch_top = 0;
    listening = false;
    settle = 0;
    sounding = -1;
    ch_free = ch_free_instr();
    grid_follow(&ch_cur, &ch_top, CH_N, CH_ROWS);
    choosing = true;
}

static void ch_frame(const PlatInput *in, uint32_t btns)
{
#define PRESSED(b) ((btns >> (b)) & 1u)
    /* PRESETS walks it too, the ear included: what opens a list keeps
     * driving it (30/10). SELECT and KNOB 1 stay what they were. */
    ch_cur += in->enc[PE_SELECT] + in->enc[PE_K1] + in->enc[PE_PRESET];
    grid_follow(&ch_cur, &ch_top, CH_N, CH_ROWS);
    if (in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_PRESET])
        settle = SETTLE;
    if (PRESSED(PB_PLAY)) {
        if (player_playing()) /* the phrase, not the song: as INSTRUMENT's PLAY does */
            player_stop();
        listening = !listening;
        if (listening)
            ch_listen(in->ms);
        else
            ch_hush();
    } else if (listening && settle > 0 && --settle == 0 && ch_cur != sounding) {
        ch_listen(in->ms);
    }
    if (listening && sounding >= 0)
        ch_preview(in->ms);
    if (PRESSED(PB_SEL)) {
        if (ch_cur == CH_NEW)
            ch_new();
        else
            ch_close(true);
    } else if (PRESSED(PB_HOME))
        ch_close(false);
    ui_led_state(PB_PLAY, listening);
#undef PRESSED
}

static void ch_hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 6, word, C_DIM);
}

static void ch_draw(void)
{
    char s[24] = "STEP ", h[3];
    int n;
    grid_hex2(s + 5, cur_row);
    for (n = 0; s[n]; n++)
        ;
    for (const char *w = " / TRK "; *w;)
        s[n++] = *w++;
    s[n++] = (char)('1' + track), s[n] = 0;
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND14, 6, 17, "CHOOSE INSTRUMENT", C_TITLE);
    gfx_text(F_COND12, PLAT_W - 12 - gfx_text_w(F_COND12, s), 17, s, C_DIM);
    gfx_fill(0, CH_TOP - 3, PLAT_W, 1, C_TRACK, 255);
    for (int r = 0; r < CH_ROWS && ch_top + r < CH_N; r++) {
        int k = ch_top + r, y = CH_TOP + r * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent;
        bool here = k == ch_cur, adder = k == CH_NEW;
        const char *name = adder ? "" : song->instruments[ch_instr(k)].name;
        uint16_t ink = here ? C_ON_ACC : k == 0 || !name[0] ? C_DIM : C_TEXT;
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 14, GRID_ROW - 2, C_ACCENT, 255);
        if (!adder && k - 1 == ch_orig) /* what the step says now, as HOME marks the open project */
            gfx_fill(6, y + 6, 3, GRID_ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
        if (adder) { /* + NEW, as INSTRUMENT's + LAY: then the slot it would take, or FULL */
            gfx_text(F_COND20, 16, base, "+ NEW", here ? C_ON_ACC : C_TEXT);
            gfx_text(F_COND20, 84, base, ch_free >= 0 ? grid_hex2(h, ch_free) : "FULL",
                     here ? C_ON_ACC : ch_free >= 0 ? C_DIM : C_MUTE);
            continue;
        }
        gfx_text(F_COND20, 16, base, k == 0 ? "--" : grid_hex2(h, k - 1), ink);
        if (k == 0) /* none named: the one in force plays */
            gfx_text(F_COND14, 48, base, "keep", ink);
        gfx_text(F_COND20, k == 0 ? 84 : 48, base, name, ink);
        grid_play_sign(PLAT_W - 32, y + 8, here ? C_ON_ACC : k == sounding ? C_ACCENT : C_DIM, k == sounding);
    }
    {
        int hh = CH_ROWS * GRID_ROW * CH_ROWS / CH_N, y = CH_TOP + CH_ROWS * GRID_ROW * ch_top / CH_N;
        gfx_fill(PLAT_W - 4, CH_TOP, 2, CH_ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, hh, C_DIM, 255);
    }
    ch_hint(6, "SEL", ch_cur == CH_NEW ? "MAKE" : "TAKE", false);
    ch_hint(96, "PLAY", "LISTEN", false);
    ch_hint(PLAT_W - 6, "HOME", "BACK", true);
}

static void draw(void)
{
    int len = pat_len(), cols = view_cols[view], w = view_w[view];
    char s[8];

    grid_clear();
    s[0] = 'T', s[1] = (char)('1' + track), s[2] = 0;
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else if (ui_speed_word()) /* the number, while the hand is on it; the triangle says the rest */
        grid_corner(ui_speed_word(), C_FX, 0, 0);
    else if (counting) { /* the count-in's beat */
        char b[2] = { (char)('0' + count_beat), 0 };
        grid_corner("REC", C_MUTE, b, C_TEXT);
    } else if (rec_armed)
        grid_corner(recording ? "REC" : "REC ARMED", C_MUTE, 0, 0);
    else if (selecting) /* the mode, in place of where one is */
        grid_corner("SELECT", C_FX, 0, 0);
    else if (loop_state() == LOOP_SET) { /* the loop, in place of where one is: LOOP 02-05 */
        char lo[3], hi[3], range[6];
        grid_hex2(lo, loop_from()), grid_hex2(hi, loop_to());
        range[0] = lo[0], range[1] = lo[1], range[2] = '-', range[3] = hi[0], range[4] = hi[1], range[5] = 0;
        grid_corner("LOOP", C_TITLE, range, C_TITLE);
    }
    else if (cur_col < 0) /* the block's length, while the cursor is on a number */
        grid_corner("LEN", C_DIM, ui_dec(s + 3, len), C_TEXT);
    else /* the track, green while it plays alone, red while muted (MONO) */
        grid_corner(s, player_soloed(track) ? C_ACCENT : player_muted(track) ? C_MUTE : C_TITLE, view_name[view],
                    C_TEXT);
    for (int c = 0; c < cols; c++) {
        char h[6] = { 0 };
        if (view == V_NOTES)
            h[0] = (char)('1' + c);
        else if (view == V_FX)
            h[0] = 'F', h[1] = 'X', h[2] = ' ', h[3] = (char)('1' + c);
        grid_head(GRID_CELL_X + c * w, w, view == V_INS ? "INSTRUMENT" : h, c == cur_col);
    }
    /* and the next view, against the right edge of that same line. It has to
     * clear the column names, and the FX view all but fills the line: "FX 3"
     * ends at x 209, which leaves 27 px to PLAT_W - 4. In the header's own
     * CONDENSED 12 "SEQ ->" is 27 px wide -- it would start exactly where
     * "FX 3" ends, and the panel showed the two reading as one word, "FX 3
     * SEQ ->". So the marker is set in CONDENSED 9, the smallest there is:
     * 21 px, starting at 215, six clear of the head, and small enough that
     * the eye takes it for what it is. Dim, like the rest of the header: a
     * hint, not a control. */
    gfx_text(F_COND9, PLAT_W - 4 - gfx_text_w(F_COND9, view_next[view]), GRID_HEAD - 3, view_next[view], C_DIM);
    for (int v = 0; v < GRID_ROWS && top + v < len; v++) {
        int r = top + v;
        bool band = r == cur_row;
        if (band)
            grid_band(v, PLAT_W - 4 - (len > GRID_ROWS ? 6 : 0));
        if (r == play_row)
            grid_playing(v, band);
        if (selecting) {
            int r0, r1, c0, c1;
            sel_rect(&r0, &r1, &c0, &c1);
            if (anchored && r >= r0 && r <= r1) /* the selection, under the cells */
                gfx_fill(c0 < 0 ? 2 : GRID_CELL_X + c0 * w, grid_y(v),
                         c0 < 0 ? GRID_CELL_X + cols * w - 2 : (c1 - c0 + 1) * w, GRID_ROW, C_SEL, 255);
        }
        if (loop_has(r) && loop_state() == LOOP_SET) /* the loop: a cyan rule at the left edge */
            gfx_fill(0, grid_y(v) + (r == loop_from() ? 3 : 0), 3,
                     GRID_ROW - (r == loop_from() ? 3 : 0) - (r == loop_to() ? 3 : 0), C_TITLE, 255);
        uint16_t nink =
            r == play_row || (loop_has(r) && loop_state() == LOOP_SET) ? C_TITLE : r % beat() ? C_DIM : C_TEXT;
        grid_num(v, r, band, band && cur_col < 0, nink);
        /* neither the FX view nor the INS one shows the notes, so the number
         * says whether the step strikes one: an effect, or an instrument, on a
         * struck step and on an empty one are two different readings, and
         * nothing else on those pages tells them apart. */
        if (view != V_NOTES && step_struck(r))
            grid_num_rule(v, r, band, band && cur_col < 0, nink);
        for (int c = 0; c < cols; c++)
            draw_cell(v, r, c, band);
    }
    if (selecting && cur_row >= top && cur_row < top + GRID_ROWS) /* the cursor, over the selection */
        gfx_frame_rect(cur_col < 0 ? 2 : GRID_CELL_X + cur_col * w, grid_y(cur_row - top),
                       cur_col < 0 ? GRID_CELL_X - 4 : w, GRID_ROW, C_TEXT, 255);
    if (selecting && !anchored && edit_clip_len()) { /* where OP2 would put the clipboard */
        int c0 = clip_cols < 0 || cur_col < 0 ? 0 : cur_col, nc = clip_cols < 0 ? cols : clip_cols;
        int v0 = cur_row - top, v1 = cur_row + edit_clip_len() - 1 - top;
        v1 = v1 >= GRID_ROWS ? GRID_ROWS - 1 : v1;
        if (c0 + nc > cols)
            nc = cols - c0;
        if (v0 >= 0 && v0 < GRID_ROWS)
            gfx_frame_rect(GRID_CELL_X + c0 * w, grid_y(v0), nc * w, grid_y(v1) + GRID_ROW - grid_y(v0), C_FX, 255);
    }
    grid_frame(top, len);
}

/* ------------------------------------------------------------- api --- */
bool pattern_open(Song *s, int e, int t)
{
    if (e < 0 || e >= s->arrange_len || t < 0 || t >= s->num_tracks ||
        s->arrangement[e].pattern[t] >= TRACK_MAX_PATTERNS)
        return false;
    song = s;
    entry = e;
    track = t;
    pat = s->arrangement[e].pattern[t];
    cur_row = top = 0;
    strip_go(strip_first(V_NOTES) + 1); /* the first note */
    said = 0;
    choosing = listening = writing_syl = in_chords = selecting = trying = false;
    rec_armed = counting = recording = false;
    rec_row = -1;
    shift = (UiShift){ 0 };
    sounding = -1;
    au_quiet();
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    /* KNOB 4's order: alphabetical, the empty slot first. */
    fx_count = 0;
    fx_order[fx_count++] = FX_NONE;
    for (int c = 1; c < FX_COUNT; c++) {
        int i;
        if (c == FX_SYL || c == FX_SMP) /* the SYLLABLE screen, the sample table */
            continue;
        i = fx_count++;
        while (i > 1) {
            const char *a = fx_name(fx_order[i - 1]), *b = fx_name((uint8_t)c);
            int k = 0;
            while (k < 3 && a[k] == b[k])
                k++;
            if (k == 3 || a[k] < b[k])
                break;
            fx_order[i] = fx_order[i - 1];
            i--;
        }
        fx_order[i] = (uint8_t)c;
    }
    gfx_invalidate();
    return true;
}

void pattern_from_phrase(int row)
{
    cur_row = row < 0 ? 0 : row >= pat_len() ? pat_len() - 1 : row;
    top = cur_row >= GRID_ROWS ? cur_row - GRID_ROWS / 2 : 0;
}

int pattern_instr(void) { return edit_instr_at(song, track, pat, cur_row); }
int pattern_entry(void) { return entry; }
bool pattern_in_syllable(void) { return writing_syl; }

void pattern_resume(void)
{
    prev_btns = prev_keys = 0xFFFFFFFFu;
    said = 0;
    gfx_invalidate();
}

int pattern_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
    int len = pat_len(), d;
    bool playing = player_playing();
    bool started = false; /* this frame set the playhead going: the view follows at once */
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    prev_keys = in->keys;
    if (btns || keys || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3] ||
        in->enc[PE_K4] || in->enc[PE_PRESET])
        said = 0;

    if (choosing) {
        ch_frame(in, btns);
        ui_led_guide(0); /* CHOOSE INSTRUMENT: SEL, GLO back, PLAY */
        gfx_frame(choosing ? ch_draw : draw);
        return PATTERN_STAY;
    }
    if (in_chords) {
        int r = chords_frame(in, btns, keys);
        if (r == CHORDS_KEPT)
            chords_write();
        if (r != CHORDS_OPEN) {
            in_chords = false;
            gfx_invalidate();
        }
        ui_led_guide(in_chords ? chords_buttons() : 0u);
        gfx_frame(in_chords ? chords_draw : draw);
        return PATTERN_STAY;
    }
    if (writing_syl) {
        int r = syllable_frame(in, btns, keys);
        if (r == SYLLABLE_KEPT)
            write_syl();
        if (r != SYLLABLE_TYPING) {
            writing_syl = false;
            gfx_invalidate();
        }
        ui_led_guide(writing_syl ? syllable_buttons() : 0u);
        gfx_frame(writing_syl ? syllable_draw : draw);
        return PATTERN_STAY;
    }
    if (PRESSED(PB_ENV) && view == V_INS) { /* ENV twice: from the column into the instrument */
        au_quiet();
        player_note_off();
        return PATTERN_INSTR;
    }
    if (PRESSED(PB_SEQ) || PRESSED(PB_FX) || PRESSED(PB_ENV)) { /* a button a view */
        au_hush(); /* another view: nothing of the ear's is left sounding */
        selecting = false; /* its columns were the other view's */
        /* a jump to the head of the group, which is its first column, or its
         * number when the cursor was on one: the buttons keep their role */
        strip_go(strip_first(PRESSED(PB_SEQ) ? V_NOTES : PRESSED(PB_FX) ? V_FX : V_INS) + (cur_col < 0 ? 0 : 1));
    }
    /* EDIT is the shift and nothing else, and a tap of it is that shift's
     * CAPS LOCK (31/10): the black keys stay commands until EDIT is tapped
     * again. The SELECTION is OP5's own, on and off -- EDIT held or latched
     * to reach the key the first time, a black key alone being a note here,
     * then the key alone once the keys are commands. The tap used to open
     * the SELECTION itself, which is the half of it nobody wanted. MONO, in
     * either: the track alone, or muted (ui.h). */
    if (ui_shift(&shift, in, btns, keys, selecting, track, &said))
        shift.latch = !shift.latch;
    if ((keys & 1u << OP_SEL) && (selecting || ui_shifted(&shift))) {
        selecting = !selecting;
        anchored = selecting;
        an_row = cur_row, an_col = cur_col;
    }
    if (selecting || ui_shifted(&shift)) {
        if (keys & 1u << OP_COPY)
            sel_op(OP_COPY);
        if (keys & 1u << OP_PASTE)
            sel_op(OP_PASTE);
        if (keys & 1u << OP_CUT)
            sel_op(OP_CUT);
        if (keys & 1u << OP_CLEAR)
            sel_op(OP_CLEAR);
        if (keys & 1u << OP_LOOP)
            sel_op(OP_LOOP);
        keys = 0; /* the keyboard is the ops' here: no note is written */
    }
    /* SEL on the instrument slot goes into the INSTRUMENT screen of the one
     * that plays there: ENV pressed twice read as counter-intuitive, and SEL
     * is free here now that the list is the PRESETS knob's (30/10). */
    if (PRESSED(PB_SEL) && view == V_INS && cur_col >= 0 && !selecting) {
        au_quiet();
        player_note_off();
        return PATTERN_INSTR;
    }
    if (PRESSED(PB_SEL) && view == V_FX && cur_col >= 0 && !selecting)
        syl_open();
    /* PRESETS on the slot: CHOOSE INSTRUMENT, where a whole sound can be
     * heard before it is taken. Nothing else on this screen used the knob. */
    if (in->enc[PE_PRESET] && view == V_INS && cur_col >= 0 && !selecting) {
        au_hush();
        ch_open();
    }
    /* ARP: what the key suggests, for the step under the cursor. A kit has no
     * chords, and the keys being written or counted in are not interrupted. */
    if (PRESSED(PB_ARP) && chords_here() && !counting && !recording) {
        au_quiet();
        chords_open(song, edit_instr_at(song, track, pat, cur_row), &kbd_oct, step_at(cur_row));
        in_chords = true;
        ui_led_guide(chords_buttons());
        gfx_frame(chords_draw);
        return PATTERN_STAY;
    }
    if (in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2]) /* the hand left the step */
        au_hush();
    /* SELECT and KNOB 1 are one hand (29/10), and neither touches the
     * column: SELECT brought the cursor to the step's number until 2/11,
     * when the column jumping out from under one knob and not the other
     * read as a lurch with the app in hand. The number is still one detent
     * to the left, on KNOB 2. */
    cur_row += in->enc[PE_SELECT] + in->enc[PE_K1];
    cur_row = cur_row < 0 ? 0 : cur_row >= len ? len - 1 : cur_row;
    if (in->enc[PE_K2]) {
        if (selecting)
            strip_turn_sel(in->enc[PE_K2]);
        else
            strip_turn(in->enc[PE_K2]);
    }
    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;

    if ((d = in->enc[PE_K3]) != 0 && !selecting) { /* selecting, the knobs only move */
        if (cur_col < 0)
            turn_len(d);
        else if (view == V_NOTES && !edit_turn_note(song, track, pat, cur_row, cur_col, d))
            said = "POOL FULL";
        else if (view == V_FX)
            turn_fx_cmd(cur_row, cur_col, d);
        else if (view == V_INS) {
            turn_instr(cur_row, d);
            /* the ear, once the hand rests; the song playing, it keeps quiet */
            au_hush();
            if (!playing)
                au_settle = AU_SETTLE;
        }
    }
    if ((d = in->enc[PE_K4]) != 0 && view == V_FX && cur_col >= 0 && !selecting)
        turn_fx_val(cur_row, cur_col, d);

    if (PRESSED(PB_REC) && !shift.held) { /* armed, or not; stopping it stops the count */
        au_hush();
        rec_armed = !rec_armed;
        recording = rec_armed && playing;
        rec_live = recording;
        counting = false;
        rec_row = -1;
        said = rec_armed ? "REC" : "REC OFF";
    }
    if (counting) { /* a bar of clicks, the meter's beats, then the phrase from the top */
        uint32_t beat = 60000u / (song->bpm ? song->bpm : 120);
        int beats = (song->beats_per_bar ? song->beats_per_bar : 4) * ui_count_in; /* SETTINGS' bars */
        if (in->ms - count_t0 >= (uint32_t)count_beat * beat) {
            if (count_beat == beats) {
                counting = false;
                recording = true;
                rec_live = false;
                player_play_entry_at(entry, cur_row);
                started = true;
            } else {
                player_click(count_beat % (beats / ui_count_in) == 0); /* each bar's first beat higher */
                count_beat++;
            }
        }
    }
    if (recording && playing)
        rec_live = true;
    if (recording && rec_live && !playing) /* STOP ends it -- not the frames before PLAY is taken up */
        recording = rec_live = false, rec_armed = false;
    if (recording) {
        rec_keys(in, keys);
        keys = 0;
    }

    /* The keys write notes in a note column of NOTES: the first one in the
     * slot under the cursor, the ones pressed while it is held in the slots
     * after it. Anywhere else -- the step's number, FX, INS -- they only
     * play, with the step's instrument: no slot is chosen for the hand
     * (29/10). */
    if (!in->keys) {
        if (chord_next || trying)
            player_note_off();
        chord_next = 0;
        trying = false;
    }
    for (int k = 0; k < PLAT_KEYS; k++) {
        int n = edit_key_note(song, edit_instr_at(song, track, pat, cur_row), kbd_oct, k), slot;
        if (!((keys >> k) & 1u) || n < NOTE_MIN || n > NOTE_MAX)
            continue;
        if (view != V_NOTES || cur_col < 0 || rec_armed) {
            player_note_on(edit_instr_at(song, track, pat, cur_row), (uint8_t)n);
            trying = true;
            continue;
        }
        slot = chord_next ? chord_next : cur_col;
        if (slot >= STEP_MAX_NOTES)
            continue;
        if (!edit_write_note(song, track, pat, cur_row, slot, (uint8_t)n))
            said = "POOL FULL";
        player_note_on(edit_instr_at(song, track, pat, cur_row), (uint8_t)n); /* heard as written */
        chord_next = slot + 1;
    }
    /* A MIDI keyboard plays the instrument the cursor is on and writes
     * nothing: the pitch is the keyboard's own (ui.h) */
    ui_midi_play(in, edit_instr_at(song, track, pat, cur_row));

    if (PRESSED(PB_SAVE))
        said = ui_save_word(app_save());
    if (undo_turn(in->enc[PE_ALGO], &said)) {
        /* what was taken back may be older than this screen: the block
         * the phrase names here, or the track itself */
        pat = track < song->num_tracks ? song->arrangement[entry].pattern[track] : PATTERN_NONE;
        if (pat >= TRACK_MAX_PATTERNS) {
            player_note_off();
            return PATTERN_SONG; /* nothing left to show */
        }
    }
    if (PRESSED(PB_PLAY)) {
        au_hush();
        if (playing || counting) {
            /* STOP does not move the cursor: it stayed on the step PLAY was
             * asked from, and the view scrolls back to it on its own. It
             * followed the playhead until 2/11, which cost the step one had
             * pointed at and read as the less intuitive of the two with a
             * loop under the ear (it was asked for on 30/10). */
            player_stop();
            counting = recording = false;
        } else if (rec_armed && ui_count_in) { /* armed: the bars counted in first */
            counting = true;
            count_beat = 0;
            count_t0 = in->ms;
        } else if (rec_armed) { /* no count: at once */
            recording = true;
            rec_live = false;
            player_play_entry_at(entry, cur_row);
            started = true;
        } else {
            /* PLAY from the step under the cursor, as SONG plays from the
             * phrase under it (2/11) */
            player_play_entry_at(entry, cur_row);
            started = true;
        }
    }
    ui_led_state(PB_REC, rec_armed);

    if (playing) /* the transport is not fought: the music speaks */
        au_hush();
    else if (au_settle > 0 && --au_settle == 0)
        au_sound();
    else if (au_sounding && au_hold > 0 && --au_hold == 0)
        au_hush(); /* the step has rung: nothing is left hanging */

    len = pat_len();
    play_row = playing && player_entry() == entry ? player_row() % len : -1;
    /* The press that STARTS the transport is not the hand busying itself:
     * the view goes with the playhead from that frame on. Counted as a
     * touch, it held the view on the cursor for GRID_FOLLOW_IDLE frames --
     * 1.5 s, fourteen steps at 138 bpm -- and the follow then jumped a page
     * at a time from wherever the playhead had got to, so a whole page
     * between the two was never shown (2/11). */
    if (started)
        still = GRID_FOLLOW_IDLE;
    else
        still = btns || keys || in->keys || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] ||
                        in->enc[PE_K3] || in->enc[PE_K4] || in->enc[PE_PRESET]
                    ? 0
                    : still < GRID_FOLLOW_IDLE ? still + 1 : still;
    if (play_row >= 0 && still >= GRID_FOLLOW_IDLE) /* autoscroll: the view goes with what plays */
        grid_follow_play(&top, play_row, len, GRID_ROWS);
    else
        grid_follow(&cur_row, &top, len, GRID_ROWS);
    ui_led_state(PB_PLAY, playing);
    /* the other views, ENV always (twice: the instrument), the keyboard's octave */
    /* EDIT is lit only while its CAPS LOCK is on: a shift the hand has under
     * its finger says itself, a shift that sticks has to be told. What says
     * the SELECTION is on is OP5's own light (ui.h, 31/10) */
    ui_led_guide((view != V_NOTES ? 1u << PB_SEQ : 0u) | (view != V_FX ? 1u << PB_FX : 0u) | 1u << PB_ENV |
                 1u << PB_GLO | (shift.latch ? 1u << PB_EDIT : 0u) | (chords_here() ? 1u << PB_ARP : 0u) |
                 (selecting ? 0u : 1u << PB_OCTDN | 1u << PB_OCTUP));
    /* the ops: copy and cut and clear on a selection or on what is under the cursor, paste once
     * something was copied. Otherwise, on a kit, the keys that are drums -- the only thing on the
     * machine that says where the snare is, and the eight or eleven lit are the whole kit (30/10) */
    ui_led_keys(!selecting && !ui_shifted(&shift)
                    ? edit_key_drums(song, edit_instr_at(song, track, pat, cur_row))
                    : 1u << OP_COPY | 1u << OP_CUT | 1u << OP_CLEAR | 1u << OP_SEL |
                          1u << OP_LOOP | 1u << OP_MONO | (edit_clip_len() ? 1u << OP_PASTE : 0u),
                /* OP5 lit while the SELECTION is on: the key that opened the mode says so */
                (selecting ? 1u << OP_SEL : 0u) | (track >= 0 && player_soloed(track) ? 1u << OP_MONO : 0u),
                track >= 0 && player_muted(track) ? 1u << OP_MONO : 0u);
    gfx_frame(choosing ? ch_draw : draw);
    if (PRESSED(PB_HOME) && selecting) { /* a step back: out of the SELECTION first */
        selecting = false;
        return PATTERN_STAY;
    }
    if (PRESSED(PB_HOME)) {
        au_quiet();
        player_note_off();
        song_pool_compact(song);
        return PATTERN_BACK;
    }
    if (PRESSED(PB_GLO)) {
        au_quiet();
        player_note_off();
        return PATTERN_PROJECT;
    }
    return PATTERN_STAY;
#undef PRESSED
}
