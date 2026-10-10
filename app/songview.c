/* SPDX-License-Identifier: GPL-3.0-only */
#include "songview.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "ui.h"

enum {
    NAV_Y = 2, NAV_H = 17,
    LEFT_Y = 49,
    LINE_Y = 60,
    HEAD_Y = 73,
    ROWS_Y = 82, ROW_H = 12, ROW_PITCH = 13, ROWS = 10,
    POS_X = 9, POS_W = 15, CELL_X = 26, CELL_W = 23, CELL_PITCH = 25,
};
enum { AT_FX, AT_LOOP, AT_ENV, AT_CLEAR, AT_OPEN, AT_MIXER,
       AT_BACK, AT_SAVE, AT_UNDO, AT_ADD, AT_PLAY, AT_REC };

#define song (&app_song)

/* row -1 is the song's own line; col 0 the phrase's number, 1.. a track. */
static int row, col, top;
static bool ask_delete;
static int8_t saved;
static uint32_t prev_btns, held;
static bool playing;
static int play_row, play_fill; /* the row the playhead is on, how far through it (0..ROW_H) */

static SongEntry *entry_at(int r) { return &song->arrangement[r]; }
static bool on_cell(void) { return row >= 0 && col >= 1; }
static bool on_pos(void) { return row >= 0 && col == 0; }

/* ------------------------------------------------------------ edits --- */
/* One past the last block of the track that holds something or is played
 * somewhere: turning past the last block gives a fresh one, and no
 * further. */
static int block_limit(int t)
{
    int last = -1;
    for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
        bool used = !song_pattern_blank(song, t, p);
        for (int i = 0; i < song->arrange_len && !used; i++)
            used = song->arrangement[i].pattern[t] == p;
        if (used)
            last = p;
    }
    return last + 1 < TRACK_MAX_PATTERNS ? last + 1 : TRACK_MAX_PATTERNS - 1;
}

/* KNOB 1 on a cell: empty, then ON (plays nothing, cuts nothing), then
 * the track's blocks. */
static void turn_block(int d)
{
    SongEntry *e = entry_at(row);
    int t = col - 1, p = e->pattern[t];
    int v = p == PATTERN_NONE ? -2 : p == PATTERN_HOLD ? -1 : p, hi = block_limit(t);

    if (e->kind == ENTRY_REF)
        return; /* a reference plays its target's cells */
    v += d;
    v = v < -2 ? -2 : v > hi ? hi : v;
    e->pattern[t] = v == -2 ? PATTERN_NONE : v == -1 ? PATTERN_HOLD : (uint8_t)v;
    if (v == -2)
        e->level[t] = VOL_FULL; /* a level with no block under it would resurface */
    app_mark_dirty();
}

static void turn_level(int d)
{
    SongEntry *e = entry_at(row);
    int t = col - 1, v = e->level[t] + d;
    if (e->pattern[t] == PATTERN_NONE || e->kind == ENTRY_REF)
        return;
    e->level[t] = (uint8_t)(v < 0 ? 0 : v > VOL_FULL ? VOL_FULL : v);
    app_mark_dirty();
}

/* KNOB 1 on a phrase's number: how long it lasts. AUTO is one full
 * cycle of its blocks; turning up from AUTO starts at that length. */
static void turn_steps(int d)
{
    SongEntry *e = entry_at(row);
    int v = e->steps;
    if (e->kind == ENTRY_REF && e->ref_kind == REF_SECTION)
        return;
    if (v == 0 && d > 0)
        v = song_entry_cycle(song, e->kind == ENTRY_REF ? entry_at(e->ref_target) : e) + d - 1;
    else
        v += d;
    e->steps = (uint16_t)(v < 1 ? 0 : v > ENTRY_CYCLE_MAX ? ENTRY_CYCLE_MAX : v);
    app_mark_dirty();
}

static void turn_song(int k, int d)
{
    int v;
    switch (k) {
    case 0:
        v = song->bpm + d;
        song->bpm = (uint16_t)(v < BPM_MIN ? BPM_MIN : v > BPM_MAX ? BPM_MAX : v);
        break;
    case 1:
        v = song->swing + d;
        song->swing = (uint8_t)(v < SWING_MIN ? SWING_MIN : v > SWING_MAX ? SWING_MAX : v);
        break;
    case 2:
        v = song->ticks_per_step + d;
        song->ticks_per_step = (uint8_t)(v < 1 ? 1 : v > TICKS_MAX ? TICKS_MAX : v);
        break;
    default:
        v = song->num_tracks + d;
        song->num_tracks = (uint8_t)(v < 1 ? 1 : v > SONG_MAX_TRACKS ? SONG_MAX_TRACKS : v);
        if (col > song->num_tracks)
            col = song->num_tracks;
        break;
    }
    app_mark_dirty();
}

/* References count rows: when a row comes or goes, the ones below it
 * move, and what pointed at them must follow. */
static void shift_refs(int from, int by, int gone)
{
    for (int i = 0; i < song->arrange_len; i++) {
        SongEntry *e = entry_at(i);
        if (e->kind != ENTRY_REF || e->ref_kind != REF_PHRASE)
            continue;
        if (e->ref_target == gone)
            e->ref_target = 0xFF; /* its target is gone: normalize makes it an empty phrase */
        else if (e->ref_target >= from)
            e->ref_target = (uint8_t)(e->ref_target + by);
    }
}

/* + PHRASE: a copy of the phrase under the cursor, right below it. The
 * name stays with the original: a name marks one phrase. */
static void insert_phrase(void)
{
    int at = row < 0 ? 0 : row;
    if (song->arrange_len >= SONG_ARRANGE_LEN)
        return;
    shift_refs(at + 1, 1, -1);
    for (int i = song->arrange_len; i > at + 1; i--)
        *entry_at(i) = *entry_at(i - 1);
    *entry_at(at + 1) = *entry_at(at);
    entry_at(at + 1)->name[0] = '\0';
    song->arrange_len++;
    song_sections_normalize(song);
    row = at + 1;
    app_mark_dirty();
}

static void delete_phrase(void)
{
    if (song->arrange_len <= 1) { /* a song has at least one phrase: empty it instead */
        SongEntry *e = entry_at(0);
        for (int t = 0; t < SONG_MAX_TRACKS; t++) {
            e->pattern[t] = PATTERN_NONE;
            e->level[t] = VOL_FULL;
        }
        e->steps = 0;
        e->kind = ENTRY_PHRASE;
    } else {
        for (int i = row; i < song->arrange_len - 1; i++)
            *entry_at(i) = *entry_at(i + 1);
        song->arrange_len--;
        shift_refs(row + 1, -1, row);
        song_sections_normalize(song);
        if (row >= song->arrange_len)
            row = song->arrange_len - 1;
    }
    app_mark_dirty();
}

/* ---------------------------------------------------------- drawing --- */
static void draw_cards(void)
{
    char a[8], b[8];

    if (row == -1) {
        ui_card(0, "BPM", ui_dec(a, song->bpm), 0);
        ui_card(1, "SWING", ui_dec(b, song->swing), "%");
        ui_card(2, "TICKS", ui_dec(a, song->ticks_per_step), 0);
        ui_card(3, "TRACKS", ui_dec(b, song->num_tracks), 0);
        return;
    }
    if (col == 0) {
        const SongEntry *e = entry_at(row);
        if (e->kind == ENTRY_REF && e->ref_kind == REF_SECTION)
            ui_card(0, "STEPS", "REF", 0);
        else if (e->steps)
            ui_card(0, "STEPS", ui_dec(a, e->steps), 0);
        else
            ui_card(0, "STEPS", "AUTO", ui_dec(b, song_entry_steps(song, row)));
        ui_card(1, 0, 0, 0);
    } else {
        const SongEntry *e = entry_at(row);
        uint8_t p = e->pattern[col - 1];
        ui_card(0, "BLOCK", p == PATTERN_NONE ? "--" : p == PATTERN_HOLD ? "ON" : ui_hex2(a, p), 0);
        if (p == PATTERN_NONE)
            ui_card(1, 0, 0, 0);
        else
            ui_card(1, "LEVEL", ui_dec(b, e->level[col - 1]), 0);
    }
    ui_card(2, 0, 0, 0);
    ui_card(3, 0, 0, 0);
}

static void draw(void)
{
    static const char *const words[12] = { "", "LOOP", "", "CLEAR", "OPEN", "MIXER",
                                           "\xE2\x80\xB9 MENU", "SAVE", "UNDO", "+ PHRASE", "PLAY", "REC" };
    const char *w[12];
    uint8_t st[12];
    char t[16], n[8];
    int x, lost = app_not_carried();
    uint8_t cur_block = on_cell() ? entry_at(row)->pattern[col - 1] : PATTERN_NONE;

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    x = gfx_text(F_COND12, APP_X0, NAV_Y + 13, "\xE2\x80\xB9 MENU", C_TITLE);
    gfx_text(F_COND12, x + 6, NAV_Y + 13, app_name, C_TEXT);
    if (saved) {
        const char *m = saved > 0 ? "SAVED" : "NOT SAVED";
        gfx_text(F_COND9, APP_X0 + APP_INNER - gfx_text_w(F_COND9, m), NAV_Y + 12, m,
                 saved > 0 ? C_ACCENT : C_MUTE);
    } else if (lost) {
        /* What this file had that the machine does not carry, and that a
         * save from here would lose. */
        x = APP_X0 + APP_INNER - gfx_text_w(F_MONO8, " NOT CARRIED");
        gfx_text(F_MONO8, x, NAV_Y + 12, " NOT CARRIED", C_FX);
        gfx_text(F_MONO8, x - gfx_text_w(F_MONO8, ui_dec(n, lost)), NAV_Y + 12, n, C_FX);
    }
    gfx_fill(APP_X0, NAV_Y + NAV_H, APP_INNER, 1, C_DIM, 77);

    draw_cards();
    ui_cards_rule();

    x = gfx_text(F_MONO8, APP_X0, LEFT_Y + 8, "SELECT", C_DIM);
    x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "PHRASE", C_TEXT);
    x = gfx_text(F_MONO8, x + 5, LEFT_Y + 8, "ALGO", C_DIM);
    gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "TRACK", C_TEXT);

    {   /* the song's own line: always readable, and a place the cursor goes */
        bool cur = row == -1;
        uint16_t ink = cur ? C_ON_ACC : C_TEXT, dim = cur ? C_ON_ACC : C_DIM;
        gfx_slab(APP_X0, LINE_Y, APP_INNER, ROW_H, cur ? C_ACCENT : C_TEXT, cur ? 255 : 11);
        x = gfx_text(F_MONO8, APP_X0 + 4, LINE_Y + 9, ui_dec(n, song->bpm), ink);
        x = gfx_text(F_MONO8, x + 2, LINE_Y + 9, "BPM", dim);
        x = gfx_text(F_MONO8, x + 7, LINE_Y + 9, "SWING", dim);
        x = gfx_text(F_MONO8, x + 2, LINE_Y + 9, ui_dec(n, song->swing), ink);
        x = gfx_text(F_MONO8, x + 7, LINE_Y + 9, ui_dec(n, song->ticks_per_step), ink);
        x = gfx_text(F_MONO8, x + 2, LINE_Y + 9, "TICKS", dim);
        x = gfx_text(F_MONO8, x + 7, LINE_Y + 9, ui_dec(n, song->num_tracks), ink);
        gfx_text(F_MONO8, x + 2, LINE_Y + 9, "TRACKS", dim);
    }

    gfx_text_mid(F_MONO8, POS_X, POS_W, HEAD_Y + 7, "#", C_DIM);
    for (int k = 0; k < song->num_tracks; k++) {
        t[0] = 'T', t[1] = (char)('1' + k), t[2] = '\0';
        gfx_text_mid(F_MONO8, CELL_X + k * CELL_PITCH, CELL_W, HEAD_Y + 7, t,
                     row >= 0 && col == k + 1 ? C_ACCENT : C_DIM);
    }

    for (int i = 0; i < ROWS && top + i < song->arrange_len; i++) {
        int r = top + i, y = ROWS_Y + i * ROW_PITCH;
        const SongEntry *e = entry_at(r);
        bool here = row == r, asked = here && col == 0 && ask_delete;

        if (r == play_row) {
            gfx_slab(APP_X0, y, APP_INNER, ROW_H, C_PLAY, 255);
            /* the playhead is a gauge: it fills down the row as the phrase goes */
            gfx_fill(APP_X0 + 1, y, 3, ROW_H, C_ACCENT, 64);
            gfx_fill(APP_X0 + 1, y, 3, play_fill, C_ACCENT, 255);
        } else {
            gfx_slab(APP_X0, y, APP_INNER, ROW_H, C_TEXT, 11);
        }
        if (here && col == 0)
            gfx_slab(POS_X, y + 1, POS_W, 10, asked ? C_MUTE : C_ACCENT, 255);
        gfx_text_mid(F_MONO8, POS_X, POS_W, y + 9, ui_hex2(t, r),
                     asked ? C_WHITE : here && col == 0 ? C_ON_ACC : r == play_row ? C_ACCENT : C_DIM);
        if (e->kind == ENTRY_REF) {
            /* A reference wears where it points: it has no cells of its own. */
            x = gfx_text(F_MONO8, CELL_X + 2, y + 9, e->ref_kind == REF_SECTION ? "REF SEC" : "REF", C_DIM);
            gfx_text(F_MONO8, x + 4, y + 9, ui_hex2(t, e->ref_target), C_FX);
            continue;
        }
        for (int k = 0; k < song->num_tracks; k++) {
            int cx = CELL_X + k * CELL_PITCH;
            uint8_t p = e->pattern[k];
            bool cur = here && col == k + 1;
            /* the other cells of the column that play the cursor's block */
            bool echo = !cur && on_cell() && col == k + 1 && p == cur_block && p < TRACK_MAX_PATTERNS;

            if (cur)
                gfx_slab(cx, y + 1, CELL_W, 10, C_ACCENT, 255);
            else if (p < TRACK_MAX_PATTERNS)
                gfx_slab(cx, y + 1, CELL_W, 10, echo ? C_ACCENT : C_TEXT, echo ? 41 : 18);
            else
                gfx_frame_rect(cx, y + 1, CELL_W, 10, C_DIM, 76);
            if (p == PATTERN_HOLD)
                gfx_text_mid(F_COND9, cx, CELL_W, y + 9, "ON", cur ? C_ON_ACC : C_DIM);
            else if (p < TRACK_MAX_PATTERNS)
                gfx_text_mid(F_COND11, cx, CELL_W, y + 10, ui_hex2(t, p), cur ? C_ON_ACC : C_TEXT);
            /* a cell turned down says so: a line as wide as its level */
            if (p != PATTERN_NONE && e->level[k] != VOL_FULL && !cur)
                gfx_fill(cx + 1, y + 10, (CELL_W - 2) * e->level[k] / VOL_FULL, 1, C_FX, 255);
        }
    }

    for (int b = 0; b < 12; b++) {
        bool cell_block = on_cell() && entry_at(row)->pattern[col - 1] < TRACK_MAX_PATTERNS;
        w[b] = words[b];
        st[b] = b == AT_BACK ? UI_BTN_BACK : UI_BTN_ON;
        if (!words[b][0])
            st[b] = UI_BTN_NONE;
        if (b == AT_LOOP || b == AT_UNDO || b == AT_REC)
            st[b] = UI_BTN_OFF; /* not written yet */
        if (b == AT_OPEN)       /* a block opens in PATTERN; a phrase has no screen yet */
            st[b] = cell_block ? UI_BTN_ON : row == -1 ? UI_BTN_NONE : UI_BTN_OFF;
        if (b == AT_ADD && row == -1)
            st[b] = UI_BTN_NONE;
        if (b == AT_CLEAR) {
            w[b] = on_pos() ? (ask_delete ? "SURE" : "DELETE") : "CLEAR";
            st[b] = row == -1 ? UI_BTN_NONE : ask_delete ? UI_BTN_ASK
                    : on_cell() && entry_at(row)->pattern[col - 1] == PATTERN_NONE ? UI_BTN_OFF : UI_BTN_ON;
        }
        if (b == AT_PLAY && playing) {
            w[b] = "STOP";
            st[b] = UI_BTN_LIVE;
        }
    }
    ui_btn_map(w, st, held);
}

/* -------------------------------------------------------------- api --- */
int songview_entry(void) { return row < 0 ? 0 : row; }

void songview_open(bool reset_cursor)
{
    if (reset_cursor) {
        row = top = 0;
        col = 1;
    }
    if (row >= song->arrange_len)
        row = song->arrange_len - 1;
    if (col > song->num_tracks)
        col = song->num_tracks;
    ask_delete = false;
    saved = 0;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

int songview_frame(const PlatInput *in, int *entry, int *track)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool moved = in->enc[PE_SELECT] || in->enc[PE_ALGO];
    bool turned = in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3] || in->enc[PE_K4];
    int out = SONGVIEW_STAY, d;
#define PRESSED(at) ((btns >> APP_BTN_AT[at]) & 1u)

    prev_btns = in->btns;
    held = in->btns;
    playing = player_playing();

    if (btns || moved || turned)
        saved = 0;
    /* DELETE asks; only a second DELETE answers yes. */
    if (ask_delete && (moved || turned || (btns && !PRESSED(AT_CLEAR))))
        ask_delete = false;

    row += in->enc[PE_SELECT];
    row = row < -1 ? -1 : row >= song->arrange_len ? song->arrange_len - 1 : row;
    col += in->enc[PE_ALGO];
    col = col < 0 ? 0 : col > song->num_tracks ? song->num_tracks : col;

    if (row == -1) {
        if ((d = in->enc[PE_K1]) != 0) turn_song(0, d);
        if ((d = in->enc[PE_K2]) != 0) turn_song(1, d);
        if ((d = in->enc[PE_K3]) != 0) turn_song(2, d);
        if ((d = in->enc[PE_K4]) != 0) turn_song(3, d);
    } else if (col == 0) {
        if ((d = in->enc[PE_K1]) != 0) turn_steps(d);
    } else {
        if ((d = in->enc[PE_K1]) != 0) turn_block(d);
        if ((d = in->enc[PE_K2]) != 0) turn_level(d);
    }

    if (PRESSED(AT_CLEAR) && row >= 0) {
        if (col == 0) {
            if (ask_delete) {
                delete_phrase();
                ask_delete = false;
            } else {
                ask_delete = true;
            }
        } else if (entry_at(row)->kind != ENTRY_REF) {
            entry_at(row)->pattern[col - 1] = PATTERN_NONE;
            entry_at(row)->level[col - 1] = VOL_FULL;
            app_mark_dirty();
        }
    }
    if (PRESSED(AT_ADD) && row >= 0)
        insert_phrase();
    if (PRESSED(AT_SAVE))
        saved = app_save() == 0 ? 1 : -1;
    if (PRESSED(AT_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_song(row < 0 ? 0 : row, false);
    }
    if (PRESSED(AT_OPEN) && on_cell() && entry_at(row)->kind != ENTRY_REF &&
        entry_at(row)->pattern[col - 1] < TRACK_MAX_PATTERNS) {
        *entry = row;
        *track = col - 1;
        out = SONGVIEW_PATTERN;
    }
    if (PRESSED(AT_MIXER))
        out = SONGVIEW_MIXER;
    if (PRESSED(AT_BACK))
        out = SONGVIEW_MENU;

    if (row >= 0 && row < top)
        top = row;
    if (row >= top + ROWS)
        top = row - ROWS + 1;
    play_row = playing ? player_entry() : -1;
    if (play_row >= 0) {
        int steps = song_entry_steps(song, play_row);
        play_fill = steps > 0 ? 1 + (int)player_row() * (ROW_H - 1) / steps : ROW_H;
        if (play_fill > ROW_H)
            play_fill = ROW_H;
    }
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(draw);
    return out;
#undef PRESSED
}
