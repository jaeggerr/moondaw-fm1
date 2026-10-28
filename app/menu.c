/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"

/* HOME, as validated on the panel on 28/10 (NOTES.md): the projects by name
 * only, CONDENSED 20, eight rows; the open one marked by a bar at its left
 * and an amber dot while it has unsaved changes; + NEW last; the version
 * small at the bottom. SELECT moves, SEL opens. */
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
static uint32_t prev_btns;

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
    asking = 0;
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
    gfx_text(F_COND12, TEXT_X, y += 2 * ROW - 8, "then opens ", C_DIM);
    gfx_text(F_COND12, TEXT_X + gfx_text_w(F_COND12, "then opens "), y, name, C_DIM);
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
    gfx_text(F_COND12, TEXT_X, PLAT_H - 4, "MOONDAW " MOONDAW_VERSION, C_DIM);
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

bool menu_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool open_it = false;

    prev_btns = in->btns;
    if (asking) {
        /* The question's two buttons answer it; anything else is "stay". */
        if (btns & 1u << PB_SAVE) {
            if (app_save() == 0) {
                load(pending);
                open_it = true;
            }
        } else if (btns & 1u << PB_SEL) {
            load(pending);
            open_it = true;
        } else if (!btns && !in->enc[PE_SELECT]) {
            gfx_frame(draw);
            return false;
        }
        asking = 0; /* "stay": the list as it was, the cursor where it was */
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
            asking = 1;
        } else {
            load(cur);
            open_it = true;
        }
    }
    if (btns & 1u << PB_PLAY) { /* the open project, from anywhere in the list */
        if (player_playing())
            player_stop();
        else
            player_play_song(0, false);
    }
    plat_led_btn(PB_PLAY, player_playing());
    gfx_frame(draw);
    return open_it;
}
