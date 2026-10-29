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
 *   SELECT  the setting     KNOB 2  its value
 *   NAME    SEL opens it: one cell a letter, KNOB 1 the place, KNOB 2 the letter
 *           (space, A-Z, 0-9, -); SEL keeps it, GLO goes back without it
 *   METER   beats in a bar, written x/4 (the file's beats_per_bar)
 *   STEP    what a step is worth as a note: 1/16 at 4 steps a beat, 1/12 at 3
 *           (the file's steps_per_beat)
 *   GLO     back where it was opened from     FX  the MIXER
 *   ARP     UNDO     SAVE  as printed */
enum { S_NAME, S_BPM, S_SWING, S_METER, S_STEP, S_TICKS, S_TRACKS, SETTINGS };
enum { LABEL_X = 14, VALUE_X = 120 };

#define song (&app_song)

static const char *const label[SETTINGS] = { "NAME", "BPM", "SWING", "METER", "STEP", "TICKS", "TRACKS" };
static int row;
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
    default:
        ui_dec(s, song->num_tracks);
    }
}

/* ---------------------------------------------------------- drawing --- */
static void draw(void)
{
    char s[STORE_NAME + 4];
    grid_clear();
    if (said)
        grid_corner(said, said[0] == 'S' ? C_ACCENT : C_MUTE, 0, 0);
    else
        grid_corner("PROJECT", C_DIM, 0, 0);
    if (naming) {
        namer_draw();
        return;
    }
    for (int k = 0; k < SETTINGS; k++) {
        bool here = k == row;
        if (here) {
            grid_band(k, PLAT_W - 4);
            grid_hollow(k, VALUE_X - 6, PLAT_W - VALUE_X);
        }
        gfx_text(F_COND20, LABEL_X, grid_base(k), label[k], here ? C_ON_ACC : C_DIM);
        value_of(k, s);
        gfx_text(F_COND20, VALUE_X, grid_base(k), s, here ? C_ACCENT : C_TEXT);
    }
}

/* -------------------------------------------------------------- api --- */
void project_open(void)
{
    row = 0;
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
    if (btns || in->enc[PE_SELECT] || in->enc[PE_K1] || in->enc[PE_K2])
        said = 0;

    if (naming) {
        int r = namer_frame(in, btns);
        if (r == NAMER_KEPT)
            app_mark_dirty(); /* the next SAVE writes it under this name, in its place */
        naming = r == NAMER_TYPING;
        gfx_frame(draw);
        return PROJECT_STAY;
    }

    row = clampi(row + in->enc[PE_SELECT], 0, SETTINGS - 1);
    if (in->enc[PE_K2])
        turn(row, in->enc[PE_K2]);
    if (PRESSED(PB_SEL) && row == S_NAME) {
        namer_open(app_name, STORE_NAME);
        naming = true;
    }
    if (PRESSED(PB_ARP))
        undo_pop();
    if (PRESSED(PB_SAVE))
        said = app_save() == 0 ? "SAVED" : "NOT SAVED";
    if (PRESSED(PB_PLAY)) {
        if (player_playing())
            player_stop();
        else
            player_play_song(0, false);
    }
    plat_led_btn(PB_PLAY, player_playing());
    gfx_frame(draw);
    return PRESSED(PB_GLO) ? PROJECT_BACK : PRESSED(PB_FX) ? PROJECT_MIXER : PROJECT_STAY;
#undef PRESSED
}
