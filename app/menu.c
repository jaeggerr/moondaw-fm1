/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "ui.h"

/* HOME, as validated on the panel on 28/10 (NOTES.md): the projects by name
 * only, CONDENSED 20, eight rows; the open one marked by a bar at its left
 * and an amber dot while it has unsaved changes; + NEW last; the version
 * small at the bottom. SELECT moves, SEL opens, PLAY plays the project
 * under the cursor (opening it if it is not the open one). */
enum { ROW = 28, ROWS = 8, TEXT_X = 12 };

#ifndef MOONDAW_VERSION
#define MOONDAW_VERSION "dev"
#endif

/* The rows, read once when the screen opens and after each change: a
 * frame is drawn fifteen times and must not go to the flash. A song in
 * memory that has no place in flash yet (the built-in one, a new one) has
 * a row too, slot -1, among the others by name. */
static struct {
    int8_t slot;
    char name[STORE_NAME + 1];
} rows[APP_SONG_SLOTS + 1];
static int nrows, open_row;
static int cur, top, asking, pending; /* pending: the row the question is about */
enum { ASK_NONE, ASK_OPEN, ASK_PLAY };   /* what the question leads to once answered */
static uint32_t prev_btns;
static int cpu, peak; /* the audio interrupt's share of its time, and its worst since PLAY */

static bool before(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a < (unsigned char)*b;
}

static void refresh(void)
{
    uint32_t bytes;
    nrows = 0;
    for (int s = 0; s < APP_SONG_SLOTS; s++)
        if (store_info(s, rows[nrows].name, &bytes) == STORE_OK)
            rows[nrows++].slot = (int8_t)s;
    if (app_slot < 0) {
        rows[nrows].slot = -1;
        for (int i = 0; (rows[nrows].name[i] = app_name[i]) != 0; i++)
            ;
        nrows++;
    }
    /* by name: the slots are where things happened to land */
    for (int i = 1; i < nrows; i++)
        for (int j = i; j > 0 && before(rows[j].name, rows[j - 1].name); j--) {
            __typeof__(rows[0]) t = rows[j];
            rows[j] = rows[j - 1];
            rows[j - 1] = t;
        }
    open_row = -1;
    for (int i = 0; i < nrows && open_row < 0; i++)
        if (rows[i].slot == app_slot) /* at most one row is -1: the song in memory */
            open_row = i;
}

static void scroll_to(int row, bool centre)
{
    int count = nrows + 1, last = count > ROWS ? count - ROWS : 0;
    cur = row;
    if (centre)
        top = cur - ROWS / 2;
    else if (cur < top)
        top = cur;
    else if (cur >= top + ROWS)
        top = cur - ROWS + 1;
    top = top < 0 ? 0 : top > last ? last : top;
}

void menu_open(void)
{
    refresh();
    scroll_to(open_row >= 0 ? open_row : 0, true);
    asking = ASK_NONE;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

/* ---------------------------------------------------------- drawing --- */
static void draw_question(void)
{
    const char *name = pending < nrows ? rows[pending].name : "a new project";
    int y = 34;
    gfx_text(F_COND20, TEXT_X, y, app_name, C_FX);
    gfx_text(F_COND20, TEXT_X, y += ROW, "is not saved.", C_TEXT);
    gfx_text(F_COND20, TEXT_X, y += 2 * ROW, "SAVE  keep the changes", C_TEXT);
    gfx_text(F_COND20, TEXT_X, y += ROW, "SEL  lose them", C_TEXT);
    gfx_text(F_COND20, TEXT_X, y += ROW, "anything else: stay", C_DIM);
    {
        const char *then = asking == ASK_PLAY ? "then plays " : "then opens ";
        gfx_text(F_COND12, TEXT_X, y += 2 * ROW - 8, then, C_DIM);
        gfx_text(F_COND12, TEXT_X + gfx_text_w(F_COND12, then), y, name, C_DIM);
    }
}

static void draw(void)
{
    int count = nrows + 1; /* + NEW */

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    if (asking) {
        draw_question();
        return;
    }
    for (int r = 0; r < ROWS && top + r < count; r++) {
        int i = top + r, y = r * ROW, base = y + 2 + FONTS[F_COND20].ascent, x;
        bool here = i == cur;
        uint16_t c = here ? C_ON_ACC : i < nrows ? C_TEXT : C_DIM;
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4 - (count > ROWS ? 6 : 0), ROW - 2, C_ACCENT, 255);
        if (i == open_row)
            gfx_fill(4, y + 6, 3, ROW - 12, here ? C_ON_ACC : C_TITLE, 255);
        x = gfx_text(F_COND20, TEXT_X, base, i < nrows ? rows[i].name : "+ NEW", c);
        if (i == open_row && app_dirty)
            gfx_fill(x + 6, base - 9, 7, 7, here ? C_ON_ACC : C_FX, 255);
    }
    if (count > ROWS) { /* where the screen is in the list */
        int h = ROWS * ROW * ROWS / count, y = ROWS * ROW * top / count;
        gfx_fill(PLAT_W - 4, 0, 2, ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
    if (player_playing() && player_clock) {
        /* the worst block's time, stage by stage, in microseconds: O the oscillators,
         * V the voice, C the filters and mix, R the reverb, E the delay, T the tick, W the
         * wait for the second core's share */
        static const char tag[PROF_N] = { 'T', 'O', 'V', 'C', 'R', 'E', 'W' };
        static const uint8_t order[PROF_N] = { PROF_OSC, PROF_VOX, PROF_CHAIN, PROF_REV, PROF_ECHO, PROF_TICK, PROF_WAIT };
        uint32_t p[PROF_N + 1];
        char t[8];
        int x = 4;
        player_profile(p);
        for (int i = 0; i < PROF_N; i++) {
            t[0] = tag[order[i]];
            ui_dec(t + 1, (int)p[order[i]]);
            x = gfx_text(F_COND12, x, PLAT_H - 4, t, C_TEXT) + 4;
        }
    } else {
        gfx_text(F_COND12, TEXT_X, PLAT_H - 4, "MOONDAW " MOONDAW_VERSION, C_DIM);
    }
    if (cpu >= 0) { /* what the song costs the chip: the worst block held while it plays */
        char b[8];
        int x = PLAT_W - 8 - gfx_text_w(F_COND12, ui_dec(b, peak));
        gfx_text(F_COND12, x, PLAT_H - 4, b, peak > 90 ? C_MUTE : C_TEXT);
        gfx_text(F_COND12, x - gfx_text_w(F_COND12, "MAX ") - 2, PLAT_H - 4, "MAX", C_DIM);
    }
}

/* -------------------------------------------------------------- api --- */
/* Puts what `row` names in memory: a song from flash, or a new one. */
static void load(int row)
{
    if (row == nrows)
        app_new();
    else if (rows[row].slot >= 0)
        app_open_slot(rows[row].slot);
}

/* After loading for PLAY: it plays from the top, and the hand stays here. */
static void play(void)
{
    player_play_song(0, false);
}

bool menu_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool open_it = false;

    prev_btns = in->btns;
    if (asking) {
        /* The question's two buttons answer it; anything else is "stay". */
        int then = asking;
        bool answered = false;
        if (btns & 1u << PB_SAVE)
            answered = app_save() == 0;
        else if (btns & 1u << PB_SEL)
            answered = true;
        else if (!btns && !in->enc[PE_SELECT]) {
            gfx_frame(draw);
            return false;
        }
        asking = ASK_NONE; /* "stay": the list as it was, the cursor where it was */
        if (answered) {
            load(pending);
            if (then == ASK_PLAY)
                play();
            else
                open_it = true;
        }
        gfx_frame(draw);
        return open_it;
    }

    scroll_to(cur + in->enc[PE_SELECT] < 0 ? 0 : cur + in->enc[PE_SELECT] > nrows ? nrows
              : cur + in->enc[PE_SELECT], false);
    if (btns & 1u << PB_SEL) {
        if (cur == open_row) {
            open_it = true;
        } else if (app_dirty) {
            pending = cur;
            asking = ASK_OPEN;
        } else {
            load(cur);
            open_it = true;
        }
    }
    /* PLAY: the project under the cursor. One song fits in memory, so
     * hearing another is opening it -- asked first over unsaved changes --
     * without leaving HOME. */
    if (btns & 1u << PB_PLAY && cur < nrows) {
        if (cur == open_row && player_playing()) {
            player_stop();
        } else if (cur == open_row) {
            play();
        } else if (app_dirty) {
            pending = cur;
            asking = ASK_PLAY;
        } else {
            player_stop();
            load(cur);
            play();
        }
    }
    {
        int worst;
        cpu = plat_audio_load(&worst);
        peak = !player_playing() ? 0 : worst > peak ? worst : peak;
        if (!player_playing())
            player_profile_reset();
    }
    plat_led_btn(PB_PLAY, player_playing());
    gfx_frame(draw);
    return open_it;
}
