/* SPDX-License-Identifier: GPL-3.0-only */
#include "mixer.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "ui.h"

enum {
    NAV_Y = 2, NAV_H = 17,
    LEFT_Y = 49,
    RET_Y = 60, CELL_H = 14,
    TRACKS_Y = 75, ROWS_Y = 85, ROW_PITCH = 15,
    CELL_W = 57, CELL_PITCH = 59,
};
enum { AT_MUTE = 2, AT_SOLO = 3, AT_BACK = 6, AT_SAVE = 7, AT_UNDO = 8, AT_PLAY = 10 };

#define song (&app_song)

static int row; /* -1 the returns, 0.. a track */
static int entry;
static char back_word[12];
static uint32_t prev_btns, held;
static bool playing;
static int8_t saved;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* L18, C, R14: where the track sits, as a hand would say it. */
static char *pan_word(char *b, int pan)
{
    if (pan == 0) {
        b[0] = 'C';
        b[1] = '\0';
        return b;
    }
    b[0] = pan < 0 ? 'L' : 'R';
    ui_dec(b + 1, pan < 0 ? -pan : pan);
    return b;
}

static void turn(int k, int d)
{
    if (row == -1) {
        uint8_t *f[4] = { &song->reverb_size, &song->reverb_damp, &song->delay_time, &song->delay_fb };
        *f[k] = (uint8_t)clampi(*f[k] + d, 0, k == 2 ? 96 : 127);
    } else {
        TrackInit *t = &song->track_init[row];
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

/* One cell: a word on the left, its value on the right, and a line that
 * draws the value -- its length for a level, a mark for a position. */
static void cell(int k, int y, bool cur, const char *label, int font, uint16_t label_ink,
                 const char *value, bool zero, int bar, int mark)
{
    int x = APP_X0 + k * CELL_PITCH, base = font == F_MONO8 ? y + 9 : y + 10;
    gfx_slab(x, y, CELL_W, CELL_H, cur ? C_ACCENT : C_TEXT, cur ? 255 : 18);
    gfx_text(font, x + 3, base, label, cur ? C_ON_ACC : label_ink);
    gfx_text(F_MONO8, x + CELL_W - 3 - gfx_text_w(F_MONO8, value), y + 9, value,
             cur ? C_ON_ACC : zero ? C_DIM : C_TEXT);
    if (mark >= 0)
        gfx_fill(x + 2 + mark, y + CELL_H - 3, 3, 2, cur ? C_ON_ACC : C_TITLE, 255);
    else if (bar > 0)
        gfx_fill(x + 2, y + CELL_H - 3, bar, 2, cur ? C_ON_ACC : C_DIM, cur ? 140 : 255);
}

static void draw(void)
{
    static const char *const base_words[12] = { "", "", "MUTE", "SOLO", "", "", "", "SAVE", "UNDO", "", "PLAY", "" };
    const char *w[12];
    uint8_t st[12];
    char a[12], back[20] = "\xE2\x80\xB9 ";
    int x, i = 4;
    bool cur = row == -1;

    for (const char *p = back_word; *p; p++)
        back[i++] = *p;
    back[i] = '\0';

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    x = gfx_text(F_COND12, APP_X0, NAV_Y + 13, back, C_TITLE);
    gfx_text(F_COND12, x + 6, NAV_Y + 13, app_name, C_TEXT);
    if (saved) {
        const char *m = saved > 0 ? "SAVED" : "NOT SAVED";
        gfx_text(F_COND9, APP_X0 + APP_INNER - gfx_text_w(F_COND9, m), NAV_Y + 12, m,
                 saved > 0 ? C_ACCENT : C_MUTE);
    }
    gfx_fill(APP_X0, NAV_Y + NAV_H, APP_INNER, 1, C_DIM, 77);

    if (cur) {
        ui_card(0, "ROOM", ui_dec(a, song->reverb_size), 0);
        ui_card(1, "DAMP", ui_dec(a, song->reverb_damp), 0);
        ui_card(2, "DELAY", ui_dec(a, song->delay_time), "ticks");
        ui_card(3, "FEEDBACK", ui_dec(a, song->delay_fb), 0);
    } else {
        const TrackInit *t = &song->track_init[row];
        ui_card(0, "VOLUME", ui_dec(a, t->volume), 0);
        ui_card(1, "PAN", pan_word(a, t->pan), 0);
        ui_card(2, "REVERB", ui_dec(a, t->send), 0);
        ui_card(3, "DELAY", ui_dec(a, t->echo), 0);
    }
    ui_cards_rule();

    x = gfx_text(F_MONO8, APP_X0, LEFT_Y + 8, "SELECT", C_DIM);
    gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "TRACK", C_TEXT);

    /* The two returns: the room, and the delay. They belong to the song. */
    cell(0, RET_Y, cur, "ROOM", F_MONO8, C_DIM, ui_dec(a, song->reverb_size), false, song->reverb_size * 51 / 127, -1);
    cell(1, RET_Y, cur, "DAMP", F_MONO8, C_DIM, ui_dec(a, song->reverb_damp), false, song->reverb_damp * 51 / 127, -1);
    cell(2, RET_Y, cur, "DELAY", F_MONO8, C_DIM, ui_dec(a, song->delay_time), false, song->delay_time * 51 / 96, -1);
    cell(3, RET_Y, cur, "FB", F_MONO8, C_DIM, ui_dec(a, song->delay_fb), false, song->delay_fb * 51 / 127, -1);

    gfx_text(F_MONO8, APP_X0 + 2, TRACKS_Y + 8, "TRACKS", C_DIM);
    for (int t = 0; t < song->num_tracks; t++) {
        const TrackInit *ti = &song->track_init[t];
        int y = ROWS_Y + t * ROW_PITCH;
        bool c = row == t, muted = player_muted(t), solo = player_soloed(t);
        char tag[3] = { 'T', (char)('1' + t), '\0' };

        /* a muted track's number is red, a soloed one's green: monitoring, not the song */
        cell(0, y, c, tag, F_COND11, muted ? C_MUTE : solo ? C_ACCENT : C_TEXT, ui_dec(a, ti->volume),
             ti->volume == 0, ti->volume * 51 / 127, -1);
        cell(1, y, c, "PAN", F_MONO8, C_DIM, pan_word(a, ti->pan), false, 0, (ti->pan + 64) * 49 / 127);
        cell(2, y, c, "REV", F_MONO8, C_DIM, ui_dec(a, ti->send), ti->send == 0, ti->send * 51 / 127, -1);
        cell(3, y, c, "ECH", F_MONO8, C_DIM, ui_dec(a, ti->echo), ti->echo == 0, ti->echo * 51 / 127, -1);
    }

    for (int b = 0; b < 12; b++) {
        w[b] = b == AT_BACK ? back : b == AT_PLAY && playing ? "STOP" : base_words[b];
        st[b] = b == AT_BACK ? UI_BTN_BACK : !w[b][0] ? UI_BTN_NONE : UI_BTN_ON;
        if (b == AT_UNDO)
            st[b] = UI_BTN_OFF; /* not written yet */
        if ((b == AT_MUTE || b == AT_SOLO) && row < 0)
            st[b] = UI_BTN_OFF; /* the returns are not a track */
        if (b == AT_MUTE && row >= 0 && player_muted(row))
            st[b] = UI_BTN_ASK; /* red while this track is cut */
        if (b == AT_SOLO && row >= 0 && player_soloed(row))
            st[b] = UI_BTN_LIVE;
        if (b == AT_PLAY && playing)
            st[b] = UI_BTN_LIVE;
    }
    ui_btn_map(w, st, held);
}

void mixer_open(const char *back, int e)
{
    int i = 0;
    for (; back[i] && i < (int)sizeof(back_word) - 1; i++)
        back_word[i] = back[i];
    back_word[i] = '\0';
    entry = e;
    row = row >= app_song.num_tracks ? app_song.num_tracks - 1 : row;
    saved = 0;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

bool mixer_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
#define PRESSED(at) ((btns >> APP_BTN_AT[at]) & 1u)

    prev_btns = in->btns;
    held = in->btns;
    playing = player_playing();
    if (btns || in->enc[PE_SELECT])
        saved = 0;

    row = clampi(row + in->enc[PE_SELECT], -1, song->num_tracks - 1);
    for (int k = 0; k < 4; k++)
        if (in->enc[PE_K1 + k]) {
            turn(k, in->enc[PE_K1 + k]);
            saved = 0;
        }
    if (PRESSED(AT_MUTE) && row >= 0)
        player_toggle_mute(row);
    if (PRESSED(AT_SOLO) && row >= 0)
        player_toggle_solo(row);
    if (PRESSED(AT_SAVE))
        saved = app_save() == 0 ? 1 : -1;
    if (PRESSED(AT_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_song(entry, false);
    }
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(draw);
    return !PRESSED(AT_BACK);
#undef PRESSED
}
