/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern.h"

#include "../core/player.h"
#include "app.h"
#include "edit.h"
#include "gfx.h"
#include "loop.h"
#include "ui.h"
#include "undo.h"

/* The measures of proto/pattern.html, in panel pixels. */
enum {
    NAV_Y = 2, NAV_H = 17,
    CARDS_Y = 22, CARDS_H = 22,
    LEFT_Y = 49,
    HEAD_Y = 59,
    ROWS_Y = 68, ROW_H = 12, ROW_PITCH = 13, ROWS = 11,
    BTN_Y = 215, BTN_H = 10,
    X0 = 3, INNER = 234,
};
/* Field 0 is the step number, 1..4 the notes, 5 the instrument, 6..8 the fx. */
enum { COL_STEP, COL_N1, COL_N4 = 4, COL_IN, COL_FX1, COL_FX3 = 8, COLS };
static const uint8_t col_x[COLS] = { 9, 23, 45, 67, 89, 116, 137, 168, 199 };
static const uint8_t col_w[COLS] = { 12, 20, 20, 20, 20, 14, 29, 29, 29 };

/* The twelve buttons, in the two rows of six they sit in under the knobs. */
enum { B_FX, B_LOOP, B_INSTR, B_CLEAR, B_SELECT, B_MIXER,
       B_BACK, B_SAVE, B_UNDO, B_SONG, B_PLAY, B_REC, BTNS };
static const uint8_t btn_of[BTNS] = { PB_FX, PB_SEL, PB_ENV, PB_LFO, PB_EDIT, PB_GLO,
                                      PB_HOME, PB_SAVE, PB_ARP, PB_SEQ, PB_PLAY, PB_REC };
/* What this build does not do yet is drawn dim: a button keeps its place
 * and says it does nothing. */
static const bool btn_live[BTNS] = { true, true, true, true, true, true,
                                     true, true, true, true, true, false };

static Song *song;
static int entry, track, pat;
static int cur_row, cur_col, top;
static int kbd_oct;        /* octaves the keyboard is moved by */
static int chord_next;     /* the note slot the next held key fills, 0 = none held */
static bool pool_full;
static int8_t saved;       /* 0 nothing to say, 1 saved, -1 the save failed */
/* SELECT is a mode: the cell it was pressed on is one corner, the cursor the other. */
static bool selecting;
static int anchor_row, anchor_col;
/* The list of effects, beside the slot, while KNOB 1 turns on it and a moment after. */
enum { POP_MS = 1500, POP_ROWS = 6, POP_W = 124 };
static bool pop_on;
static uint32_t pop_until;
static const char *back_word = "\xE2\x80\xB9 SONG"; /* where HOME leads: SONG, or the PHRASE it came from */
static uint32_t prev_btns, prev_keys;
static uint8_t fx_order[FX_SYL]; /* alphabetical, --- first */
static int fx_count;

static bool fx_is_pair(uint8_t cmd)
{
    return cmd == FX_VIB || cmd == FX_TRM || cmd == FX_SCP;
}

/* ----------------------------------------------------------- data --- */
static int pat_len(void) { return song->patterns[track][pat].length; }
static const Step *step_at(int row) { return song_step(song, track, pat, row); }

static Step *step_w(int row)
{
    Step *st;
    undo_step(track, pat, row);
    st = song_step_w(song, track, pat, row);
    pool_full = st == 0;
    if (st)
        app_mark_dirty();
    return st;
}

static void write_note(int row, int k, uint8_t note)
{
    pool_full = !edit_write_note(song, track, pat, row, k, note);
}

static void turn_note(int row, int k, int d)
{
    pool_full = !edit_turn_note(song, track, pat, row, k, d);
}

static void turn_octave(int row, int k, int d)
{
    pool_full = !edit_turn_octave(song, track, pat, row, k, d);
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

/* KNOB 1 on an fx: the commands in alphabetical order, --- first. One
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
                           : fx_clamp_val(st->fx[slot].cmd, song->ticks_per_step,
                                          st->fx[slot].val);
}

/* part 0: the whole value. 1 and 2: the high and the low nibble of a pair. */
static void turn_fx_val(int row, int slot, int part, int d)
{
    Step *st = step_at(row)->fx[slot].cmd != FX_NONE ? step_w(row) : 0;
    int v, hi, lo;
    if (!st)
        return;
    v = st->fx[slot].val;
    hi = v >> 4;
    lo = v & 15;
    if (part == 1)
        hi += d;
    else if (part == 2)
        lo += d;
    else
        v += d;
    if (part) {
        hi = hi < 0 ? 0 : hi > 15 ? 15 : hi;
        lo = lo < 0 ? 0 : lo > 15 ? 15 : lo;
        v = hi << 4 | lo;
    }
    st->fx[slot].val = fx_clamp_val(st->fx[slot].cmd, song->ticks_per_step, v);
}

static void clear_slot(int row, int col)
{
    Step *st;
    if (col == COL_STEP) {
        pool_full = !edit_clear_step(song, track, pat, row);
        return;
    }
    if (!(st = step_is_empty(step_at(row)) ? 0 : step_w(row)))
        return;
    if (col <= COL_N4) {
        st->notes[col - COL_N1] = NOTE_NONE;
    } else if (col == COL_IN) {
        st->instrument = INSTR_NONE;
    } else {
        st->fx[col - COL_FX1].cmd = st->fx[col - COL_FX1].val = 0;
    }
}

/* PRESETS: the blocks of this track that hold something or are played
 * somewhere, in order, wrapping. It is the PHRASE's cell that changes:
 * the phrase plays what the screen shows. */
static void step_block(int d)
{
    for (int guard = 0; guard < TRACK_MAX_PATTERNS; guard++) {
        bool used = false;
        pat = (pat + (d < 0 ? TRACK_MAX_PATTERNS - 1 : 1)) % TRACK_MAX_PATTERNS;
        for (int i = 0; i < song->arrange_len; i++)
            used |= song->arrangement[i].pattern[track] == pat;
        if (used || !song_pattern_blank(song, track, pat))
            break;
    }
    undo_bytes(&song->arrangement[entry].pattern[track], 1);
    song->arrangement[entry].pattern[track] = (uint8_t)pat;
    app_mark_dirty();
    if (cur_row >= pat_len())
        cur_row = pat_len() - 1;
}

/* ------------------------------------------------------- selection --- */
static int sel_r0(void) { return cur_row < anchor_row ? cur_row : anchor_row; }
static int sel_r1(void) { return cur_row < anchor_row ? anchor_row : cur_row; }
static int sel_c0(void) { return cur_col < anchor_col ? cur_col : anchor_col; }
static int sel_c1(void) { return cur_col < anchor_col ? anchor_col : cur_col; }
static bool picked(int r, int c)
{
    return selecting && r >= sel_r0() && r <= sel_r1() && c >= sel_c0() && c <= sel_c1();
}

/* The fields the rectangle covers. The step's number is the whole step. */
static int sel_mask(void)
{
    int m = 0;
    if (sel_c0() == COL_STEP)
        return EDIT_CLIP_ALL;
    for (int c = sel_c0(); c <= sel_c1(); c++)
        m |= 1 << (c - COL_N1);
    return m;
}

/* --------------------------------------------------------- drawing --- */
static int play_row = -1;
static uint32_t held_btns;
static bool is_playing;

static void slot_bg(int x, int y, int w, bool full, bool cur, bool pick, uint16_t tint, int a)
{
    if (cur)
        gfx_slab(x, y, w, 10, C_ACCENT, 255);
    else if (pick)
        gfx_slab(x, y, w, 10, C_SEL, 255); /* taken: amber */
    else if (full)
        gfx_slab(x, y, w, 10, tint, a);
    else
        gfx_frame_rect(x, y, w, 10, C_DIM, 76);
}

static void draw_cards(void)
{
    const Step *st = step_at(cur_row);
    char a[8], b[8];

    if (selecting) { /* in the mode one takes, one does not write */
        for (int k = 0; k < 4; k++)
            ui_card(k, 0, 0, 0);
        return;
    }
    if (cur_col == COL_STEP) {
        ui_card(0, "LEN", ui_dec(a, pat_len()), "steps");
        ui_card(1, 0, 0, 0);
        ui_card(2, 0, 0, 0);
    } else if (cur_col <= COL_N4) {
        uint8_t n = st->notes[cur_col - COL_N1];
        ui_card(0, "NOTE", n <= NOTE_MAX ? note_name(n, a) : n == NOTE_OFF ? "OFF" : "---", 0);
        if (n <= NOTE_MAX)
            ui_card(1, "OCT", ui_dec(b, n / 12 - 1), 0);
        else
            ui_card(1, 0, 0, 0);
        ui_card(2, 0, 0, 0);
    } else if (cur_col == COL_IN) {
        bool has = st->instrument != INSTR_NONE;
        ui_card(0, "INSTR", has ? ui_hex2(a, st->instrument) : "--",
             has ? song->instruments[st->instrument % SONG_MAX_INSTR].name : 0);
        ui_card(1, 0, 0, 0);
        ui_card(2, 0, 0, 0);
    } else {
        StepFx f = st->fx[cur_col - COL_FX1];
        ui_card(0, "EFFECT", fx_name(f.cmd), 0);
        if (f.cmd == FX_NONE) {
            ui_card(1, 0, 0, 0);
            ui_card(2, 0, 0, 0);
        } else if (fx_is_pair(f.cmd)) {
            a[0] = "0123456789ABCDEF"[f.val >> 4];
            b[0] = "0123456789ABCDEF"[f.val & 15];
            a[1] = b[1] = '\0';
            ui_card(1, "SPEED", a, 0);
            ui_card(2, f.cmd == FX_SCP ? "SEMIS" : "DEPTH", b, 0);
        } else {
            ui_card(1, "VALUE", ui_hex2(a, f.val), 0);
            ui_card(2, 0, 0, 0);
        }
    }
    ui_card(3, 0, 0, 0);
}

static void draw_row(int r, int y)
{
    const Step *st = step_at(r);
    bool playing = r == play_row;
    char t[4];

    if (playing) {
        gfx_slab(X0, y, INNER, ROW_H, C_PLAY, 255);
        gfx_fill(X0 + 1, y, 3, ROW_H, C_ACCENT, 255);
    } else {
        gfx_slab(X0, y, INNER, ROW_H, C_TEXT, r % 16 == 0 ? 43 : r % 4 == 0 ? 26 : 11);
        if (loop_has(r))
            gfx_fill(X0 + 1, y, 3, ROW_H, C_TITLE, 255); /* the gutter: what goes round */
    }
    if (cur_row == r && cur_col == COL_STEP)
        gfx_slab(col_x[0], y + 1, col_w[0], 10, C_ACCENT, 255);
    else if (picked(r, COL_STEP))
        gfx_slab(col_x[0], y + 1, col_w[0], 10, C_SEL, 255);
    gfx_text_mid(F_MONO8, col_x[0], col_w[0], y + 9, ui_hex2(t, r),
                 cur_row == r && cur_col == COL_STEP ? C_ON_ACC : picked(r, COL_STEP) ? C_TEXT
                 : loop_has(r) ? C_TITLE
                 : r % 16 == 0 ? C_TEXT : r % 4 == 0 ? C_BEAT : C_DIM);
    for (int k = 0; k < STEP_MAX_NOTES; k++) {
        int c = COL_N1 + k;
        uint8_t n = st->notes[k];
        bool cur = cur_row == r && cur_col == c, full = n != NOTE_NONE;
        slot_bg(col_x[c], y + 1, col_w[c], full, cur, picked(r, c), C_TEXT, 18);
        if (full)
            gfx_text_mid(F_COND11, col_x[c], col_w[c], y + 10,
                         n == NOTE_OFF ? "OFF" : note_name(n, t), cur ? C_ON_ACC : C_TEXT);
    }
    {
        bool cur = cur_row == r && cur_col == COL_IN, full = st->instrument != INSTR_NONE;
        slot_bg(col_x[COL_IN], y + 1, col_w[COL_IN], full, cur, picked(r, COL_IN), C_TITLE, 26);
        if (full)
            gfx_text_mid(F_MONO8, col_x[COL_IN], col_w[COL_IN], y + 9,
                         ui_hex2(t, st->instrument), cur ? C_ON_ACC : C_TITLE);
    }
    for (int k = 0; k < STEP_MAX_FX; k++) {
        int c = COL_FX1 + k, x = col_x[c];
        StepFx f = st->fx[k];
        bool cur = cur_row == r && cur_col == c, full = f.cmd != FX_NONE;
        slot_bg(x, y + 1, col_w[c], full, cur, picked(r, c), C_FX, 28);
        if (full) {
            int w = gfx_text_w(F_COND9, fx_name(f.cmd)) + 2 + gfx_text_w(F_MONO8, ui_hex2(t, f.val));
            int tx = gfx_text(F_COND9, x + (col_w[c] - w) / 2, y + 9, fx_name(f.cmd),
                              cur ? C_ON_ACC : C_FX);
            gfx_text(F_MONO8, tx + 2, y + 9, t, cur ? C_ON_ACC : C_TEXT);
        }
    }
}

/* The list K1 walks, around where it is: the mnemonic and what it does.
 * To the LEFT of the slot, so the value being set stays in sight. */
static void draw_pop(void)
{
    static const char *const slot_name[3] = { "FX1", "FX2", "FX3" };
    int k = cur_col - COL_FX1, at = 0, first, x, y, h = 12 + POP_ROWS * 12 + 2;
    uint8_t cmd = step_at(cur_row)->fx[k].cmd;

    for (int i = 0; i < fx_count; i++)
        if (fx_order[i] == cmd)
            at = i;
    first = at - 2 < 0 ? 0 : at - 2;
    if (first > fx_count - POP_ROWS)
        first = fx_count - POP_ROWS;
    x = col_x[cur_col] - 3 - POP_W;
    y = ROWS_Y + (cur_row - top) * ROW_PITCH + ROW_H / 2 - h / 2;
    y = y < HEAD_Y ? HEAD_Y : y + h > BTN_Y - 2 ? BTN_Y - 2 - h : y;

    gfx_fill(x - 1, y - 1, POP_W + 2, h + 2, C_BG, 255);
    gfx_slab(x, y, POP_W, h, GFX_RGB(0x1A, 0x20, 0x29), 255);
    gfx_frame_rect(x, y, POP_W, h, C_DIM, 115);
    gfx_text(F_MONO8, gfx_text(F_MONO8, x + 4, y + 9, "K1", C_ACCENT) + 3, y + 9, "EFFECT", C_DIM);
    gfx_text(F_MONO8, x + POP_W - 4 - gfx_text_w(F_MONO8, slot_name[k]), y + 9, slot_name[k], C_DIM);
    for (int i = 0; i < POP_ROWS; i++) {
        uint8_t c = fx_order[first + i];
        int ry = y + 12 + i * 12;
        bool cur = first + i == at;
        gfx_slab(x + 2, ry, POP_W - 4, 11, cur ? C_ACCENT : C_TEXT, cur ? 255 : 11);
        gfx_text(F_COND9, x + 5, ry + 9, fx_name(c), cur ? C_ON_ACC : C_FX);
        gfx_text(F_MONO8, x + 25, ry + 9, fx_desc(c), cur ? C_ON_ACC : C_DIM);
    }
}

static void draw(void)
{
    static const char *const head[COLS] = { "#", "N1", "N2", "N3", "N4", "IN", "FX1", "FX2", "FX3" };
    static const char names[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    char t[12], k[12];
    int x, len = pat_len();

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);

    /* The bar: where one is, and the way out. */
    /* The back names where it leads, and the phrase is said beside the track. */
    x = gfx_text(F_COND12, X0, NAV_Y + 13, back_word, C_TITLE);
    t[0] = 'P';
    ui_hex2(t + 1, entry);
    x = gfx_text(F_COND12, x + 6, NAV_Y + 13, t, C_DIM);
    k[0] = 'T';
    ui_dec(k + 1, track + 1);
    x = gfx_text(F_COND12, x + 6, NAV_Y + 13, k, C_TEXT);
    gfx_slab(x + 6, NAV_Y + 3, 40, 12, C_TEXT, 18);
    gfx_text(F_COND12, gfx_text(F_MONO8, x + 10, NAV_Y + 12, "LEN", C_DIM) + 3, NAV_Y + 13,
             ui_dec(k, len), C_TEXT);
    gfx_slab(X0 + INNER - 36, NAV_Y + 3, 36, 12, C_TEXT, 18);
    gfx_text(F_COND12, gfx_text(F_MONO8, X0 + INNER - 32, NAV_Y + 12, "PAT", C_DIM) + 3,
             NAV_Y + 13, ui_hex2(k, pat), C_TEXT);
    if (pool_full)
        gfx_text(F_COND9, x + 52, NAV_Y + 12, "POOL FULL", C_MUTE);
    else if (saved)
        gfx_text(F_COND9, x + 52, NAV_Y + 12, saved > 0 ? "SAVED" : "NOT SAVED",
                 saved > 0 ? C_ACCENT : C_MUTE);
    else if (selecting) { /* fields x steps */
        int tx;
        ui_dec(k, sel_c0() == COL_STEP ? COLS - 1 : sel_c1() - sel_c0() + 1);
        for (tx = 0; k[tx]; tx++)
            ;
        k[tx] = 'x';
        ui_dec(k + tx + 1, sel_r1() - sel_r0() + 1);
        gfx_slab(x + 48, NAV_Y + 3, 52, 12, C_FX, 40);
        tx = gfx_text(F_MONO8, x + 52, NAV_Y + 12, "SEL", C_FX);
        gfx_text(F_COND12, tx + 3, NAV_Y + 13, k, C_FX);
    } else
        ui_loop_slot(x + 48, NAV_Y + 3);
    gfx_fill(X0, NAV_Y + NAV_H, INNER, 1, C_DIM, 77);

    draw_cards();
    ui_cards_rule();

    /* The left hand's manual: one line, always. */
    x = gfx_text(F_MONO8, X0, LEFT_Y + 8, "SELECT", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, selecting ? "STEPS" : "STEP", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "ALGO", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, selecting ? "FIELDS" : "FIELD", C_TEXT);
    if (!selecting) {
        x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "PRESETS", C_DIM);
        x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "PAT", C_TEXT);
        x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "OCT", C_DIM);
    }
    if (!selecting) {   /* where the 27 keys are: F3..G5 unmoved */
        int lo = 53 + kbd_oct * 12, hi = lo + 26;
        k[0] = names[lo % 12 * 2], k[1] = (char)('0' + lo / 12 - 1), k[2] = '-';
        k[3] = names[hi % 12 * 2], k[4] = (char)('0' + hi / 12 - 1), k[5] = '\0';
        gfx_text(F_MONO8, x + 3, LEFT_Y + 8, k, C_TEXT);
    }

    for (int c = 0; c < COLS; c++)
        gfx_text_mid(F_MONO8, col_x[c], col_w[c], HEAD_Y + 7, head[c],
                     c == cur_col || (selecting && c >= sel_c0() && c <= sel_c1()) ? C_ACCENT : C_DIM);

    for (int i = 0; i < ROWS && top + i < len; i++)
        draw_row(top + i, ROWS_Y + i * ROW_PITCH);
    {   /* the two seams: notes | instrument | fx are three different things */
        int n = len - top < ROWS ? len - top : ROWS;
        gfx_dash_v(112, HEAD_Y, ROWS_Y - HEAD_Y + n * ROW_PITCH - 1, C_TITLE, 90);
        gfx_dash_v(133, HEAD_Y, ROWS_Y - HEAD_Y + n * ROW_PITCH - 1, C_TITLE, 90);
    }

    if (pop_on)
        draw_pop();

    {   /* The twelve buttons, where they are under the hand. What this
         * build does not do yet is OFF: named, and it says it does nothing. */
        const char *words[12] = { "FX", selecting ? "COPY" : loop_state() == LOOP_ONE_END ? "END" : "LOOP",
                                  selecting ? "PASTE" : "INSTR", "CLEAR", "SELECT", "MIXER",
                                  back_word, "SAVE", "UNDO", "SONG", is_playing ? "STOP" : "PLAY", "REC" };
        uint8_t st[12];
        for (int b = 0; b < BTNS; b++)
            st[b] = !btn_live[b] || (b == B_UNDO && !undo_can()) ? UI_BTN_OFF : b == B_BACK ? UI_BTN_BACK
                    : b == B_PLAY && is_playing ? UI_BTN_LIVE
                    : b == B_LOOP && !selecting && loop_state() != LOOP_NONE ? UI_BTN_HERE
                    : b == B_SELECT && selecting ? UI_BTN_HERE
                    : b == B_FX && selecting ? UI_BTN_NONE
                    : b == B_INSTR && selecting && !edit_clip_len() ? UI_BTN_OFF : UI_BTN_ON;
        ui_btn_map(words, st, held_btns);
    }
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
    cur_col = COL_N1;
    selecting = false;
    pop_on = false;
    back_word = "\xE2\x80\xB9 SONG";
    pool_full = false;
    saved = 0;
    loop_keep(entry);
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    /* The picker's order: alphabetical, the empty slot first. */
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

static int instr_at(int row) { return edit_instr_at(song, track, pat, row); }

void pattern_from_phrase(int row)
{
    back_word = "\xE2\x80\xB9 PHR";
    cur_row = row < 0 ? 0 : row >= pat_len() ? pat_len() - 1 : row;
    top = cur_row >= ROWS ? cur_row - ROWS / 2 : 0;
}

int pattern_instr(void) { return instr_at(cur_row); }
int pattern_entry(void) { return entry; }

void pattern_resume(void)
{
    prev_btns = prev_keys = 0xFFFFFFFFu;
    saved = 0;
    gfx_invalidate();
}

int pattern_frame(const PlatInput *real)
{
    PlatInput masked = *real;
    const PlatInput *in = &masked;
    uint32_t btns = real->btns & ~prev_btns, keys;
    int len = pat_len(), d;
#define PRESSED(b) ((btns >> btn_of[b]) & 1u)

    if (selecting) { /* the right hand and the keys write nothing while one takes */
        masked.keys = 0;
        for (int k = PE_K1; k <= PE_K4; k++)
            masked.enc[k] = 0;
    }
    keys = in->keys & ~prev_keys;
    prev_btns = real->btns;
    prev_keys = real->keys;
    held_btns = in->btns;
    is_playing = player_playing();

    /* The list stays while KNOB 1 turns on an fx slot and a moment after; anything else closes it. */
    if (in->enc[PE_K1] && cur_col >= COL_FX1 && !in->enc[PE_SELECT] && !in->enc[PE_ALGO])
        pop_until = real->ms + POP_MS, pop_on = true;
    else if (btns || in->keys || in->enc[PE_SELECT] || in->enc[PE_ALGO] || in->enc[PE_PRESET] ||
             in->enc[PE_K2] || in->enc[PE_K3] || (int32_t)(real->ms - pop_until) >= 0)
        pop_on = false;

    /* The left hand moves. */
    cur_row += in->enc[PE_SELECT];
    cur_row = cur_row < 0 ? 0 : cur_row >= len ? len - 1 : cur_row;
    cur_col += in->enc[PE_ALGO];
    cur_col = cur_col < 0 ? 0 : cur_col >= COLS ? COLS - 1 : cur_col;
    if (in->enc[PE_PRESET]) {
        selecting = false; /* another block: what was taken was this one's */
        step_block(in->enc[PE_PRESET]);
    }
    if (PRESSED(B_SELECT)) {
        selecting = !selecting;
        anchor_row = cur_row;
        anchor_col = cur_col;
    } else if (selecting) {
        int r0 = sel_r0(), n = sel_r1() - r0 + 1;
        if (PRESSED(B_LOOP)) /* COPY: the rectangle stays, CLEAR after it is a cut */
            edit_clip_copy(song, track, pat, r0, n, sel_mask());
        if (PRESSED(B_INSTR) && edit_clip_len()) { /* PASTE: from the cursor down */
            pool_full = edit_clip_paste(song, track, pat, cur_row,
                                        cur_col >= COL_FX1 ? cur_col - COL_FX1 : -1) < 0;
            selecting = false;
        }
        if (PRESSED(B_CLEAR)) {
            pool_full = !edit_erase_range(song, track, pat, r0, n, sel_mask());
            selecting = false;
        }
        btns &= ~(1u << btn_of[B_FX] | 1u << btn_of[B_LOOP] | 1u << btn_of[B_INSTR] | 1u << btn_of[B_CLEAR]);
    }
    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;

    /* The right hand writes: KNOB 1-4 are the cards. */
    if ((d = in->enc[PE_K1]) != 0) {
        if (cur_col == COL_STEP) {
            int n = len + d;
            undo_bytes(&song->patterns[track][pat].length, sizeof(uint16_t));
            song->patterns[track][pat].length =
                (uint16_t)(n < 1 ? 1 : n > PATTERN_MAX_STEPS ? PATTERN_MAX_STEPS : n);
            app_mark_dirty();
        } else if (cur_col <= COL_N4) {
            turn_note(cur_row, cur_col - COL_N1, d);
        } else if (cur_col == COL_IN) {
            turn_instr(cur_row, d);
        } else {
            turn_fx_cmd(cur_row, cur_col - COL_FX1, d);
        }
    }
    if ((d = in->enc[PE_K2]) != 0) {
        if (cur_col >= COL_N1 && cur_col <= COL_N4)
            turn_octave(cur_row, cur_col - COL_N1, d);
        else if (cur_col >= COL_FX1)
            turn_fx_val(cur_row, cur_col - COL_FX1,
                        fx_is_pair(step_at(cur_row)->fx[cur_col - COL_FX1].cmd) ? 1 : 0, d);
    }
    if ((d = in->enc[PE_K3]) != 0 && cur_col >= COL_FX1 &&
        fx_is_pair(step_at(cur_row)->fx[cur_col - COL_FX1].cmd))
        turn_fx_val(cur_row, cur_col - COL_FX1, 2, d);

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
        slot = chord_next ? chord_next
               : cur_col >= COL_N1 && cur_col <= COL_N4 ? cur_col - COL_N1 : 0;
        if (slot >= STEP_MAX_NOTES)
            continue;
        write_note(cur_row, slot, (uint8_t)n);
        player_note_on(instr_at(cur_row), (uint8_t)n); /* what was just written is heard */
        chord_next = slot + 1;
    }

    /* SAVED stays up until the hand does something else. */
    if (btns || keys || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3])
        saved = 0;
    if (PRESSED(B_SAVE))
        saved = app_save() == 0 ? 1 : -1;
    if (PRESSED(B_CLEAR))
        clear_slot(cur_row, cur_col);
    if (PRESSED(B_LOOP))
        loop_press(entry, cur_row); /* a block's steps are those of its first pass in the phrase */
    if (PRESSED(B_UNDO) && undo_pop()) {
        /* what was taken back may be older than this screen: the block
         * the phrase names here, or the track itself */
        pat = track < song->num_tracks ? song->arrangement[entry].pattern[track] : PATTERN_NONE;
        if (pat >= TRACK_MAX_PATTERNS) {
            player_note_off();
            return PATTERN_SONG; /* nothing left to show */
        }
        pool_full = false;
    }
    if (PRESSED(B_FX)) {
        /* to the first free fx slot of the step, else the first */
        const Step *st = step_at(cur_row);
        cur_col = COL_FX1;
        for (int i = STEP_MAX_FX - 1; i >= 0; i--)
            if (st->fx[i].cmd == FX_NONE)
                cur_col = COL_FX1 + i;
    }
    if (PRESSED(B_PLAY)) {
        if (is_playing)
            player_stop();
        else
            player_play_entry(entry);
    }

    len = pat_len();
    if (cur_row >= len)
        cur_row = len - 1;
    if (anchor_row >= len)
        anchor_row = len - 1;
    if (cur_row < top)
        top = cur_row;
    if (cur_row >= top + ROWS)
        top = cur_row - ROWS + 1;
    play_row = is_playing && player_entry() == entry ? player_row() % len : -1;
    gfx_frame(draw);
    if (PRESSED(B_BACK) || PRESSED(B_SONG)) {
        player_note_off();
        song_pool_compact(song);
        selecting = false;
        return PRESSED(B_SONG) ? PATTERN_SONG : PATTERN_BACK;
    }
    if (PRESSED(B_INSTR)) {
        player_note_off();
        return PATTERN_INSTR;
    }
    if (PRESSED(B_MIXER)) {
        player_note_off();
        selecting = false;
        return PATTERN_MIXER;
    }
    return PATTERN_STAY;
#undef PRESSED
}
