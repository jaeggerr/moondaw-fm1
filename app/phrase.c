/* SPDX-License-Identifier: GPL-3.0-only */
#include "phrase.h"

#include "../core/player.h"
#include "app.h"
#include "edit.h"
#include "gfx.h"
#include "ui.h"
#include "undo.h"

enum {
    NAV_Y = 2, NAV_H = 17,
    LEFT_Y = 49,
    HEAD_Y = 60, HEAD_H = 19,
    ROWS_Y = 80, ROW_H = 12, ROW_PITCH = 13, ROWS = 10,
    POS_X = 9, POS_W = 15, CELL_X = 26, CELL_W = 23, CELL_PITCH = 25,
};
enum { AT_LOOP = 1, AT_MUTE, AT_CLEAR, AT_OPEN, AT_MIXER,
       AT_BACK, AT_SAVE, AT_UNDO, AT_PLAY = 10, AT_REC };

#define song (&app_song)

/* row -1 is the column heads; col a track. */
static int entry, row, col, top, steps;
static int kbd_oct;    /* octaves the keyboard is moved by */
static int chord_next; /* the note slot the next held key fills, 0 = none held */
static bool pool_full;
static bool follow;    /* the page goes with the playhead until the hand moves SELECT */
static int8_t saved;
static uint32_t prev_btns, prev_keys, held;
static bool playing;
static int play_row = -1;

/* ------------------------------------------------------------- data --- */
static int blk(int t) { return song->arrangement[entry].pattern[t]; }
static bool has(int t) { return blk(t) < TRACK_MAX_PATTERNS; }
/* A block shorter than the phrase repeats: the grid shows what plays,
 * and writing on a row writes the step of the block that plays there. */
static int prow(int t, int r) { return r % song->patterns[t][blk(t)].length; }
static const Step *cell(int t, int r) { return song_step(song, t, blk(t), prow(t, r)); }
static bool on_cell(void) { return row >= 0 && has(col); }

static void measure(void)
{
    steps = song_entry_steps(song, entry);
    if (steps < 1)
        steps = 1;
    if (row >= steps)
        row = steps - 1;
}

/* A track this phrase leaves out draws nothing and the cursor does not
 * stop on it. */
static void move_col(int d)
{
    for (int n = d < 0 ? -d : d; n > 0; n--) {
        int c = col;
        do
            c += d < 0 ? -1 : 1;
        while (c >= 0 && c < song->num_tracks && !has(c));
        if (c < 0 || c >= song->num_tracks)
            return;
        col = c;
    }
}

static void settle_col(void)
{
    if (col >= song->num_tracks)
        col = song->num_tracks - 1;
    for (int k = 0; k < song->num_tracks && !has(col); k++) {
        if (col + k < song->num_tracks && has(col + k))
            col += k;
        else if (col - k >= 0 && has(col - k))
            col -= k;
    }
}

/* PRESETS: the phrase before or after, the cursor where it was. A
 * reference has no notes of its own and is stepped over. */
static void step_phrase(int d)
{
    for (int n = d < 0 ? -d : d; n > 0; n--) {
        int e = entry;
        do
            e += d < 0 ? -1 : 1;
        while (e >= 0 && e < song->arrange_len && song->arrangement[e].kind == ENTRY_REF);
        if (e < 0 || e >= song->arrange_len)
            break;
        entry = e;
    }
    measure();
    settle_col();
}

/* ---------------------------------------------------------- drawing --- */
static void draw_cell(int t, int r, int y, bool muted)
{
    const Step *st = cell(t, r);
    int x = CELL_X + t * CELL_PITCH, first = -1, more = 0;
    bool cur = row == r && col == t;
    uint16_t ink = cur ? C_ON_ACC : muted ? C_DIM : C_TEXT;
    char n[4];

    if (cur)
        gfx_slab(x, y + 1, CELL_W, 10, C_ACCENT, 255);
    for (int k = 0; k < STEP_MAX_NOTES; k++)
        if (st->notes[k] <= NOTE_MAX) {
            if (first < 0)
                first = k;
            else
                more++;
        }
    if (first >= 0) {
        /* the first note, and a dot for each other note of the chord */
        int w = gfx_text_w(F_MONO8, note_name(st->notes[first], n)) + (more ? 4 : 0);
        int tx = gfx_text(F_MONO8, x + (CELL_W - w) / 2, y + 9, n, ink);
        for (int i = 0; i < more; i++)
            gfx_fill(tx + 2, y + 1 + (10 - (more * 3 - 1)) / 2 + i * 3, 2, 2,
                     cur ? C_ON_ACC : muted ? C_DIM : C_TITLE, 255);
    } else if (st->notes[0] == NOTE_OFF) {
        gfx_text_mid(F_MONO8, x, CELL_W, y + 9, "OFF", cur ? C_ON_ACC : C_DIM);
    } else {
        /* no note: the step's first fx says something happens here */
        for (int k = 0; k < STEP_MAX_FX; k++)
            if (st->fx[k].cmd != FX_NONE) {
                gfx_text_mid(F_COND9, x, CELL_W, y + 9, fx_name(st->fx[k].cmd),
                             cur ? C_ON_ACC : muted ? C_DIM : C_FX);
                break;
            }
    }
}

static void draw(void)
{
    static const char names[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    const char *w[12] = { "", "LOOP", "MUTE", "CLEAR", "OPEN", "MIXER",
                          "\xE2\x80\xB9 SONG", "SAVE", "UNDO", "", playing ? "STOP" : "PLAY", "REC" };
    uint8_t bs[12];
    char a[12], b[12];
    int x;

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);

    /* The bar: where one is, and the way out. */
    x = gfx_text(F_COND12, APP_X0, NAV_Y + 13, w[AT_BACK], C_TITLE);
    x = gfx_text(F_COND12, x + 6, NAV_Y + 13, app_name, C_TEXT);
    gfx_slab(x + 6, NAV_Y + 3, 36, 12, C_TEXT, 18);
    gfx_text(F_COND12, gfx_text(F_MONO8, x + 10, NAV_Y + 12, "PHR", C_DIM) + 3, NAV_Y + 13,
             ui_hex2(a, entry), C_TEXT);
    x += 46;
    {
        const char *m = pool_full ? "POOL FULL" : saved > 0 ? "SAVED" : saved < 0 ? "NOT SAVED" : 0;
        int right = APP_X0 + APP_INNER;
        if (m) {
            right -= gfx_text_w(F_COND9, m);
            gfx_text(F_COND9, right, NAV_Y + 12, m, saved > 0 && !pool_full ? C_ACCENT : C_MUTE);
        } else {
            right -= gfx_text_w(F_MONO8, "STEPS");
            gfx_text(F_MONO8, right, NAV_Y + 12, "STEPS", C_DIM);
            right -= gfx_text_w(F_MONO8, ui_dec(a, steps)) + 2;
            gfx_text(F_MONO8, right, NAV_Y + 12, a, C_TEXT);
        }
        /* the phrase's name, when it has one and there is room: shown, not edited here */
        if (song->arrangement[entry].name[0] &&
            x + gfx_text_w(F_COND9, song->arrangement[entry].name) + 4 < right)
            gfx_text(F_COND9, x, NAV_Y + 12, song->arrangement[entry].name, C_DIM);
    }
    gfx_fill(APP_X0, NAV_Y + NAV_H, APP_INNER, 1, C_DIM, 77);

    if (on_cell()) {
        const Step *st = cell(col, row);
        uint8_t n = st->notes[0];
        ui_card(0, "NOTE", n <= NOTE_MAX ? note_name(n, a) : n == NOTE_OFF ? "OFF" : "---", 0);
        if (n <= NOTE_MAX)
            ui_card(1, "OCTAVE", ui_dec(b, n / 12 - 1), 0);
        else
            ui_card(1, 0, 0, 0);
    } else {
        ui_card(0, 0, 0, 0);
        ui_card(1, 0, 0, 0);
    }
    ui_card(2, 0, 0, 0);
    ui_card(3, 0, 0, 0);
    ui_cards_rule();

    /* The left hand's manual: one line, always. */
    x = gfx_text(F_MONO8, APP_X0, LEFT_Y + 8, "SELECT", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "STEP", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "ALGO", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "TRACK", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "PRESETS", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "PHR", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "OCT", C_DIM);
    {   /* where the 27 keys are: F3..G5 unmoved */
        int lo = 53 + kbd_oct * 12, hi = lo + 26;
        b[0] = names[lo % 12 * 2], b[1] = (char)('0' + lo / 12 - 1), b[2] = '-';
        b[3] = names[hi % 12 * 2], b[4] = (char)('0' + hi / 12 - 1), b[5] = '\0';
        gfx_text(F_MONO8, x + 3, LEFT_Y + 8, b, C_TEXT);
    }

    /* The column heads: the track and its block -- the track's handle. */
    for (int t = 0; t < song->num_tracks; t++) {
        int hx = CELL_X + t * CELL_PITCH;
        bool cur = row == -1 && col == t, muted = player_muted(t);
        char tag[3] = { 'T', (char)('1' + t), '\0' };

        if (!has(t) && blk(t) != PATTERN_HOLD) {
            gfx_text_mid(F_MONO8, hx, CELL_W, HEAD_Y + 8, tag, C_DIM);
            continue; /* left out of this phrase: nothing under it */
        }
        gfx_slab(hx, HEAD_Y, CELL_W, HEAD_H, cur ? C_ACCENT : C_TEXT, cur ? 255 : 11);
        gfx_text_mid(F_MONO8, hx, CELL_W, HEAD_Y + 8, tag,
                     cur ? C_ON_ACC : muted ? C_MUTE : col == t ? C_ACCENT : C_DIM);
        gfx_text_mid(F_COND9, hx, CELL_W, HEAD_Y + 17, has(t) ? ui_hex2(a, blk(t)) : "ON",
                     cur ? C_ON_ACC : muted ? C_MUTE : has(t) ? C_TITLE : C_DIM);
    }

    for (int i = 0; i < ROWS && top + i < steps; i++) {
        int r = top + i, y = ROWS_Y + i * ROW_PITCH;
        if (r == play_row) {
            gfx_slab(APP_X0, y, APP_INNER, ROW_H, C_PLAY, 255);
            gfx_fill(APP_X0 + 1, y, 3, ROW_H, C_ACCENT, 255);
        } else {
            gfx_slab(APP_X0, y, APP_INNER, ROW_H, C_TEXT, r % 16 == 0 ? 43 : r % 4 == 0 ? 26 : 11);
        }
        gfx_text_mid(F_MONO8, POS_X, POS_W, y + 9, ui_hex2(a, r),
                     r == play_row ? C_ACCENT : r % 16 == 0 ? C_TEXT : r % 4 == 0 ? C_BEAT : C_DIM);
        for (int t = 0; t < song->num_tracks; t++)
            if (has(t))
                draw_cell(t, r, y, player_muted(t));
    }

    for (int k = 0; k < 12; k++) {
        bs[k] = k == AT_BACK ? UI_BTN_BACK : !w[k][0] ? UI_BTN_NONE : UI_BTN_ON;
        if (k == AT_LOOP || k == AT_REC || (k == AT_UNDO && !undo_can()))
            bs[k] = UI_BTN_OFF; /* not written yet, or nothing to take back */
        if (k == AT_CLEAR)
            bs[k] = row < 0 ? UI_BTN_NONE : on_cell() ? UI_BTN_ON : UI_BTN_OFF;
        if (k == AT_OPEN && !has(col))
            bs[k] = UI_BTN_OFF;
        if (k == AT_MUTE && player_muted(col))
            bs[k] = UI_BTN_ASK; /* red while this track is cut, as on MIXER */
        if (k == AT_PLAY && playing)
            bs[k] = UI_BTN_LIVE;
    }
    ui_btn_map(w, bs, held);
}

/* -------------------------------------------------------------- api --- */
int phrase_entry(void) { return entry; }
int phrase_track(void) { return col; }
int phrase_row(void) { return row < 0 || !has(col) ? 0 : prow(col, row); }

void phrase_resume(void)
{
    measure(); /* PATTERN may have changed a length, or the block */
    settle_col();
    pool_full = false;
    saved = 0;
    chord_next = 0;
    prev_btns = prev_keys = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

void phrase_open(int e, int track)
{
    entry = e < 0 ? 0 : e >= song->arrange_len ? song->arrange_len - 1 : e;
    col = track < 0 ? 0 : track;
    row = top = 0;
    follow = true;
    phrase_resume();
}

int phrase_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
    int d;
#define PRESSED(at) ((btns >> APP_BTN_AT[at]) & 1u)

    prev_btns = in->btns;
    prev_keys = in->keys;
    held = in->btns;
    playing = player_playing();

    /* The left hand moves. */
    if (in->enc[PE_PRESET])
        step_phrase(in->enc[PE_PRESET]);
    if (in->enc[PE_SELECT]) {
        row += in->enc[PE_SELECT];
        row = row < -1 ? -1 : row >= steps ? steps - 1 : row;
        follow = false; /* the hand took the page back */
    }
    if (in->enc[PE_ALGO])
        move_col(in->enc[PE_ALGO]);
    if (btns & (1u << PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (btns & (1u << PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;

    if (btns || keys || in->enc[PE_K1] || in->enc[PE_K2])
        saved = 0;

    /* The right hand writes the step's first note. */
    if (on_cell()) {
        if ((d = in->enc[PE_K1]) != 0)
            pool_full = !edit_turn_note(song, col, blk(col), prow(col, row), 0, d);
        if ((d = in->enc[PE_K2]) != 0)
            pool_full = !edit_turn_octave(song, col, blk(col), prow(col, row), 0, d);
    }
    /* The keys write notes, and what was written is heard: the first one
     * is the step's first note, the ones pressed while it is held the
     * rest of the chord. */
    if (!in->keys) {
        if (chord_next)
            player_note_off();
        chord_next = 0;
    }
    for (int k = 0; on_cell() && k < PLAT_KEYS; k++) {
        int n = 53 + kbd_oct * 12 + k;
        if (!((keys >> k) & 1u) || n < NOTE_MIN || n > NOTE_MAX || chord_next >= STEP_MAX_NOTES)
            continue;
        pool_full = !edit_write_note(song, col, blk(col), prow(col, row), chord_next, (uint8_t)n);
        player_note_on(edit_instr_at(song, col, blk(col), prow(col, row)), (uint8_t)n);
        chord_next++;
    }

    if (PRESSED(AT_CLEAR) && on_cell())
        pool_full = !edit_clear_step(song, col, blk(col), prow(col, row));
    if (PRESSED(AT_MUTE))
        player_toggle_mute(col);
    if (PRESSED(AT_UNDO) && undo_pop()) {
        if (entry >= song->arrange_len)
            entry = song->arrange_len - 1; /* it may be older than this screen */
        measure();
        settle_col();
        pool_full = false;
    }
    if (PRESSED(AT_SAVE))
        saved = app_save() == 0 ? 1 : -1;
    if (PRESSED(AT_PLAY)) {
        if (playing) {
            player_stop();
        } else {
            player_play_entry(entry);
            follow = true;
        }
    }

    measure();
    play_row = playing && player_entry() == entry ? player_row() % steps : -1;
    if (follow && play_row >= 0) {
        top = play_row - play_row % ROWS; /* a page at a time, never a crawl */
    } else {
        if (row >= 0 && row < top)
            top = row;
        if (row >= top + ROWS)
            top = row - ROWS + 1;
        if (row < 0)
            top = 0;
    }
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(draw);

    if (PRESSED(AT_BACK)) {
        player_note_off();
        song_pool_compact(song);
        return PHRASE_BACK;
    }
    if (PRESSED(AT_OPEN) && has(col)) {
        player_note_off();
        return PHRASE_PATTERN;
    }
    if (PRESSED(AT_MIXER)) {
        player_note_off();
        return PHRASE_MIXER;
    }
    return PHRASE_STAY;
#undef PRESSED
}
