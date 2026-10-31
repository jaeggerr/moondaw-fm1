/* SPDX-License-Identifier: GPL-3.0-only */
#include "songview.h"

#include "../core/player.h"
#include "app.h"
#include "edit.h"
#include "gfx.h"
#include "grid.h"
#include "loop.h"
#include "ui.h"
#include "undo.h"

/* SONG, as validated on the panel on 28/10 (NOTES.md): one row a phrase,
 * its number fixed at the left, then the block each track plays, six
 * tracks a screen.
 *
 *   SELECT, KNOB 1  the phrase (SELECT onto its number)   KNOB 2  the track; left of the first, the phrase's number
 *   KNOB 3  WHAT THE THING UNDER THE CURSOR IS -- on a cell its block, -- below 00
 *           and one new block past the last (NEW in the corner, the cell in C_FX
 *           until something is written in it); what it lands on is heard by itself
 *           once the hand rests, unless the song is playing -- then it is not
 *           interrupted (30/10), and PLAY ends the audition. On the number: whether
 *           the row holds blocks of its own or points at a phrase already in the
 *           song (31/10); on a reference: which phrase, or which section
 *   KNOB 4  HOW MUCH -- on a cell its level in the phrase (a bar at its foot when
 *           below full); on the number the phrase's length (AUTO, then steps); on a
 *           reference the same, as long as it points at a phrase (31/10)
 *   PRESETS on a cell: CHOOSE PATTERN, the track's blocks with what each one
 *           holds and NEW on the first free number; on a reference: CHOOSE PHRASE,
 *           the phrases and the sections it could point at with their names. PLAY
 *           listens to the one the cursor rests on (30/10)
 *   SEL     on a cell: its block, in PATTERN; on the number: the SECTION; on a
 *           reference: the phrase it plays (31/10)
 *   EDIT    tapped: the SELECTION of blocks, locked on until another tap (on the
 *           phrase's number it takes whole phrases); held: its commands for as long
 *           as it is down. OP1 OP2 OP3 copy paste cut, OP4 clears, MONO solo/mute
 *           -- a cell is its block and the level it plays at; OP6 makes the phrases
 *           it covers the loop PLAY goes round (with none, an end at the cursor,
 *           then the other), and the same phrases again forget it (30/10)
 *   SEQ     on the number: a copy of the phrase below (on a cell: free, 29/10)
 *   LFO     on the number: delete the phrase (twice) -- a reference's too
 *   keys    on a cell: play the track's instrument -- the first the block names --
 *           and write nothing; OCT- OCT+ the keyboard's octave (30/10)
 *   PLAY    the song from the cursor's phrase; with the hand still 1.5 s the view
 *           follows the phrase that plays, a page at a time. PLAY again stops it
 *           and leaves the cursor on the phrase it stopped in (30/10)
 *   FX      the MIXER      GLO   the PROJECT: what is GLObal (HOME goes home)
 *   ALGO    UNDO left, REDO right     SAVE  PLAY  as printed
 * SEQ and LFO on the number, and ARP, have no mockup yet: provisional. */
enum { CELL_W = 31, CELLS = 6 };
enum { CH_TOP = 24, CH_ROWS = 7 }; /* CHOOSE PATTERN, as CHOOSE INSTRUMENT's */

#define song (&app_song)

static int row, col, top, left; /* col -1: the phrase's number */
static bool ask_delete;
static const char *said;        /* SAVED, NOT SAVED: until the hand does something else */
static bool level_said;         /* KNOB 4 turned: the corner says the cell's level */
static uint32_t prev_btns, prev_keys;
static UiShift shift; /* EDIT held + MONO: the track under the cursor alone, or muted */
static bool playing;
static int play_row = -1;
static int still; /* frames the hand has not touched anything: past GRID_FOLLOW_IDLE the view follows PLAY */
/* KNOB 3 on a cell: the block it lands on is heard by itself, the track
 * soloed, once the hand has rested SETTLE frames -- a quick turn through
 * the blocks starts nothing on the way (29/10, kept from the panel that
 * came before). Only while nothing plays: a song that is running is never
 * interrupted, the block is in its cell and comes round on its own
 * (30/10). PLAY ends the audition, and the PLAY after it plays the song. */
enum { SETTLE = 12 }; /* frames: 200 ms */
static int settle;
static bool audition;      /* a block is playing by itself, for the ear */
static bool audition_solo; /* and this screen is what soloed its track */
static int audition_track = -1;

static int kbd_oct;  /* the keys' octave, as PATTERN's */
static bool trying;  /* a key of the hand is sounding */

/* The SELECTION of blocks (30/10), EDIT's own job here as it is PATTERN's:
 * a quick tap locks it on, the cursor stretches it from where it was
 * tapped, and the black keys are the commands -- OP1 copy, OP2 paste, OP3
 * cut, OP4 clear, on the cells themselves (a cell is a block and the level
 * it plays at). EDIT held gives the same commands for as long as it is
 * down and leaves the SELECTION as it was (ui.h). */
static bool selecting, anchored;
static int an_row, an_col;
/* What OP1 took. `clip_cols` -1: whole phrases, every track. */
enum { CLIP_ROWS = 32 };
static uint8_t clip_pat[CLIP_ROWS][SONG_MAX_TRACKS], clip_lvl[CLIP_ROWS][SONG_MAX_TRACKS];
static int clip_rows, clip_cols;

static SongEntry *entry_at(int r) { return &song->arrangement[r]; }
static bool is_ref(int r) { return entry_at(r)->kind == ENTRY_REF; }

/* What the keys play where the cursor is: the first instrument the block
 * under it names, the song's first when it names none; -1 where there is
 * no block to play -- a reference, the phrase's number, an empty cell
 * (30/10). */
static int cell_instr(void)
{
    int p = col < 0 || is_ref(row) ? PATTERN_NONE : entry_at(row)->pattern[col];
    if (p >= TRACK_MAX_PATTERNS)
        return -1;
    for (int r = 0; r < song->patterns[col][p].length; r++)
        if (song_step(song, col, p, r)->instrument != INSTR_NONE)
            return song_step(song, col, p, r)->instrument;
    return 0;
}

/* ------------------------------------------------------------ edits --- */
/* A number the track already has: a block with something written in it, or
 * one the arrangement names somewhere. The rest of the sixteen are free --
 * what CHOOSE PATTERN lists, and what NEW takes the first of.
 *
 * `except_cell`: a blank block only the cursor's own cell names is the
 * fresh one being tried, not one the song holds -- turning on past it
 * makes no second new block, and turning back leaves nothing behind
 * (30/10). The list counts it, being where the cursor is. */
static bool block_used(int t, int p, bool except_cell)
{
    if (!song_pattern_blank(song, t, p))
        return true;
    for (int i = 0; i < song->arrange_len; i++)
        if (song->arrangement[i].pattern[t] == p && !(except_cell && i == row && t == col))
            return true;
    return false;
}

/* One past the last block of the track that holds something or is played
 * somewhere: turning past the last block gives a fresh one, and no
 * further. */
static int block_limit(int t)
{
    int last = -1;
    for (int p = 0; p < TRACK_MAX_PATTERNS; p++)
        if (block_used(t, p, true))
            last = p;
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

/* KNOB 3 on a cell: --, then the blocks; ON (a cell that plays nothing and
 * cuts nothing, from moondaw's files) counts as -- when turned from. */
static void turn_block(int d)
{
    int p = entry_at(row)->pattern[col], v = p >= TRACK_MAX_PATTERNS ? -1 : p, hi = block_limit(col);
    v += d;
    v = v < -1 ? -1 : v > hi ? hi : v;
    set_block(row, col, v < 0 ? PATTERN_NONE : (uint8_t)v);
}

/* KNOB 4 on a cell: its level in the phrase, 0..127 (the file's `vol`), 4 a
 * notch -- the drums softer in the intro, the bass up in the chorus,
 * without touching the block or the mixer (29/10). */
static void turn_level(int d)
{
    SongEntry *e = entry_at(row);
    int v = e->level[col] + 4 * d;
    undo_bytes(&e->level[col], 1);
    e->level[col] = (uint8_t)(v < 0 ? 0 : v > VOL_FULL ? VOL_FULL : v);
    app_mark_dirty();
    level_said = true;
}

/* KNOB 4 on a phrase's number: how long it lasts. AUTO is one full cycle
 * of its blocks; turning up from AUTO starts at that length. It was KNOB 3
 * until 31/10, when KNOB 3 took what the row IS and left the knob beside it
 * the how much -- which is what KNOB 4 means on a cell, and in FX. */
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

/* ------------------------------------------------------ references --- */
/* What a reference may point at, in one order: the phrases that hold
 * blocks of their own, then the sections. A reference to a reference
 * resolves to nothing (`song_entry_resolve`), and so would a section the
 * row stands in, so neither is offered; its own row never is either. The
 * list is walked by KNOB 3 on the row and listed by CHOOSE PHRASE: one
 * order, so the knob and the list agree. */
static int ref_slots(void)
{
    int n = 0;
    for (int i = 0; i < song->arrange_len; i++)
        if (i != row && !is_ref(i))
            n++;
    for (int s = 0; s < song->section_count; s++)
        if (s != entry_at(row)->section)
            n++;
    return n;
}

/* Slot `k`: what it points at, and whether that is a phrase or a section. */
static void ref_slot(int k, uint8_t *kind, uint8_t *target)
{
    for (int i = 0; i < song->arrange_len; i++)
        if (i != row && !is_ref(i) && k-- == 0) {
            *kind = REF_PHRASE, *target = (uint8_t)i;
            return;
        }
    for (int s = 0; s < song->section_count; s++)
        if (s != entry_at(row)->section && k-- == 0) {
            *kind = REF_SECTION, *target = (uint8_t)s;
            return;
        }
}

/* Which slot a reference is on now; 0 when what it holds is not among them
 * -- a file may point at anything, the knob only at what plays. */
static int ref_slot_of(const SongEntry *e)
{
    int k = 0;
    for (int i = 0; i < song->arrange_len; i++) {
        if (i == row || is_ref(i))
            continue;
        if (e->ref_kind == REF_PHRASE && e->ref_target == i)
            return k;
        k++;
    }
    for (int s = 0; s < song->section_count; s++) {
        if (s == entry_at(row)->section)
            continue;
        if (e->ref_kind == REF_SECTION && e->ref_target == s)
            return k;
        k++;
    }
    return 0;
}

/* The slot a new reference starts on: the phrase above it, which is what
 * "again" nearly always means; the first one when it stands at the top. */
static int ref_near(void)
{
    int k = 0, best = 0;
    for (int i = 0; i < song->arrange_len; i++) {
        if (i == row || is_ref(i))
            continue;
        if (i < row)
            best = k;
        k++;
    }
    return best;
}

/* The first reference that points at this row, or -1. A reference that
 * comes to point at a reference resolves to nothing
 * (`song_entry_resolve`), so a row that others name cannot become one
 * without their going quiet. */
static int pointed_at(int r)
{
    for (int i = 0; i < song->arrange_len; i++) {
        const SongEntry *e = entry_at(i);
        if (i != r && e->kind == ENTRY_REF && e->ref_kind == REF_PHRASE && e->ref_target == r)
            return i;
    }
    return -1;
}

/* KNOB 3 on a phrase's number: what the row IS. A phrase holds blocks of
 * its own; a reference plays a phrase already in the song, or a whole
 * section of it, and has no cells. Two states, so a detent either way is
 * the other one.
 *
 * The blocks go when it becomes a reference and do NOT come back when it
 * stops being one: it comes back an empty phrase, and UNDO is what holds
 * what it held -- as UNDO holds every other edit on this screen. One turn
 * too many is one ALGO to the left. */
static void turn_kind(int d)
{
    SongEntry *e = entry_at(row);
    bool to_ref = ((is_ref(row) + d) % 2 + 2) % 2 != 0;

    if (!d || to_ref == is_ref(row))
        return;
    if (to_ref && ref_slots() == 0) { /* a song of one phrase has nothing to point at */
        said = "NO TARGET";
        return;
    }
    undo_arrangement();
    for (int t = 0; t < SONG_MAX_TRACKS; t++) {
        e->pattern[t] = PATTERN_NONE;
        e->level[t] = VOL_FULL;
    }
    e->steps = 0;
    e->kind = (uint8_t)(to_ref ? ENTRY_REF : ENTRY_PHRASE);
    if (to_ref) {
        int lost;
        ref_slot(ref_near(), &e->ref_kind, &e->ref_target);
        /* the turn is his, but a row that goes quiet elsewhere is not
         * allowed to go quiet without a word: it is named, and the corner
         * holds about eight letters before the first track's number. */
        if ((lost = pointed_at(row)) >= 0) {
            static char word[12];
            int k = 2;
            grid_hex2(word, lost);
            for (const char *t = " SILENT"; *t; t++)
                word[k++] = *t;
            word[k] = 0;
            said = word;
        }
    }
    app_mark_dirty();
}

/* KNOB 3 on a reference: where it points, along the one list. */
static void turn_target(int d)
{
    SongEntry *e = entry_at(row);
    int n = ref_slots(), k;

    if (n == 0)
        return;
    k = ref_slot_of(e) + d;
    k = k < 0 ? 0 : k >= n ? n - 1 : k;
    undo_bytes(&e->ref_kind, 1);
    undo_bytes(&e->ref_target, 1);
    ref_slot(k, &e->ref_kind, &e->ref_target);
    app_mark_dirty();
}

/* KNOB 4 on a reference: how long it plays, as it is on a phrase. AUTO is
 * the whole of what it points at; turning up from AUTO starts there. A
 * reference to a section is as long as the phrases it holds and the file
 * keeps no number for it (`trk_save`), so the knob says nothing there. */
static void turn_ref_steps(int d)
{
    SongEntry *e = entry_at(row);
    int v = e->steps;

    if (e->ref_kind == REF_SECTION)
        return;
    undo_bytes(&e->steps, sizeof e->steps);
    if (v == 0 && d > 0)
        v = song_entry_steps(song, row) + d - 1;
    else
        v += d;
    e->steps = (uint16_t)(v < 1 ? 0 : v > ENTRY_CYCLE_MAX ? ENTRY_CYCLE_MAX : v);
    app_mark_dirty();
}

/* The rectangle the SELECTION covers: from the anchor to the cursor. On the
 * phrase's number it is the whole row, every track. */
static void sel_rect(int *r0, int *r1, int *c0, int *c1)
{
    int ar = anchored ? an_row : row, ac = anchored ? an_col : col;
    *r0 = ar < row ? ar : row;
    *r1 = ar < row ? row : ar;
    *c0 = ac < col ? ac : col;
    *c1 = ac < col ? col : ac;
    if (*c0 < 0)
        *c0 = -1, *c1 = song->num_tracks - 1;
}

/* A reference has no cells of its own: it is passed over, never written. */
static void sel_op(int op)
{
    static char word[16];
    int r0, r1, c0, c1, n, t0, nc;

    sel_rect(&r0, &r1, &c0, &c1);
    n = r1 - r0 + 1;
    t0 = c0 < 0 ? 0 : c0;
    nc = c1 - t0 + 1;
    if (op == UI_KEY_LOOP) { /* the phrases PLAY goes round; with none, an end at the cursor */
        if (anchored)
            loop_set(-1, r0, r1);
        else
            loop_press(-1, row);
        anchored = false;
        said = loop_state() == LOOP_SET ? "LOOP" : loop_state() == LOOP_ONE_END ? "LOOP: THE OTHER END" : "NO LOOP";
        return;
    }
    if (op == UI_OP_PASTE) {
        int at = clip_cols < 0 || col < 0 ? 0 : col;
        int cols = clip_cols < 0 ? song->num_tracks : clip_cols;
        n = 0;
        if (!clip_rows)
            return;
        for (int i = 0; i < clip_rows && row + i < song->arrange_len; i++) {
            SongEntry *e = entry_at(row + i);
            if (is_ref(row + i))
                continue;
            for (int t = 0; t < cols && at + t < song->num_tracks; t++) {
                undo_bytes(&e->pattern[at + t], 1);
                undo_bytes(&e->level[at + t], 1);
                e->pattern[at + t] = clip_pat[i][t];
                e->level[at + t] = clip_lvl[i][t];
            }
            n++;
        }
        app_mark_dirty();
    } else {
        if (op == UI_OP_COPY || op == UI_OP_CUT) {
            clip_rows = n > CLIP_ROWS ? CLIP_ROWS : n;
            clip_cols = c0 < 0 ? -1 : nc;
            for (int i = 0; i < clip_rows; i++) {
                const SongEntry *e = entry_at(r0 + i);
                for (int t = 0; t < nc; t++) {
                    bool none = is_ref(r0 + i);
                    clip_pat[i][t] = none ? PATTERN_NONE : e->pattern[t0 + t];
                    clip_lvl[i][t] = none ? VOL_FULL : e->level[t0 + t];
                }
            }
        }
        if (op == UI_OP_CUT || op == UI_OP_CLEAR)
            for (int i = r0; i <= r1; i++) {
                if (is_ref(i))
                    continue;
                for (int t = t0; t <= c1; t++)
                    set_block(i, t, PATTERN_NONE);
            }
        anchored = false;
    }
    {
        const char *w = op == UI_OP_COPY ? "COPIED " : op == UI_OP_PASTE ? "PASTED " : op == UI_OP_CUT ? "CUT "
                                                                                                      : "CLEARED ";
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

/* -------------------------------------------------------------- ear --- */
/* The block under the cursor, alone and round and round, for the ear. A
 * blank block is silence: nothing is started for it (29/10). */
static void audition_start(void)
{
    int p = col < 0 || is_ref(row) ? PATTERN_NONE : entry_at(row)->pattern[col];
    if (p >= TRACK_MAX_PATTERNS || song_pattern_blank(song, col, p))
        return;
    audition_track = col;
    if (!player_soloed(col))
        player_toggle_solo(col), audition_solo = true;
    player_play_entry(row);
    audition = true;
}

/* The ear has had it: the sound stops and the track goes back among the
 * others -- a solo MONO had put on is left alone. */
static void audition_end(void)
{
    if (!audition)
        return;
    player_stop();
    if (audition_solo && audition_track >= 0 && player_soloed(audition_track))
        player_toggle_solo(audition_track);
    audition = audition_solo = false;
    audition_track = -1;
}

/* --------------------------------------------------- choose pattern --- */
/* CHOOSE PATTERN, which the PRESETS knob opens (30/10). A panel of blocks
 * stood between the hand and the cell until the 30th and was taken out
 * because its list of bare numbers said nothing KNOB 3 does not -- and
 * KNOB 3 still chooses, the ear saying which. This one is not that list:
 * it says what each number holds, and it has NEW, which takes the first
 * number the track does not use yet. Working out which block is free was
 * the one thing the cell could not do.
 *
 * Its rows: --, then the track's blocks, then NEW. */
static bool choosing;
static int ch_cur, ch_top, ch_n, ch_free, ch_orig;
static uint8_t ch_num[TRACK_MAX_PATTERNS]; /* the numbers it lists, ascending */
/* PLAY turns the listening on: from then on the block the cursor rests on
 * plays by itself, once it has rested SETTLE frames (a quick walk down the
 * list starts nothing on the way); PLAY again, SEL or HOME turn it off.
 * The panel of 29/10 did exactly this and it is what he missed when it
 * went (30/10). */
static bool listening;
static int ch_heard = -1; /* the row sounding, -1 for none */
static bool ch_solo;      /* and this screen is what soloed the track */
static uint8_t ch_was;    /* the cell's own byte, to put back: ON is not a block and must survive */

static int ch_rows(void) { return ch_n + 2; }
static bool ch_is_new(int k) { return k == ch_n + 1; }
static uint8_t ch_block(int k) { return k == 0 ? PATTERN_NONE : ch_num[k - 1]; }

/* The ear goes quiet and the cell gets back what it held. The block is put
 * in the cell to be heard -- the player plays phrases, never a block on its
 * own -- so nothing of this is journalled: it is undone here, on every way
 * out of the list. */
static void ch_hush(void)
{
    if (ch_heard < 0)
        return;
    player_stop();
    if (ch_solo && player_soloed(col)) /* a solo MONO had put on is left alone */
        player_toggle_solo(col);
    entry_at(row)->pattern[col] = ch_was;
    ch_heard = -1;
    ch_solo = false;
}

/* The block under the cursor, alone and round and round. -- is silence and
 * so is an empty block: nothing is started for them, and NEW has no block
 * to start yet. */
static void ch_listen(void)
{
    ch_hush();
    if (ch_is_new(ch_cur) || ch_block(ch_cur) == PATTERN_NONE ||
        song_pattern_blank(song, col, ch_block(ch_cur)))
        return;
    entry_at(row)->pattern[col] = ch_block(ch_cur);
    if (!player_soloed(col))
        player_toggle_solo(col), ch_solo = true;
    player_play_entry(row);
    ch_heard = ch_cur;
}

/* NEW: the first number the track does not use, as long as a fresh block
 * is and with nothing in it -- a block shortened once keeps what lies past
 * its length, and a block called new must not sing it. Then it goes in the
 * cell: that is the whole point, the hand never works out which is free.
 * Both writes are one gesture of the journal, so one UNDO takes the new
 * block back out of the cell. */
static void new_block(int p)
{
    Pattern *pt = &song->patterns[col][p];
    undo_bytes(&pt->length, sizeof pt->length);
    pt->length = PATTERN_DEF_STEPS;
    edit_erase_range(song, col, p, 0, PATTERN_DEF_STEPS, EDIT_CLIP_ALL);
    set_block(row, col, (uint8_t)p);
}

static void ch_open(void)
{
    ch_n = 0;
    ch_free = -1;
    for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
        if (block_used(col, p, false))
            ch_num[ch_n++] = (uint8_t)p;
        else if (ch_free < 0)
            ch_free = p;
    }
    ch_was = entry_at(row)->pattern[col];
    ch_orig = ch_was < TRACK_MAX_PATTERNS ? ch_was : -1;
    ch_cur = ch_top = 0;
    for (int k = 0; k < ch_n; k++)
        if (ch_num[k] == ch_orig)
            ch_cur = k + 1;
    grid_follow(&ch_cur, &ch_top, ch_rows(), CH_ROWS);
    listening = false;
    ch_heard = -1;
    ch_solo = false;
    settle = 0;
    choosing = true;
    gfx_invalidate();
}

/* SEL takes what the cursor is on; HOME leaves and changes nothing. */
static void ch_close(bool take)
{
    listening = false;
    ch_hush(); /* the cell holds what it held before the ear borrowed it */
    settle = 0;
    if (take && ch_is_new(ch_cur)) {
        if (ch_free < 0)
            said = "ALL SIXTEEN USED"; /* a track has sixteen blocks, and this one has them all */
        else
            new_block(ch_free);
    } else if (take && ch_block(ch_cur) != entry_at(row)->pattern[col]) {
        set_block(row, col, ch_block(ch_cur));
    }
    choosing = false;
    gfx_invalidate();
}

static void ch_frame(const PlatInput *in, uint32_t btns)
{
#define PRESSED(b) ((btns >> (b)) & 1u)
    /* PRESETS walks it too, and exactly as the others do, the ear included:
     * what opens a list keeps driving it, or the hand is made to change
     * knob for nothing (30/10). SELECT and KNOB 1 stay what they were. */
    ch_cur += in->enc[PE_SELECT] + in->enc[PE_K1] + in->enc[PE_PRESET];
    grid_follow(&ch_cur, &ch_top, ch_rows(), CH_ROWS);
    if (in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_PRESET])
        settle = SETTLE;
    if (PRESSED(PB_PLAY)) {
        listening = !listening;
        if (listening)
            ch_listen();
        else
            ch_hush();
    } else if (listening && settle > 0 && --settle == 0 && ch_cur != ch_heard) {
        ch_listen();
    }
    if (PRESSED(PB_SEL))
        ch_close(true);
    else if (PRESSED(PB_HOME) || PRESSED(PB_GLO))
        ch_close(false);
    ui_led_state(PB_PLAY, listening);
    ui_led_guide(0); /* SEL takes, HOME goes back: nothing of this screen's */
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

/* The whole screen, nothing of the grid behind it: drawn over the grid it
 * read as a grid of one track (29/10). */
static void ch_draw(void)
{
    int n = ch_rows(), bar = n > CH_ROWS ? 6 : 0;
    char s[24] = "PHR ", h[3], d[12];

    grid_hex2(s + 4, row);
    s[6] = ' ', s[7] = '/', s[8] = ' ', s[9] = 'T', s[10] = 'R', s[11] = 'K', s[12] = ' ';
    s[13] = (char)('1' + col), s[14] = 0;
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND14, 6, 17, "CHOOSE PATTERN", C_TITLE);
    gfx_text(F_COND12, PLAT_W - 6 - gfx_text_w(F_COND12, s), 17, s, C_DIM);
    gfx_fill(0, CH_TOP - 3, PLAT_W, 1, C_TRACK, 255);
    for (int r = 0; r < CH_ROWS && ch_top + r < n; r++) {
        int k = ch_top + r, y = CH_TOP + r * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent, x;
        bool here = k == ch_cur, fresh = ch_is_new(k);
        uint8_t b = ch_block(k);
        bool blank = !fresh && k > 0 && song_pattern_blank(song, col, b);
        uint16_t ink = here      ? C_ON_ACC
                       : fresh   ? (ch_free < 0 ? C_DIM : C_FX) /* C_FX: what is not written yet */
                       : k == 0  ? C_DIM
                       : blank   ? C_DIM
                                 : C_TEXT;
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 8 - bar, GRID_ROW - 2, C_ACCENT, 255);
        if (!fresh && k > 0 && b == ch_orig) /* the block in the cell now, as HOME marks the open project */
            gfx_fill(6, y + 6, 3, GRID_ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
        x = gfx_text(F_COND20, 16, base, fresh ? "NEW" : k == 0 ? "--" : grid_hex2(h, b), ink);
        if (fresh && ch_free >= 0) { /* which number it will take: the question the cell could not answer */
            int x2 = gfx_text(F_COND14, x + 8, base, "block", ink);
            gfx_text(F_COND20, x2 + 6, base, grid_hex2(h, ch_free), ink);
        } else if (fresh) {
            gfx_text(F_COND14, x + 8, base, "all sixteen used", ink);
        } else if (k == 0) {
            gfx_text(F_COND14, x + 8, base, "silent", ink);
        } else {
            int w;
            if (blank) /* what it holds: the first thing a bare number never said */
                gfx_text(F_COND14, x + 8, base, "empty", ink);
            {   /* how long it is: "16 steps", the other thing a number never said */
                int j = 0;
                ui_dec(d, song->patterns[col][b].length);
                while (d[j])
                    j++;
                for (const char *t = " steps"; *t; t++)
                    d[j++] = *t;
                d[j] = 0;
            }
            w = gfx_text_w(F_COND14, d);
            gfx_text(F_COND14, PLAT_W - 32 - bar - w, base, d, ink);
            if (!blank) /* what can be heard has its PLAY; an empty block is silence */
                grid_play_sign(PLAT_W - 26 - bar, y + 8,
                               here ? C_ON_ACC : k == ch_heard ? C_ACCENT : C_DIM, k == ch_heard);
        }
    }
    if (n > CH_ROWS) {
        int hh = CH_ROWS * GRID_ROW * CH_ROWS / n, y = CH_TOP + CH_ROWS * GRID_ROW * ch_top / n;
        gfx_fill(PLAT_W - 4, CH_TOP, 2, CH_ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, hh, C_DIM, 255);
    }
    ch_hint(6, "SEL", "TAKE", false);
    ch_hint(96, "PLAY", "LISTEN", false);
    ch_hint(PLAT_W - 6, "HOME", "BACK", true);
}

/* ---------------------------------------------------- choose phrase --- */
/* CHOOSE PHRASE: what a reference points at, opened by PRESETS on it as
 * CHOOSE PATTERN is opened on a cell (31/10). KNOB 3 on the row walks the
 * same list without opening it; this one says what each one is -- the
 * phrase's name and how long it lasts, the section's name and how many
 * phrases it holds -- which is what a bare number cannot. SEL takes it,
 * PLAY listens, HOME leaves it as it was. */
static bool picking;
static int cp_cur, cp_top;
static bool cp_listening;
static int cp_heard = -1; /* the slot sounding, -1 for none */

static void cp_hush(void)
{
    if (cp_heard < 0)
        return;
    player_stop();
    cp_heard = -1;
}

/* What the cursor rests on, whole and round and round: a phrase as it is,
 * a section from the first of its phrases. Nothing is borrowed and nothing
 * is written, so none of this is journalled. */
static void cp_listen(void)
{
    uint8_t kind, target;
    int at, first, count;

    cp_hush();
    ref_slot(cp_cur, &kind, &target);
    at = target;
    if (kind == REF_SECTION)
        at = song_section_bounds(song, target, &first, &count) ? first : -1;
    if (at < 0 || at >= song->arrange_len)
        return;
    player_play_entry(at);
    cp_heard = cp_cur;
}

static void cp_open(void)
{
    picking = true;
    cp_cur = ref_slot_of(entry_at(row));
    cp_top = 0;
    grid_follow(&cp_cur, &cp_top, ref_slots(), CH_ROWS);
    cp_listening = false;
    cp_heard = -1;
    settle = 0;
    gfx_invalidate();
}

/* SEL takes what the cursor is on; HOME leaves and changes nothing. */
static void cp_close(bool take)
{
    SongEntry *e = entry_at(row);

    cp_listening = false;
    cp_hush();
    settle = 0;
    if (take) {
        uint8_t kind, target;
        ref_slot(cp_cur, &kind, &target);
        if (kind != e->ref_kind || target != e->ref_target) {
            undo_bytes(&e->ref_kind, 1);
            undo_bytes(&e->ref_target, 1);
            e->ref_kind = kind;
            e->ref_target = target;
            app_mark_dirty();
        }
    }
    picking = false;
    gfx_invalidate();
}

static void cp_frame(const PlatInput *in, uint32_t btns)
{
#define PRESSED(b) ((btns >> (b)) & 1u)
    /* PRESETS walks it, as it walks CHOOSE PATTERN, and so does KNOB 3:
     * this is the list KNOB 3 walks on the row itself, so the knob that
     * chose outside keeps choosing inside (30/10). */
    cp_cur += in->enc[PE_SELECT] + in->enc[PE_K1] + in->enc[PE_PRESET] + in->enc[PE_K3];
    grid_follow(&cp_cur, &cp_top, ref_slots(), CH_ROWS);
    if (in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_PRESET] || in->enc[PE_K3])
        settle = SETTLE;
    if (PRESSED(PB_PLAY)) {
        cp_listening = !cp_listening;
        if (cp_listening)
            cp_listen();
        else
            cp_hush();
    } else if (cp_listening && settle > 0 && --settle == 0 && cp_cur != cp_heard) {
        cp_listen();
    }
    if (PRESSED(PB_SEL))
        cp_close(true);
    else if (PRESSED(PB_HOME) || PRESSED(PB_GLO))
        cp_close(false);
    ui_led_state(PB_PLAY, cp_listening);
    ui_led_guide(0); /* SEL takes, HOME goes back: nothing of this screen's */
#undef PRESSED
}

/* The whole screen, nothing of the grid behind it, as CHOOSE PATTERN's. */
static void cp_draw(void)
{
    int n = ref_slots(), bar = n > CH_ROWS ? 6 : 0;
    const SongEntry *me = entry_at(row);
    char s[20], h[3], d[12];

    s[0] = 'P', s[1] = 'H', s[2] = 'R', s[3] = ' ';
    grid_hex2(s + 4, row);
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND14, 6, 17, "CHOOSE PHRASE", C_TITLE);
    gfx_text(F_COND12, PLAT_W - 6 - gfx_text_w(F_COND12, s), 17, s, C_DIM);
    gfx_fill(0, CH_TOP - 3, PLAT_W, 1, C_TRACK, 255);
    for (int r = 0; r < CH_ROWS && cp_top + r < n; r++) {
        int k = cp_top + r, y = CH_TOP + r * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent, x, w, j = 0;
        bool here = k == cp_cur;
        uint16_t ink = here ? C_ON_ACC : C_TEXT;
        const char *name, *tail;
        uint8_t kind, target;

        ref_slot(k, &kind, &target);
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 8 - bar, GRID_ROW - 2, C_ACCENT, 255);
        if (kind == me->ref_kind && target == me->ref_target) /* where it points now */
            gfx_fill(6, y + 6, 3, GRID_ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
        if (kind == REF_SECTION) {
            int first, count;
            x = gfx_text(F_COND20, 16, base, "SEC", ink);
            x = gfx_text(F_COND20, x + 6, base, grid_hex2(h, target), ink);
            name = song->sections[target];
            if (!song_section_bounds(song, target, &first, &count))
                count = 0;
            for (const char *q = ui_dec(d, count); *q; q++)
                s[j++] = *q;
            tail = count == 1 ? " phrase" : " phrases";
        } else {
            x = gfx_text(F_COND20, 16, base, grid_hex2(h, target), ink);
            name = song->arrangement[target].name;
            for (const char *q = ui_dec(d, song_entry_steps(song, target)); *q; q++)
                s[j++] = *q;
            tail = " steps";
        }
        for (; *tail; tail++)
            s[j++] = *tail;
        s[j] = 0;
        if (name[0]) /* a phrase that was never named says nothing here */
            gfx_text(F_COND14, x + 8, base, name, ink);
        w = gfx_text_w(F_COND14, s);
        gfx_text(F_COND14, PLAT_W - 10 - bar - w, base, s, ink);
    }
    if (n > CH_ROWS) {
        int hh = CH_ROWS * GRID_ROW * CH_ROWS / n, y = CH_TOP + CH_ROWS * GRID_ROW * cp_top / n;
        gfx_fill(PLAT_W - 4, CH_TOP, 2, CH_ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, hh, C_DIM, 255);
    }
    ch_hint(6, "SEL", "TAKE", false);
    ch_hint(96, "PLAY", "LISTEN", false);
    ch_hint(PLAT_W - 6, "HOME", "BACK", true);
}

/* ---------------------------------------------------------- drawing --- */
static void draw(void)
{
    int n = song->arrange_len, tracks = song->num_tracks;
    char s[8];

    grid_clear();
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else if (level_said && col >= 0 && !is_ref(row)) /* KNOB 4: the cell's level */
        grid_corner("VOL", C_DIM, ui_dec(s, entry_at(row)->level[col]), C_TEXT);
    else if (col < 0 || is_ref(row)) /* KNOB 4: how long the row lasts, its number or its reference */
        grid_corner(entry_at(row)->steps ? ui_dec(s, entry_at(row)->steps) : "AUTO", C_TEXT, 0, 0);
    else if (entry_at(row)->pattern[col] < TRACK_MAX_PATTERNS &&
             song_pattern_blank(song, col, entry_at(row)->pattern[col]))
        grid_corner("NEW", C_FX, 0, 0); /* the cell under the cursor wears the cursor's colour: the corner says it */
    else if (loop_state() == LOOP_SET) { /* the loop, in place of where one is: LOOP 02-05 */
        char lo[3], hi[3], range[6];
        grid_hex2(lo, loop_from()), grid_hex2(hi, loop_to());
        range[0] = lo[0], range[1] = lo[1], range[2] = '-', range[3] = hi[0], range[4] = hi[1], range[5] = 0;
        grid_corner("LOOP", C_TITLE, range, C_TITLE);
    } else if (loop_state() == LOOP_ONE_END)
        grid_corner("LOOP", C_TITLE, grid_hex2(s, loop_from()), C_TEXT);
    for (int c = 0; c < CELLS && left + c < tracks; c++) /* green alone, red muted */
        grid_track_head(GRID_CELL_X + c * CELL_W, CELL_W, left + c, left + c == col);
    for (int v = 0; v < GRID_ROWS && top + v < n; v++) {
        int r = top + v;
        const SongEntry *e = entry_at(r);
        bool band = r == row, asked = band && col < 0 && ask_delete;
        if (band)
            gfx_slab(2, grid_y(v) + 1, PLAT_W - 4 - (n > GRID_ROWS ? 6 : 0), GRID_ROW - 2,
                     asked ? C_MUTE : C_ACCENT, 255);
        if (selecting && anchored) { /* the SELECTION, under the cells, as PATTERN's */
            int r0, r1, c0, c1;
            sel_rect(&r0, &r1, &c0, &c1);
            if (r >= r0 && r <= r1) {
                int a = c0 < left ? left : c0, b = c1 > left + CELLS - 1 ? left + CELLS - 1 : c1;
                if (c0 < 0)
                    gfx_fill(2, grid_y(v), PLAT_W - 4 - (n > GRID_ROWS ? 6 : 0), GRID_ROW, C_SEL, 255);
                else if (b >= a)
                    gfx_fill(GRID_CELL_X + (a - left) * CELL_W, grid_y(v), (b - a + 1) * CELL_W, GRID_ROW, C_SEL,
                             255);
            }
        }
        if (loop_has(r) && loop_state() == LOOP_SET) /* the loop: a cyan rule at the left edge, as PATTERN's */
            gfx_fill(0, grid_y(v) + (r == loop_from() ? 3 : 0), 3,
                     GRID_ROW - (r == loop_from() ? 3 : 0) - (r == loop_to() ? 3 : 0), C_TITLE, 255);
        if (r == play_row)
            grid_playing(v, band);
        grid_num(v, r, band, band && col < 0 && !asked,
                 r == play_row || (loop_has(r) && loop_state() == LOOP_SET) ? C_TITLE : C_TEXT);
        if (e->kind == ENTRY_REF) { /* a reference wears where it points: one cell, the whole lane */
            bool on = band && col >= 0;
            int x;
            if (on)
                grid_hollow(v, GRID_CELL_X, PLAT_W - 4 - (n > GRID_ROWS ? 6 : 0) - GRID_CELL_X);
            x = gfx_text(F_COND20, GRID_CELL_X + 6, grid_base(v),
                         e->ref_kind == REF_SECTION ? "REF SEC" : "REF", grid_ink(band, on, C_DIM));
            gfx_text(F_COND20, x + 8, grid_base(v), grid_hex2(s, e->ref_target), grid_ink(band, on, C_FX));
            continue;
        }
        for (int t = left; t < left + CELLS && t < tracks; t++) {
            uint8_t p = e->pattern[t];
            bool on = band && t == col;
            bool fresh = p < TRACK_MAX_PATTERNS && song_pattern_blank(song, t, p);
            /* a block with nothing in it yet wears C_FX, what the theme gives
             * to what is not written: the hand turned KNOB 3 to the end and
             * made it, and sees that it did (30/10) */
            uint16_t ink = !(p < TRACK_MAX_PATTERNS) || !grid_heard(t) ? C_DIM
                           : fresh                                     ? C_FX
                           : r == play_row                             ? C_TITLE
                                                                       : C_TEXT;
            grid_cell(v, GRID_CELL_X + (t - left) * CELL_W, CELL_W,
                      p < TRACK_MAX_PATTERNS ? grid_hex2(s, p) : p == PATTERN_HOLD ? "ON" : "--",
                      band, on, ink);
            if (p != PATTERN_NONE && e->level[t] < VOL_FULL) /* softer: a bar its length at the cell's foot */
                gfx_fill(GRID_CELL_X + (t - left) * CELL_W + 4, grid_y(v) + GRID_ROW - 5,
                         (CELL_W - 8) * e->level[t] / VOL_FULL + 1, 2, on ? C_TEXT : band ? C_ON_ACC : C_FX, 255);
        }
    }
    if (selecting && row >= top && row < top + GRID_ROWS) /* the cursor, over the SELECTION */
        gfx_frame_rect(col < 0 ? 2 : GRID_CELL_X + (col - left) * CELL_W, grid_y(row - top),
                       col < 0 ? GRID_CELL_X - 4 : CELL_W, GRID_ROW, C_TEXT, 255);
    if (selecting && !anchored && clip_rows) { /* where OP2 would put what was taken */
        int c0 = clip_cols < 0 || col < 0 ? 0 : col, nc = clip_cols < 0 ? tracks : clip_cols;
        int v0 = row - top, v1 = row + clip_rows - 1 - top;
        v1 = v1 >= GRID_ROWS ? GRID_ROWS - 1 : v1;
        if (v1 >= n - top)
            v1 = n - top - 1;
        c0 = c0 < left ? left : c0;
        if (c0 + nc > left + CELLS)
            nc = left + CELLS - c0;
        if (c0 + nc > tracks)
            nc = tracks - c0;
        if (v0 >= 0 && v0 < GRID_ROWS && nc > 0)
            gfx_frame_rect(GRID_CELL_X + (c0 - left) * CELL_W, grid_y(v0), nc * CELL_W,
                           grid_y(v1) + GRID_ROW - grid_y(v0), C_FX, 255);
    }
    grid_frame(top, n);
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
    choosing = picking = false;
    selecting = anchored = false;
    said = 0;
    settle = still = 0;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

void songview_cancel(void)
{
    if (choosing) /* a block being listened to goes back to what the cell held */
        ch_close(false);
    if (picking) /* a phrase being listened to is left where it was */
        cp_close(false);
    audition_end();
}

/* What can be pressed where the cursor is: the guide's glow (ui.h). */
static uint32_t usable(void)
{
    /* EDIT lights only while the SELECTION is locked on (ui.h, 30/10) */
    uint32_t u = 1u << PB_FX | 1u << PB_GLO | 1u << PB_HOME | 1u << PB_PLAY | 1u << PB_SAVE |
                 (selecting ? 1u << PB_EDIT : 0u);
    if (is_ref(row)) /* LFO deletes the row, SEL goes to what it plays */
        return u | (col < 0 ? 1u << PB_LFO : 0u) |
               (song_entry_resolve(song, row, 0) >= 0 ? 1u << PB_SEL : 0u);
    u |= 1u << PB_SEL;
    if (cell_instr() >= 0) /* the keys play: their octave */
        u |= 1u << PB_OCTDN | 1u << PB_OCTUP;
    if (col < 0)
        u |= 1u << PB_SEQ | 1u << PB_LFO;
    return u;
}

int songview_frame(const PlatInput *in, int *entry, int *track)
{
    uint32_t keys;
    uint32_t btns = in->btns & ~prev_btns;
    bool acted = btns || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3] ||
                 in->enc[PE_PRESET];
    int out = SONGVIEW_STAY;
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    keys = in->keys & ~prev_keys, prev_keys = in->keys;
    playing = player_playing();
    loop_keep(-1); /* a loop a phrase's steps carried is not this screen's */
    if (acted)
        said = 0;

    /* PRESETS, which this screen had no use for, opens CHOOSE PATTERN on the
     * cell under the cursor. KNOB 3 is untouched: it is the ear (30/10). */
    if (in->enc[PE_PRESET] && !choosing && !picking && !selecting && col >= 0) {
        audition_end(); /* the ear's block and a key held on the way in */
        settle = 0;
        if (trying)
            player_note_off(), trying = false;
        /* the cell's blocks, or -- on a reference -- the phrases it could
         * point at: the same knob, the list of what is under it (31/10) */
        if (is_ref(row))
            cp_open(), gfx_frame(cp_draw);
        else
            ch_open(), gfx_frame(ch_draw); /* the detent that opened it does not also walk it */
        return SONGVIEW_STAY;
    }
    if (choosing) {
        ch_frame(in, btns);
        play_row = playing ? player_entry() : -1;
        gfx_frame(choosing ? ch_draw : draw);
        return SONGVIEW_STAY;
    }
    if (picking) {
        cp_frame(in, btns);
        play_row = playing ? player_entry() : -1;
        gfx_frame(picking ? cp_draw : draw);
        return SONGVIEW_STAY;
    }

    /* DELETE asks; only a second LFO answers yes. */
    if (ask_delete && (in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3] ||
                       (btns && !PRESSED(PB_LFO))))
        ask_delete = false;

    row += in->enc[PE_SELECT] + in->enc[PE_K1]; /* KNOB 1 too: one hand (29/10) */
    row = row < 0 ? 0 : row >= song->arrange_len ? song->arrange_len - 1 : row;
    if (in->enc[PE_SELECT]) /* SELECT: the phrase as a whole, the cursor on its number (29/10) */
        col = -1;
    col += in->enc[PE_K2];
    {   /* a reference wears one cell where a phrase wears eight, so the
         * cursor has two places on it: its number, and the cell. Where the
         * cursor stands is the only truth (29/10), so the column is clamped
         * and not remembered: crossing a reference leaves the cursor on its
         * cell, and walking back out starts from the first track. */
        int last = is_ref(row) ? 0 : song->num_tracks - 1;
        col = col < -1 ? -1 : col > last ? last : col;
    }
    if (in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2]) /* the cursor left the cell the ear was on */
        audition_end(), settle = 0;

    /* KNOB 4 is how much, wherever the cursor is: the cell's level, the
     * phrase's length on its number, the reference's on either of its two
     * places (31/10). KNOB 3 beside it is what the thing IS. */
    if (in->enc[PE_K4] && !selecting && is_ref(row))
        turn_ref_steps(in->enc[PE_K4]);
    else if (in->enc[PE_K4] && !selecting && col < 0)
        turn_steps(in->enc[PE_K4]);
    else if (in->enc[PE_K4] && !selecting && entry_at(row)->pattern[col] != PATTERN_NONE)
        turn_level(in->enc[PE_K4]);
    else if (btns || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3])
        level_said = false;
    if (in->enc[PE_K3] && !selecting) {
        if (is_ref(row) && col >= 0) {
            turn_target(in->enc[PE_K3]); /* where it points */
        } else if (col < 0) {
            turn_kind(in->enc[PE_K3]); /* what the row is: blocks of its own, or a pointer */
        } else {
            bool song_on = playing && !audition; /* the song itself, not the ear's block */
            turn_block(in->enc[PE_K3]);
            if (!song_on) { /* at rest: what it lands on is heard, once the hand stops */
                audition_end();
                settle = SETTLE;
            }
        }
    }
    /* SEL goes in, wherever the cursor is: the phrase's SECTION on its
     * number, the block itself in PATTERN on a cell -- the panel of blocks
     * that stood between them is gone, KNOB 3 chooses and the ear says which
     * (30/10). An empty cell has no block to open. */
    if (PRESSED(PB_SEL) && is_ref(row)) {
        /* On a reference, the cran below it is what it plays: the phrase it
         * names, or the first of the section's. PRESETS is what changes it
         * -- SEL never opens a list, here or anywhere (31/10). */
        int at = song_entry_resolve(song, row, 0);
        if (at >= 0 && at < song->arrange_len) {
            audition_end();
            settle = 0;
            row = at;
            col = -1;
        }
    } else if (PRESSED(PB_SEL)) {
        *entry = row;
        *track = col < 0 ? 0 : col;
        if (col < 0)
            out = SONGVIEW_PHRASE;
        else if (entry_at(row)->pattern[col] < TRACK_MAX_PATTERNS)
            out = SONGVIEW_PATTERN;
    }
    if (PRESSED(PB_SEQ) && !is_ref(row) && col < 0)
        insert_phrase();
    /* EDIT: a quick tap locks the SELECTION of blocks on or off (at the
     * release), held it is the shift and the SELECTION is left as it was
     * (30/10). MONO, in either: the track alone, or muted (ui.h). */
    if (ui_shift(&shift, in, btns, keys, selecting, col, &said) && shift.frames < EDIT_TAP) {
        selecting = !selecting;
        anchored = selecting;
        an_row = row, an_col = col;
    }
    if (selecting || shift.held) {
        if (keys & 1u << UI_OP_COPY)
            sel_op(UI_OP_COPY);
        if (keys & 1u << UI_OP_PASTE)
            sel_op(UI_OP_PASTE);
        if (keys & 1u << UI_OP_CUT)
            sel_op(UI_OP_CUT);
        if (keys & 1u << UI_OP_CLEAR)
            sel_op(UI_OP_CLEAR);
        if (keys & 1u << UI_KEY_LOOP)
            sel_op(UI_KEY_LOOP);
        keys = 0; /* the keyboard is the commands' here: no note is played */
    }
    if (PRESSED(PB_LFO) && col < 0) {
        if (ask_delete)
            delete_phrase();
        ask_delete = !ask_delete;
    }
    if (PRESSED(PB_OCTDN) && kbd_oct > -3)
        kbd_oct--;
    if (PRESSED(PB_OCTUP) && kbd_oct < 3)
        kbd_oct++;
    /* The keys play the instrument of the track under the cursor and write
     * nothing: what a track sounds like, heard without opening its block,
     * and a line tried over the arrangement (30/10). While EDIT is down they
     * are commands, not notes (ui.h). */
    if (!in->keys && trying)
        player_note_off(), trying = false;
    if (!shift.held && !selecting) {
        int instr = cell_instr();
        for (int k = 0; k < PLAT_KEYS && instr >= 0; k++) {
            int n = edit_key_note(song, instr, kbd_oct, k);
            if (((keys >> k) & 1u) && n >= NOTE_MIN && n <= NOTE_MAX)
                player_note_on(instr, (uint8_t)n), trying = true;
        }
    }
    {   /* A MIDI keyboard plays that same instrument at its own pitch, and
         * is no command while EDIT is down (ui.h) */
        int instr = cell_instr();
        if (instr >= 0)
            ui_midi_play(in, instr);
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
        settle = 0;
        if (audition) /* the ear first: the song is what the next PLAY plays */
            audition_end();
        else if (playing) {
            /* STOP leaves the cursor where the song stopped, not where it was
             * when PLAY was pressed: the hand carries on from what it heard
             * last, and the view is already there (30/10) */
            int at = player_entry();
            player_stop();
            if (at >= 0 && at < song->arrange_len)
                row = at;
        } else {
            player_play_song(row, false);
        }
    }
    if (PRESSED(PB_FX))
        out = SONGVIEW_MIXER;
    if (PRESSED(PB_GLO)) /* GLObal: what holds for the whole song */
        out = SONGVIEW_PROJECT;
    if (PRESSED(PB_HOME) && selecting) /* a step back: out of the SELECTION first */
        selecting = anchored = false;
    else if (PRESSED(PB_HOME)) /* then HOME's root */
        out = SONGVIEW_BACK;

    if (settle > 0 && --settle == 0)
        audition_start();
    play_row = playing ? player_entry() : -1;
    still = btns || keys || in->keys || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2] || in->enc[PE_K3] ||
                    in->enc[PE_K4]
                ? 0
                : still < GRID_FOLLOW_IDLE ? still + 1 : still;
    if (play_row >= 0 && still >= GRID_FOLLOW_IDLE) /* autoscroll: the view goes with the phrase that plays */
        grid_follow_play(&top, play_row, song->arrange_len, GRID_ROWS);
    else
        grid_follow(&row, &top, song->arrange_len, GRID_ROWS);
    {
        int t = col < 0 ? 0 : col;
        grid_follow(&t, &left, song->num_tracks, CELLS);
    }
    ui_led_state(PB_PLAY, playing);
    ui_led_guide(usable());
    /* the SELECTION's commands: copy and cut on a stretched one, clear always,
     * paste once something was taken; MONO: lit alone, half muted */
    ui_led_mono(col, !selecting && !shift.held
                         ? 0u
                         : (anchored ? 1u << UI_OP_COPY | 1u << UI_OP_CUT : 0u) | 1u << UI_OP_CLEAR |
                               1u << UI_KEY_LOOP | 1u << UI_KEY_MONO | (clip_rows ? 1u << UI_OP_PASTE : 0u));
    gfx_frame(draw);
    if (out != SONGVIEW_STAY) { /* the screen is leaving: the ear's block and the hand's note with it */
        audition_end();
        loop_clear(); /* the phrases' loop is this screen's: the next counts steps */
        settle = 0;
        if (trying)
            player_note_off(), trying = false;
    }
    return out;
#undef PRESSED
}
