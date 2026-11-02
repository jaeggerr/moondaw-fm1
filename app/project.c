/* SPDX-License-Identifier: GPL-3.0-only */
#include "project.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "namer.h"
#include "ui.h"
#include "undo.h"

/* PROJECT, as validated on the panel on 28/10 (NOTES.md): one setting a
 * row, its name in grey at the left, its value at the right.
 *
 *   SELECT, KNOB 1  the setting     KNOB 3  its value
 *   NAME    SEL opens it: one cell a letter, KNOB 2 the place, KNOB 3 the letter
 *           (space, A-Z, 0-9, -); SEL keeps it, HOME goes back without it
 *   KEY     the tonic and major or minor, C MAJOR to B MINOR: what ARP on PATTERN
 *           suggests chords from (the file's key); nothing else listens to it
 *   METER   beats in a bar, written x/4 (the file's beats_per_bar)
 *   STEP    what a step is worth as a note: 1/16 at 4 steps a beat, 1/12 at 3
 *           (the file's steps_per_beat)
 *   HOME    back where it was opened from     SEQ  SONG     FX  the MIXER
 *   ALGO    UNDO left, REDO right     SAVE  as printed */
enum { S_NAME, S_BPM, S_SPEED, S_METRO, S_KEY, S_SWING, S_METER, S_STEP, S_TICKS, S_TRACKS, SETTINGS };
enum { SPEED_STEP = 5 };
enum { LABEL_X = 14, VALUE_X = 120 };

#define song (&app_song)

static const char *const label[SETTINGS] = { "NAME",  "BPM",   "SPEED", "METRONOME", "KEY",
                                             "SWING", "METER", "STEP",  "TICKS",     "TRACKS" };

/* SPEED and METRONOME are not the project: they are how the machine is
 * listening to it, and they are not written to the file (player_init puts
 * both back when a project opens). Nothing marks them off -- a rule was
 * tried and taken out (02/11) -- they sit under BPM because that is where
 * the hand goes. They are reachable here so that the gesture is not the
 * only way in, and the foot then says what the gesture is, which is how
 * one stops needing this screen (his design). */
static const char *const gesture[SETTINGS] = { [S_SPEED] = "EDIT + SELECT", [S_METRO] = "EDIT + REC" };
static const char *const gesture_say[SETTINGS] = { [S_SPEED] = "slower, faster", [S_METRO] = "on, off" };
static int row, top;
static bool naming;
static const char *said;
static uint32_t prev_btns;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void turn(int k, int d)
{
    switch (k) {
    case S_BPM:
        undo_bytes(&song->bpm, sizeof song->bpm);
        song->bpm = (uint16_t)clampi(song->bpm + d, BPM_MIN, BPM_MAX);
        break;
    case S_KEY: {
        int v = clampi(song->key_root * 2 + song->key_mode + d, 0, KEY_ROOTS * 2 - 1);
        undo_bytes(&song->key_root, 2); /* and key_mode, the next byte */
        song->key_root = (uint8_t)(v / 2);
        song->key_mode = (uint8_t)(v % 2);
        break;
    }
    case S_SWING:
        undo_bytes(&song->swing, 1);
        song->swing = (uint8_t)clampi(song->swing + d, SWING_MIN, SWING_MAX);
        break;
    case S_METER:
        undo_bytes(&song->beats_per_bar, 1);
        song->beats_per_bar = (uint8_t)clampi(song->beats_per_bar + d, 1, METER_MAX);
        break;
    case S_STEP:
        undo_bytes(&song->steps_per_beat, 1);
        song->steps_per_beat = (uint8_t)clampi(song->steps_per_beat + d, 1, METER_MAX);
        break;
    case S_TICKS:
        undo_bytes(&song->ticks_per_step, 1);
        song->ticks_per_step = (uint8_t)clampi(song->ticks_per_step + d, 1, TICKS_MAX);
        break;
    case S_TRACKS:
        undo_bytes(&song->num_tracks, 1);
        song->num_tracks = (uint8_t)clampi(song->num_tracks + d, 1, SONG_MAX_TRACKS);
        break;
    /* Neither is the song: no UNDO, and the project is not dirtied by them. */
    case S_SPEED:
        player_set_speed(player_speed() + d * SPEED_STEP);
        return;
    case S_METRO:
        if (player_metro() != (d > 0))
            player_toggle_metro();
        return;
    default:
        return;
    }
    app_mark_dirty();
}

static void value_of(int k, char *s)
{
    char *p;
    switch (k) {
    case S_NAME:
        for (p = app_name; *p; p++)
            *s++ = *p;
        *s = 0;
        break;
    case S_BPM:
        ui_dec(s, song->bpm);
        break;
    case S_KEY: {
        const char *q;
        for (q = key_root_name(song->key_root); *q; q++)
            *s++ = *q;
        for (q = song->key_mode == KEY_MINOR ? " MINOR" : " MAJOR"; *q; q++)
            *s++ = *q;
        *s = 0;
        break;
    }
    case S_SWING:
        ui_dec(s, song->swing);
        for (p = s; *p; p++)
            ;
        p[0] = ' ', p[1] = '%', p[2] = 0;
        break;
    case S_METER: /* beats in a bar, over the quarter */
        ui_dec(s, song->beats_per_bar);
        for (p = s; *p; p++)
            ;
        p[0] = '/', p[1] = '4', p[2] = 0;
        break;
    case S_STEP: /* a step is 1 / (4 x steps a beat) of a whole note */
        s[0] = '1', s[1] = '/';
        ui_dec(s + 2, 4 * song->steps_per_beat);
        break;
    case S_TICKS:
        ui_dec(s, song->ticks_per_step);
        break;
    case S_SPEED:
        ui_dec(s, player_speed());
        for (p = s; *p; p++)
            ;
        p[0] = ' ', p[1] = '%', p[2] = 0;
        break;
    case S_METRO: {
        const char *q;
        for (q = player_metro() ? "ON" : "OFF"; *q; q++)
            *s++ = *q;
        *s = 0;
        break;
    }
    default:
        ui_dec(s, song->num_tracks);
    }
}

/* Eight rows, or seven while the foot is saying a gesture. */
static int shown_rows(void) { return GRID_ROWS - (gesture[row] ? 1 : 0); }

/* ---------------------------------------------------------- drawing --- */
static void draw(void)
{
    char s[STORE_NAME + 4], n[12];
    int lost = app_not_carried();
    int bar = SETTINGS > shown_rows() ? 6 : 0; /* the scroll bar's own six pixels */
    grid_clear();
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else if (lost)
        /* What the file had that this machine does not carry, and that a
         * SAVE from here would write the song without. It stands in the
         * corner as long as the song is the one that arrived short: the
         * screen of the project as a whole is where it belongs, the old
         * SONG header having been replaced by one with no room for it. */
        grid_corner(ui_dec(n, lost), C_FX, "NOT CARRIED", C_FX);
    else
        grid_corner("PROJECT", C_DIM, 0, 0);
    if (naming) {
        namer_draw();
        return;
    }
    for (int v = 0; v < shown_rows() && top + v < SETTINGS; v++) {
        int k = top + v;
        (void)0;
        bool here = k == row;
        if (here) {
            grid_band(v, PLAT_W - 4 - bar);
            grid_hollow(v, VALUE_X - 6, PLAT_W - VALUE_X - bar);
        }
        gfx_text(F_COND20, LABEL_X, grid_base(v), label[k], here ? C_ON_ACC : C_DIM);
        value_of(k, s);
        gfx_text(F_COND20, VALUE_X, grid_base(v), s, here ? C_ACCENT : C_TEXT);
    }
    grid_bar_rows(top, SETTINGS, shown_rows());
    if (gesture[row]) { /* the fast way to the row one is on */
        int y = PLAT_H - GRID_ROW, x;
        gfx_fill(0, y, PLAT_W, 1, C_TRACK, 255);
        x = gfx_text(F_COND14, 8, y + 19, gesture[row], C_TITLE);
        gfx_text(F_COND12, x + 10, y + 19, gesture_say[row], C_DIM);
    }
}

/* -------------------------------------------------------------- api --- */
void project_open(void)
{
    row = top = 0;
    naming = false;
    said = 0;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

int project_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    if (btns || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3])
        said = 0;

    if (naming) {
        int r = namer_frame(in, btns);
        if (r == NAMER_KEPT)
            app_mark_dirty(); /* the next SAVE writes it under this name, in its place */
        naming = r == NAMER_TYPING;
        ui_led_guide(0); /* the namer */
        gfx_frame(draw);
        return PROJECT_STAY;
    }

    row = clampi(row + in->enc[PE_SELECT] + in->enc[PE_K1], 0, SETTINGS - 1);
    grid_follow(&row, &top, SETTINGS, shown_rows());
    if (in->enc[PE_K3])
        turn(row, in->enc[PE_K3]);
    if (PRESSED(PB_SEL) && row == S_NAME) {
        namer_open(app_name, STORE_NAME);
        naming = true;
    }
    undo_turn(in->enc[PE_ALGO], &said);
    if (PRESSED(PB_SAVE))
        said = ui_save_word(app_save());
    if (PRESSED(PB_PLAY)) {
        if (player_playing())
            player_stop();
        else
            player_play_song(0, false);
    }
    ui_led_state(PB_PLAY, player_playing());
    ui_led_guide(1u << PB_FX | 1u << PB_SEQ); /* the MIXER, SONG */
    gfx_frame(draw);
    if (PRESSED(PB_HOME))
        return PROJECT_BACK;
    if (PRESSED(PB_SEQ)) /* SEQ: SONG, wherever this came from (30/10) */
        return PROJECT_SONG;
    return PRESSED(PB_FX) ? PROJECT_MIXER : PROJECT_STAY;
#undef PRESSED
}
