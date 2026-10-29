/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern.h"

#include "../core/player.h"
#include "app.h"
#include "edit.h"
#include "gfx.h"
#include "grid.h"
#include "ui.h"
#include "undo.h"

/* PATTERN, as validated on the panel on 28/10 (NOTES.md): one block of
 * one track, in three views, a button each (29/10), each SONG's grid once
 * more -- one row a step, its number at the left, white on the beat.
 *
 *   NOTES  the four notes of a chord          KNOB 2 a semitone, under the lowest OFF then empty
 *   FX     the three effects                  KNOB 2 the value, KNOB 3 the effect
 *   INS    the instrument the step names, or --, then the name of the one that plays:
 *          white where the step names it, grey where it keeps the one in force (29/10)
 *
 *   SELECT  the step     KNOB 1  the column; left of the first, the step's number (KNOB 2 the
 *                        block's length)
 *   the keys  write the note under the cursor (a chord while one is held), OCT-/OCT+ their octave
 *   SEQ   NOTES     FX  FX     ENV  INS; on INS, the INSTRUMENT that plays on the step
 *   SEL   on INS: CHOOSE INSTRUMENT, a list of them for the step (PLAY listens: the one
 *         the cursor rests on sounds the step's note; SEL takes it, GLO goes back)
 *   GLO   back where it was opened from     ARP  UNDO     SAVE  PLAY  as printed
 * FX is a view here, not the MIXER: FX shows the effects of where one is. EDIT
 * does nothing: there is nothing under the cursor the views do not already edit. */
enum { V_NOTES, V_FX, V_INS, VIEWS };
static const char *const view_name[VIEWS] = { "NOTES", "FX", "INS" };
static const int view_cols[VIEWS] = { STEP_MAX_NOTES, STEP_MAX_FX, 1 };
static const int view_w[VIEWS] = { 38, 60, 180 };

static Song *song;
static int entry, track, pat;
static int view, cur_row, cur_col, top; /* cur_col -1: the step's number */
static int kbd_oct;                     /* octaves the keyboard is moved by */
static int chord_next;                  /* the note slot the next held key fills, 0 = none held */
static const char *said;
static uint32_t prev_btns, prev_keys;
static uint8_t fx_order[FX_SYL]; /* alphabetical, --- first */
static int fx_count;
static int play_row = -1;
/* CHOOSE INSTRUMENT: its rows are --, then the instruments. While it
 * listens, the one the cursor rests on SETTLE frames sounds the step's
 * note, struck again every AGAIN frames. */
enum { CH_TOP = 24, CH_ROWS = 7, CH_N = SONG_MAX_INSTR + 1, SETTLE = 12, AGAIN = 45 };
static bool choosing, listening;
static int ch_cur, ch_top, ch_orig, settle, again, sounding = -1;

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
    Step *st = step_at(row)->fx[slot].cmd != FX_NONE ? step_w(row) : 0;
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

static void draw_cell(int v, int r, int c, bool band)
{
    const Step *st = step_at(r);
    int w = view_w[view], x = GRID_CELL_X + c * w;
    bool on = band && c == cur_col;
    char s[4];

    if (view == V_NOTES) {
        uint8_t n = st->notes[c];
        grid_cell(v, x, w, n <= NOTE_MAX ? note_name(n, s) : n == NOTE_OFF ? "OFF" : "---", band, on,
                  n <= NOTE_MAX ? (r == play_row ? C_TITLE : C_TEXT) : C_DIM);
    } else if (view == V_FX) {
        StepFx f = st->fx[c];
        int tw, tx;
        if (f.cmd == FX_NONE) {
            grid_cell(v, x, w, "---", band, on, C_DIM);
            return;
        }
        if (on)
            grid_hollow(v, x, w);
        tw = gfx_text_w(F_COND20, fx_name(f.cmd)) + 4 + gfx_text_w(F_COND20, grid_hex2(s, f.val));
        tx = gfx_text(F_COND20, x + (w - tw) / 2, grid_base(v), fx_name(f.cmd), grid_ink(band, on, C_FX));
        gfx_text(F_COND20, tx + 4, grid_base(v), s, grid_ink(band, on, C_TEXT));
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

/* ------------------------------------------------- choose instrument --- */
static int ch_instr(int k) { return k == 0 ? edit_instr_at(song, track, pat, cur_row) : k - 1; }

static void ch_sound(void)
{
    const Step *st = step_at(cur_row);
    player_note_on(ch_instr(ch_cur), st->notes[0] <= NOTE_MAX ? st->notes[0] : 60);
    sounding = ch_cur;
    again = AGAIN;
}

static void ch_hush(void)
{
    if (sounding >= 0)
        player_note_off();
    sounding = -1;
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

static void ch_open(void)
{
    const Step *st = step_at(cur_row);
    ch_orig = st->instrument == INSTR_NONE ? -1 : st->instrument;
    ch_cur = ch_orig + 1;
    ch_top = 0;
    listening = false;
    settle = 0;
    sounding = -1;
    grid_follow(&ch_cur, &ch_top, CH_N, CH_ROWS);
    choosing = true;
}

static void ch_frame(const PlatInput *in, uint32_t btns)
{
#define PRESSED(b) ((btns >> (b)) & 1u)
    ch_cur += in->enc[PE_SELECT];
    grid_follow(&ch_cur, &ch_top, CH_N, CH_ROWS);
    if (in->enc[PE_SELECT])
        settle = SETTLE;
    if (PRESSED(PB_PLAY)) {
        listening = !listening;
        if (listening)
            ch_sound();
        else
            ch_hush();
    } else if (listening && settle > 0 && --settle == 0 && ch_cur != sounding) {
        ch_sound();
    } else if (listening && sounding >= 0 && --again == 0) {
        ch_sound();
    }
    if (PRESSED(PB_SEL))
        ch_close(true);
    else if (PRESSED(PB_GLO))
        ch_close(false);
    plat_led_btn(PB_PLAY, listening);
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
        bool here = k == ch_cur;
        const char *name = song->instruments[ch_instr(k)].name;
        uint16_t ink = here ? C_ON_ACC : k == 0 || !name[0] ? C_DIM : C_TEXT;
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 14, GRID_ROW - 2, C_ACCENT, 255);
        if (k - 1 == ch_orig) /* what the step says now, as HOME marks the open project */
            gfx_fill(6, y + 6, 3, GRID_ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
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
    ch_hint(6, "SEL", "TAKE", false);
    ch_hint(96, "PLAY", "LISTEN", false);
    ch_hint(PLAT_W - 6, "GLO", "BACK", true);
}

static void draw(void)
{
    int len = pat_len(), cols = view_cols[view], w = view_w[view];
    char s[8];

    grid_clear();
    s[0] = 'T', s[1] = (char)('1' + track), s[2] = 0;
    if (said)
        grid_corner(said, said[0] == 'S' ? C_ACCENT : C_MUTE, 0, 0);
    else if (cur_col < 0) /* the block's length, while the cursor is on a number */
        grid_corner("LEN", C_DIM, ui_dec(s + 3, len), C_TEXT);
    else
        grid_corner(s, C_TITLE, view_name[view], C_TEXT);
    for (int c = 0; c < cols; c++) {
        char h[6] = { 0 };
        if (view == V_NOTES)
            h[0] = (char)('1' + c);
        else if (view == V_FX)
            h[0] = 'F', h[1] = 'X', h[2] = ' ', h[3] = (char)('1' + c);
        grid_head(GRID_CELL_X + c * w, w, view == V_INS ? "INSTRUMENT" : h, c == cur_col);
    }
    for (int v = 0; v < GRID_ROWS && top + v < len; v++) {
        int r = top + v;
        bool band = r == cur_row;
        if (band)
            grid_band(v, PLAT_W - 4 - (len > GRID_ROWS ? 6 : 0));
        if (r == play_row)
            grid_playing(v, band);
        grid_num(v, r, band, band && cur_col < 0, r == play_row ? C_TITLE : r % beat() ? C_DIM : C_TEXT);
        for (int c = 0; c < cols; c++)
            draw_cell(v, r, c, band);
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
    cur_row = top = cur_col = 0;
    view = V_NOTES;
    said = 0;
    choosing = listening = false;
    sounding = -1;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    /* KNOB 3's order: alphabetical, the empty slot first. */
    fx_count = 0;
    fx_order[fx_count++] = FX_NONE;
    for (int c = 1; c < FX_SYL; c++) {
        int i = fx_count++;
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
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    prev_keys = in->keys;
    if (btns || keys || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3])
        said = 0;

    if (choosing) {
        ch_frame(in, btns);
        gfx_frame(choosing ? ch_draw : draw);
        return PATTERN_STAY;
    }
    if (PRESSED(PB_ENV) && view == V_INS) { /* ENV twice: from the column into the instrument */
        player_note_off();
        return PATTERN_INSTR;
    }
    if (PRESSED(PB_SEQ) || PRESSED(PB_FX) || PRESSED(PB_ENV)) { /* a button a view */
        view = PRESSED(PB_SEQ) ? V_NOTES : PRESSED(PB_FX) ? V_FX : V_INS;
        if (cur_col >= 0)
            cur_col = 0;
    }
    if (PRESSED(PB_SEL) && view == V_INS && cur_col >= 0)
        ch_open();
    cur_row += in->enc[PE_SELECT];
    cur_row = cur_row < 0 ? 0 : cur_row >= len ? len - 1 : cur_row;
    cur_col += in->enc[PE_K1];
    cur_col = cur_col < -1 ? -1 : cur_col >= view_cols[view] ? view_cols[view] - 1 : cur_col;
    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;

    if ((d = in->enc[PE_K2]) != 0) {
        if (cur_col < 0)
            turn_len(d);
        else if (view == V_NOTES && !edit_turn_note(song, track, pat, cur_row, cur_col, d))
            said = "POOL FULL";
        else if (view == V_FX)
            turn_fx_val(cur_row, cur_col, d);
        else if (view == V_INS)
            turn_instr(cur_row, d);
    }
    if ((d = in->enc[PE_K3]) != 0 && view == V_FX && cur_col >= 0)
        turn_fx_cmd(cur_row, cur_col, d);

    /* The keys write notes: the first one in the slot under the cursor,
     * the ones pressed while it is held in the slots after it. */
    if (!in->keys) {
        if (chord_next)
            player_note_off();
        chord_next = 0;
    }
    for (int k = 0; k < PLAT_KEYS; k++) {
        int n = 53 + kbd_oct * 12 + k, slot;
        if (!((keys >> k) & 1u) || n < NOTE_MIN || n > NOTE_MAX)
            continue;
        slot = chord_next ? chord_next : view == V_NOTES && cur_col > 0 ? cur_col : 0;
        if (slot >= STEP_MAX_NOTES)
            continue;
        if (!edit_write_note(song, track, pat, cur_row, slot, (uint8_t)n))
            said = "POOL FULL";
        player_note_on(edit_instr_at(song, track, pat, cur_row), (uint8_t)n); /* heard as written */
        chord_next = slot + 1;
    }

    if (PRESSED(PB_SAVE))
        said = app_save() == 0 ? "SAVED" : "NOT SAVED";
    if (PRESSED(PB_ARP) && undo_pop()) {
        /* what was taken back may be older than this screen: the block
         * the phrase names here, or the track itself */
        pat = track < song->num_tracks ? song->arrangement[entry].pattern[track] : PATTERN_NONE;
        if (pat >= TRACK_MAX_PATTERNS) {
            player_note_off();
            return PATTERN_SONG; /* nothing left to show */
        }
    }
    if (PRESSED(PB_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_entry(entry);
    }

    len = pat_len();
    grid_follow(&cur_row, &top, len, GRID_ROWS);
    play_row = playing && player_entry() == entry ? player_row() % len : -1;
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(choosing ? ch_draw : draw);
    if (PRESSED(PB_GLO)) {
        player_note_off();
        song_pool_compact(song);
        return PATTERN_BACK;
    }
    return PATTERN_STAY;
#undef PRESSED
}
