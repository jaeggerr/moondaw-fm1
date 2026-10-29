/* SPDX-License-Identifier: GPL-3.0-only */
#include "mixer.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "ui.h"
#include "undo.h"

/* MIXER, as validated on the panel on 28/10 (NOTES.md), reached by FX:
 * one row a track, VOL PAN REV ECH as the file writes them. Above track
 * 1, out of sight until SELECT goes up, the row of the two returns, ROOM
 * DAMP TIME FB -- the column names change with the row.
 *
 *   SELECT, KNOB 1  the row (SELECT onto its number)     KNOB 2  the column; left of the first, the track's number
 *   KNOB 3  the value; on a track's number: normal -> MUTE -> SOLO (monitoring,
 *           never in the file)
 *   HOME    back where it was opened from     EDIT, GLO  the PROJECT
 *   ALGO    UNDO left, REDO right     SAVE  PLAY  as printed */
enum { CELL_W = 45, COLS = 4 };

#define song (&app_song)

static int row, col, top, entry; /* row -1: the returns; col -1: the number */
static const char *said;
static uint32_t prev_btns;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int nrows(void) { return song->num_tracks + 1; }

static int state(int t) { return player_soloed(t) ? 2 : player_muted(t) ? 1 : 0; }

static bool any_solo(void)
{
    for (int t = 0; t < song->num_tracks; t++)
        if (player_soloed(t))
            return true;
    return false;
}

/* KNOB 3 on a track's number: normal, MUTE, SOLO, one at a time. */
static void turn_state(int t, int d)
{
    int s = clampi(state(t) + d, 0, 2);
    if ((s == 1) != player_muted(t))
        player_toggle_mute(t);
    if ((s == 2) != player_soloed(t))
        player_toggle_solo(t);
}

static void turn(int k, int d)
{
    if (row < 0) {
        uint8_t *f[COLS] = { &song->reverb_size, &song->reverb_damp, &song->delay_time, &song->delay_fb };
        undo_bytes(f[k], 1);
        *f[k] = (uint8_t)clampi(*f[k] + d, 0, k == 2 ? 96 : 127);
    } else {
        TrackInit *t = &song->track_init[row];
        undo_bytes(k == 0 ? (void *)&t->volume : k == 1 ? (void *)&t->pan : k == 2 ? (void *)&t->send : (void *)&t->echo, 1);
        if (k == 0)
            t->volume = (uint8_t)clampi(t->volume + d, 0, 127);
        else if (k == 1)
            t->pan = (int8_t)clampi(t->pan + d, -64, 63);
        else if (k == 2)
            t->send = (uint8_t)clampi(t->send + d, 0, 127);
        else
            t->echo = (uint8_t)clampi(t->echo + d, 0, 127);
    }
    app_mark_dirty();
}

/* ---------------------------------------------------------- drawing --- */
static void draw(void)
{
    static const char *const track_head[COLS] = { "VOL", "PAN", "REV", "ECH" };
    static const char *const fx_head[COLS] = { "ROOM", "DAMP", "TIME", "FB" };
    char s[8];

    grid_clear();
    if (said)
        grid_corner(said, grid_said_ink(said), 0, 0);
    else
        grid_corner("MIXER", C_DIM, 0, 0);
    for (int c = 0; c < COLS; c++)
        grid_head(GRID_CELL_X + c * CELL_W, CELL_W, row < 0 ? fx_head[c] : track_head[c], c == col);
    for (int v = 0; v < GRID_ROWS && top + v < nrows(); v++) {
        int r = top + v - 1, base = grid_base(v);
        bool band = r == row, on_num = band && col < 0;
        bool silent = r >= 0 && (player_muted(r) || (any_solo() && !player_soloed(r)));
        uint16_t num = r < 0 || (r >= 0 && player_soloed(r)) ? C_TITLE : player_muted(r) ? C_MUTE : C_TEXT;

        if (band)
            grid_band(v, PLAT_W - 4 - (nrows() > GRID_ROWS ? 6 : 0));
        if (on_num)
            grid_hollow(v, GRID_NUM_X - 7, GRID_CELL_X - GRID_NUM_X + 3);
        num = grid_ink(band, on_num, num);
        if (r < 0) {
            gfx_text(F_COND20, GRID_NUM_X, base, "FX", num);
        } else {
            int x;
            s[0] = (char)('1' + r), s[1] = 0;
            x = gfx_text(F_COND20, GRID_NUM_X, base, s, num);
            if (state(r))
                gfx_text(F_COND14, x + 5, base, state(r) == 1 ? "M" : "S", num);
        }
        for (int c = 0; c < COLS; c++) {
            const TrackInit *t = r >= 0 ? &song->track_init[r] : 0;
            int val = r < 0 ? (c == 0 ? song->reverb_size : c == 1 ? song->reverb_damp
                               : c == 2 ? song->delay_time : song->delay_fb)
                      : c == 0 ? t->volume : c == 1 ? t->pan : c == 2 ? t->send : t->echo;
            if (r >= 0 && c == 1) { /* pan: C, L18, R18 */
                s[0] = val < 0 ? 'L' : val > 0 ? 'R' : 'C', s[1] = 0;
                if (val)
                    ui_dec(s + 1, val < 0 ? -val : val);
            } else {
                ui_dec(s, val);
            }
            grid_cell(v, GRID_CELL_X + c * CELL_W, CELL_W, s, band, band && c == col,
                      silent || (r >= 0 && c >= 2 && !val) ? C_DIM : C_TEXT);
        }
    }
    grid_frame(top, nrows());
}

/* -------------------------------------------------------------- api --- */
void mixer_open(const char *back, int e)
{
    (void)back;
    entry = e;
    row = col = 0;
    top = 1; /* the returns' row is above, out of sight */
    said = 0;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

int mixer_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    int d, vis;
#define PRESSED(b) ((btns >> (b)) & 1u)

    prev_btns = in->btns;
    if (btns || in->enc[PE_SELECT] || in->enc[PE_K2] || in->enc[PE_K1] || in->enc[PE_K3])
        said = 0;

    row = clampi(row + in->enc[PE_SELECT] + in->enc[PE_K1], -1, song->num_tracks - 1);
    if (in->enc[PE_SELECT]) /* SELECT: the track as a whole, the cursor on its number (29/10) */
        col = -1;
    col = clampi(col + in->enc[PE_K2], -1, COLS - 1);
    if ((d = in->enc[PE_K3]) != 0) {
        if (col >= 0)
            turn(col, d);
        else if (row >= 0)
            turn_state(row, d);
    }
    undo_turn(in->enc[PE_ALGO], &said);
    if (PRESSED(PB_SAVE))
        said = app_save() == 0 ? "SAVED" : "NOT SAVED";
    if (PRESSED(PB_PLAY)) {
        if (player_playing())
            player_stop();
        else
            player_play_song(entry, false);
    }

    vis = row + 1; /* rows as drawn: the returns first */
    grid_follow(&vis, &top, nrows(), GRID_ROWS);
    ui_led_state(PB_PLAY, player_playing());
    ui_led_guide(1u << PB_EDIT | 1u << PB_GLO); /* the PROJECT */
    gfx_frame(draw);
    return PRESSED(PB_HOME) ? MIXER_BACK : PRESSED(PB_EDIT) || PRESSED(PB_GLO) ? MIXER_PROJECT : MIXER_STAY;
#undef PRESSED
}
