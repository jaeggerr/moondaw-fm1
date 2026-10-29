/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "ui.h"

/* HOME, the root menu since 29/10 (as the other MOONDAW apps): CONTINUE,
 * the open project's name at its right; PROJECTS, how many; SETTINGS; HELP
 * (app/help.h). SELECT or KNOB 1 moves, SEL goes; PLAY on
 * CONTINUE plays the open project.
 *
 * PROJECTS, as HOME was validated on the panel on 28/10 (NOTES.md): the
 * projects by name only, CONDENSED 20, eight rows; the open one marked by
 * a bar at its left and an amber dot while it has unsaved changes; + NEW
 * last. SEL opens, PLAY plays the project under the cursor (opening it if
 * it is not the open one), HOME goes back to the root. The version small
 * at the bottom of both. */
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
static int peak; /* the worst block since PLAY, held */
static bool listing; /* in PROJECTS, not the root */
static int item;     /* the root's cursor */
static const char *const item_word[MENU_ITEMS] = { "CONTINUE", "PROJECTS", "SETTINGS", "HELP" };

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

void menu_open(int at)
{
    refresh();
    scroll_to(open_row >= 0 ? open_row : 0, true);
    listing = false;
    item = at;
    asking = ASK_NONE;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

void menu_reload(void)
{
    refresh();
    scroll_to(cur > nrows ? nrows : cur, false);
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

static void draw_root(void)
{
    for (int i = 0; i < MENU_ITEMS; i++) {
        int y = i * ROW, base = y + 2 + FONTS[F_COND20].ascent;
        bool here = i == item;
        char n[8];
        const char *right = i == MENU_CONTINUE ? app_name : i == MENU_PROJECTS ? ui_dec(n, nrows) : "";
        int x = PLAT_W - 10 - gfx_text_w(F_COND20, right) - (i == MENU_CONTINUE && app_dirty ? 13 : 0);
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4, ROW - 2, C_ACCENT, 255);
        gfx_text(F_COND20, TEXT_X, base, item_word[i], here ? C_ON_ACC : C_TEXT);
        x = gfx_text(F_COND20, x, base, right, here ? C_ON_ACC : C_DIM);
        if (i == MENU_CONTINUE && app_dirty) /* not saved */
            gfx_fill(x + 6, base - 9, 7, 7, here ? C_ON_ACC : C_FX, 255);
    }
}

static void draw_foot(void);

static void draw(void)
{
    int count = nrows + 1; /* + NEW */

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    if (asking) {
        draw_question();
        return;
    }
    if (!listing) {
        draw_root();
        draw_foot();
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
    draw_foot();
}

/* The version, or while a song plays what it costs, stage by stage; MAX. */
static void draw_foot(void)
{
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
    if (app_cpu >= 0) { /* what the song costs the chip: the worst block held while it plays */
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

int menu_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool open_it = false;
    int to = MENU_STAY;

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
            return MENU_STAY;
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
        return open_it ? MENU_SONG : MENU_STAY;
    }

    if (!listing) { /* the root */
        item += in->enc[PE_SELECT] + in->enc[PE_K1];
        item = item < 0 ? 0 : item >= MENU_ITEMS ? MENU_ITEMS - 1 : item;
        if (btns & 1u << PB_SEL) {
            if (item == MENU_CONTINUE)
                to = MENU_SONG;
            else if (item == MENU_PROJECTS) {
                listing = true;
                refresh();
                scroll_to(open_row >= 0 ? open_row : 0, true);
            } else if (item == MENU_SETTINGS)
                to = MENU_TO_SETTINGS;
            else if (item == MENU_HELP)
                to = MENU_TO_HELP;
        }
        if (btns & 1u << PB_PLAY && item == MENU_CONTINUE) {
            if (player_playing())
                player_stop();
            else
                play();
        }
    } else {
        if (btns & 1u << PB_HOME) { /* back to the root, on PROJECTS */
            listing = false;
            item = MENU_PROJECTS;
        }
        int d = in->enc[PE_SELECT] + in->enc[PE_K1];
        scroll_to(cur + d < 0 ? 0 : cur + d > nrows ? nrows : cur + d, false);
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
    }
    peak = !player_playing() ? 0 : app_block > peak ? app_block : peak; /* the worst block, from PLAY on */
    if (!player_playing())
        player_profile_reset();
    ui_led_state(PB_PLAY, player_playing());
    gfx_frame(draw);
    return open_it ? MENU_SONG : to;
}
