/* SPDX-License-Identifier: GPL-3.0-only */
#include "songview.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "ui.h"
#include "undo.h"

/* SONG, as validated on the panel on 28/10 (NOTES.md): one row a phrase,
 * its number fixed at the left, then the block each track plays, six
 * tracks a screen.
 *
 *   SELECT  the phrase          KNOB 1  the track; left of the first, the phrase's number
 *   KNOB 2  on a cell its block, -- below 00 and one new block past the last;
 *           on the number the phrase's length (AUTO, then steps)
 *   SEL     on a cell: CHOOSE PATTERN, the track's blocks (SELECT walks, PLAY listens:
 *           the block the cursor rests on plays alone; SEL takes it, GLO goes
 *           back); on the number: the SECTION
 *   EDIT    on a cell: its block, in PATTERN; on the number: the SECTION
 *   SEQ     on the number: a copy of the phrase below (on a cell: free, 29/10)
 *   LFO     on the number: delete the phrase (twice)
 *   FX      the MIXER      GLO   the PROJECT: what is GLObal (HOME goes home)
 *   ALGO    UNDO left, REDO right     SAVE  PLAY  as printed
 * SEQ and LFO on the number, and ARP, have no mockup yet: provisional. */
enum { CELL_W = 31, CELLS = 6 };
enum { POP_TOP = 24, POP_ROWS = 7 };

#define song (&app_song)

static int row, col, top, left; /* col -1: the phrase's number */
static bool ask_delete;
static const char *said;        /* SAVED, NOT SAVED: until the hand does something else */
static uint32_t prev_btns;
static bool playing;
static int play_row = -1;
/* The panel of a track's blocks. While PLAY previews one, the cell holds it
 * for real, the track soloed; GLO or HOME puts back what was there. */
static bool pop;
static int pop_cur, pop_top, pop_orig, pop_preview = -1;
/* PLAY turns the listening on: from then on the block the cursor rests on
 * plays by itself, once it has rested SETTLE frames (a quick turn of SELECT
 * starts nothing on the way); PLAY again, SEL or GLO turn it off (29/10). */
enum { SETTLE = 12 }; /* frames: 200 ms */
static bool listening;
static int settle;

static SongEntry *entry_at(int r) { return &song->arrangement[r]; }
static bool is_ref(int r) { return entry_at(r)->kind == ENTRY_REF; }

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

static void set_block(int r, int t, uint8_t p)
{
    SongEntry *e = entry_at(r);
    undo_bytes(&e->pattern[t], 1);
    undo_bytes(&e->level[t], 1);
    e->pattern[t] = p;
    if (p == PATTERN_NONE)
        e->level[t] = VOL_FULL; /* a level with no block under it would resurface */
    app_mark_dirty();
}

/* KNOB 2 on a cell: --, then the blocks; ON (a cell that plays nothing and
 * cuts nothing, from moondaw's files) counts as -- when turned from. */
static void turn_block(int d)
{
    int p = entry_at(row)->pattern[col], v = p >= TRACK_MAX_PATTERNS ? -1 : p, hi = block_limit(col);
    v += d;
    v = v < -1 ? -1 : v > hi ? hi : v;
    set_block(row, col, v < 0 ? PATTERN_NONE : (uint8_t)v);
}

/* KNOB 2 on a phrase's number: how long it lasts. AUTO is one full cycle
 * of its blocks; turning up from AUTO starts at that length. */
static void turn_steps(int d)
{
    SongEntry *e = entry_at(row);
    int v = e->steps;
    undo_bytes(&e->steps, sizeof e->steps);
    if (v == 0 && d > 0)
        v = song_entry_cycle(song, e) + d - 1;
    else
        v += d;
    e->steps = (uint16_t)(v < 1 ? 0 : v > ENTRY_CYCLE_MAX ? ENTRY_CYCLE_MAX : v);
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

/* A row comes or goes: every row below moves and the references are
 * renumbered. What UNDO keeps is the arrangement as it was, whole. */
static void undo_arrangement(void)
{
    undo_bytes(&song->arrange_len, 1);
    undo_bytes(&song->section_count, 1);
    undo_bytes(song->arrangement, (uint32_t)song->arrange_len * sizeof(SongEntry));
    if (song->section_count)
        undo_bytes(song->sections, (uint32_t)song->section_count * sizeof song->sections[0]);
}

/* A copy of the phrase under the cursor, right below it. The name stays
 * with the original: a name marks one phrase. */
static void insert_phrase(void)
{
    if (song->arrange_len >= SONG_ARRANGE_LEN)
        return;
    undo_arrangement();
    shift_refs(row + 1, 1, -1);
    for (int i = song->arrange_len; i > row + 1; i--)
        *entry_at(i) = *entry_at(i - 1);
    *entry_at(row + 1) = *entry_at(row);
    entry_at(row + 1)->name[0] = '\0';
    song->arrange_len++;
    song_sections_normalize(song);
    row++;
    app_mark_dirty();
}

static void delete_phrase(void)
{
    undo_arrangement();
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

/* ------------------------------------------------------------ panel --- */
/* Its rows: --, then 00 up to one past the track's last block. */
static int pop_rows(void) { return block_limit(col) + 2; }
static uint8_t pop_block(int k) { return k == 0 ? PATTERN_NONE : (uint8_t)(k - 1); }

static void preview_end(void)
{
    if (pop_preview < 0)
        return;
    player_stop();
    if (player_soloed(col))
        player_toggle_solo(col);
    entry_at(row)->pattern[col] = (uint8_t)pop_orig;
    pop_preview = -1;
}

/* The block under the cursor, alone, round and round; -- is silence. */
static void preview_here(void)
{
    preview_end();
    if (pop_block(pop_cur) == PATTERN_NONE)
        return;
    entry_at(row)->pattern[col] = pop_block(pop_cur);
    if (!player_soloed(col))
        player_toggle_solo(col);
    player_play_entry(row);
    pop_preview = pop_cur;
}

static void pop_close(bool take)
{
    listening = false;
    preview_end();
    if (take && pop_block(pop_cur) != pop_orig)
        set_block(row, col, pop_block(pop_cur));
    pop = false;
}

/* ---------------------------------------------------------- drawing --- */
static void hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 6, word, C_DIM);
}

/* The whole screen, nothing of the grid behind it: drawn over the grid it
 * read as a grid of one track (29/10). It says what it is for and where
 * from, and every block has its PLAY. */
static void draw_pop(void)
{
    int n = pop_rows(), bar = n > POP_ROWS ? 6 : 0;
    char s[24] = "PHR ", h[3];
    grid_hex2(s + 4, (uint8_t)row);
    s[6] = ' ', s[7] = '/', s[8] = ' ', s[9] = 'T', s[10] = 'R', s[11] = 'K', s[12] = ' ';
    s[13] = (char)('1' + col), s[14] = 0;
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND14, 6, 17, "CHOOSE PATTERN", C_TITLE);
    gfx_text(F_COND12, PLAT_W - 6 - gfx_text_w(F_COND12, s), 17, s, C_DIM);
    gfx_fill(0, POP_TOP - 3, PLAT_W, 1, C_TRACK, 255);
    for (int r = 0; r < POP_ROWS && pop_top + r < n; r++) {
        int k = pop_top + r, y = POP_TOP + r * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent, x;
        uint8_t b = pop_block(k);
        bool here = k == pop_cur;
        uint16_t ink = here ? C_ON_ACC : b == PATTERN_NONE || k == n - 1 ? C_DIM : C_TEXT;
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 8 - bar, GRID_ROW - 2, C_ACCENT, 255);
        if (b == pop_orig) /* the block in place, as HOME marks the open project */
            gfx_fill(6, y + 6, 3, GRID_ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
        x = gfx_text(F_COND20, 16, base, b == PATTERN_NONE ? "--" : grid_hex2(h, b), ink);
        if (b == PATTERN_NONE)
            gfx_text(F_COND14, x + 8, base, "silent", ink);
        else if (k == n - 1 && song_pattern_blank(song, col, b))
            gfx_text(F_COND14, x + 8, base, "new", ink);
        if (b != PATTERN_NONE)
            grid_play_sign(PLAT_W - 26 - bar, y + 8,
                      here ? C_ON_ACC : k == pop_preview ? C_ACCENT : C_DIM, k == pop_preview);
    }
    if (n > POP_ROWS) {
        int hh = POP_ROWS * GRID_ROW * POP_ROWS / n, y = POP_TOP + POP_ROWS * GRID_ROW * pop_top / n;
        gfx_fill(PLAT_W - 4, POP_TOP, 2, POP_ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, hh, C_DIM, 255);
    }
    hint(6, "SEL", "TAKE", false);
    hint(96, "PLAY", "LISTEN", false);
    hint(PLAT_W - 6, "GLO", "BACK", true);
}

static void draw(void)
{
    int n = song->arrange_len, tracks = song->num_tracks;
    char s[8];

    grid_clear();
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else if (col < 0 && !is_ref(row)) /* the phrase's length, while the cursor is on it */
        grid_corner(entry_at(row)->steps ? ui_dec(s, entry_at(row)->steps) : "AUTO", C_TEXT, 0, 0);
    for (int c = 0; c < CELLS && left + c < tracks; c++) {
        s[0] = (char)('1' + left + c), s[1] = 0;
        grid_head(GRID_CELL_X + c * CELL_W, CELL_W, s, left + c == col);
    }
    for (int v = 0; v < GRID_ROWS && top + v < n; v++) {
        int r = top + v;
        const SongEntry *e = entry_at(r);
        bool band = r == row, asked = band && col < 0 && ask_delete;
        if (band)
            gfx_slab(2, grid_y(v) + 1, PLAT_W - 4 - (n > GRID_ROWS ? 6 : 0), GRID_ROW - 2,
                     asked ? C_MUTE : C_ACCENT, 255);
        if (r == play_row)
            grid_playing(v, band);
        grid_num(v, r, band, band && col < 0 && !asked, r == play_row ? C_TITLE : C_TEXT);
        if (e->kind == ENTRY_REF) { /* a reference wears where it points: it has no cells of its own */
            int x = gfx_text(F_COND20, GRID_CELL_X + 6, grid_base(v),
                             e->ref_kind == REF_SECTION ? "REF SEC" : "REF", band ? C_ON_ACC : C_DIM);
            gfx_text(F_COND20, x + 8, grid_base(v), grid_hex2(s, e->ref_target), band ? C_ON_ACC : C_FX);
            continue;
        }
        for (int t = left; t < left + CELLS && t < tracks; t++) {
            uint8_t p = e->pattern[t];
            bool on = band && t == col;
            uint16_t ink = p < TRACK_MAX_PATTERNS ? (r == play_row ? C_TITLE : C_TEXT) : C_DIM;
            grid_cell(v, GRID_CELL_X + (t - left) * CELL_W, CELL_W,
                      p < TRACK_MAX_PATTERNS ? grid_hex2(s, p) : p == PATTERN_HOLD ? "ON" : "--",
                      band, on, ink);
        }
    }
    grid_frame(top, n);
    if (pop)
        draw_pop();
}

/* -------------------------------------------------------------- api --- */
int songview_entry(void) { return row; }

void songview_open(bool reset_cursor)
{
    if (reset_cursor)
        row = top = col = left = 0;
    if (row >= song->arrange_len)
        row = song->arrange_len - 1;
    if (col >= song->num_tracks)
        col = song->num_tracks - 1;
    ask_delete = false;
    said = 0;
    pop = false;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

void songview_cancel(void)
{
    if (pop)
        pop_close(false);
}

int songview_frame(const PlatInput *in, int *entry, int *track)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool acted = btns || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2];
    int out = SONGVIEW_STAY;
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    playing = player_playing();
    if (acted)
        said = 0;

    if (pop) {
        pop_cur += in->enc[PE_SELECT];
        grid_follow(&pop_cur, &pop_top, pop_rows(), POP_ROWS);
        if (in->enc[PE_SELECT])
            settle = SETTLE;
        if (PRESSED(PB_PLAY)) {
            listening = !listening;
            if (listening)
                preview_here();
            else
                preview_end();
        } else if (listening && settle > 0 && --settle == 0 && pop_cur != pop_preview) {
            preview_here();
        }
        if (PRESSED(PB_SEL))
            pop_close(true);
        else if (PRESSED(PB_GLO))
            pop_close(false);
        play_row = -1;
        plat_led_btn(PB_PLAY, listening);
        gfx_frame(draw);
        return SONGVIEW_STAY;
    }

    /* DELETE asks; only a second LFO answers yes. */
    if (ask_delete && (in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] ||
                       (btns && !PRESSED(PB_LFO))))
        ask_delete = false;

    row += in->enc[PE_SELECT];
    row = row < 0 ? 0 : row >= song->arrange_len ? song->arrange_len - 1 : row;
    col += in->enc[PE_K1];
    col = col < -1 ? -1 : col >= song->num_tracks ? song->num_tracks - 1 : col;

    if (in->enc[PE_K2] && !is_ref(row)) {
        if (col < 0)
            turn_steps(in->enc[PE_K2]);
        else
            turn_block(in->enc[PE_K2]);
    }
    if (PRESSED(PB_SEL) && !is_ref(row)) {
        if (col < 0) {
            *entry = row;
            *track = 0;
            out = SONGVIEW_PHRASE;
        } else {
            pop = true;
            pop_orig = entry_at(row)->pattern[col];
            pop_cur = pop_orig < TRACK_MAX_PATTERNS ? pop_orig + 1 : 0;
            pop_top = 0;
            pop_preview = -1, listening = false, settle = 0;
            grid_follow(&pop_cur, &pop_top, pop_rows(), POP_ROWS);
        }
    }
    if (PRESSED(PB_SEQ) && !is_ref(row) && col < 0)
        insert_phrase();
    if (PRESSED(PB_EDIT) && !is_ref(row)) { /* what is under the cursor, to edit */
        *entry = row;
        *track = col < 0 ? 0 : col;
        if (col < 0)
            out = SONGVIEW_PHRASE;
        else if (entry_at(row)->pattern[col] < TRACK_MAX_PATTERNS)
            out = SONGVIEW_PATTERN;
    }
    if (PRESSED(PB_LFO) && col < 0) {
        if (ask_delete)
            delete_phrase();
        ask_delete = !ask_delete;
    }
    if (undo_turn(in->enc[PE_ALGO], &said)) {
        if (row >= song->arrange_len)
            row = song->arrange_len - 1;
        if (col >= song->num_tracks)
            col = song->num_tracks - 1;
    }
    if (PRESSED(PB_SAVE))
        said = app_save() == 0 ? "SAVED" : "NOT SAVED";
    if (PRESSED(PB_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_song(row, false);
    }
    if (PRESSED(PB_FX))
        out = SONGVIEW_MIXER;
    if (PRESSED(PB_GLO)) /* GLObal: what holds for the whole song */
        out = SONGVIEW_PROJECT;

    grid_follow(&row, &top, song->arrange_len, GRID_ROWS);
    {
        int t = col < 0 ? 0 : col;
        grid_follow(&t, &left, song->num_tracks, CELLS);
    }
    play_row = playing ? player_entry() : -1;
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(draw);
    return out;
#undef PRESSED
}
