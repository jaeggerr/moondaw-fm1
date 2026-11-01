/* SPDX-License-Identifier: GPL-3.0-only */
#include "room.h"

#include "../core/store.h"
#include "../core/trk.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"

enum { R_OFF, R_WINDOW, R_LIST, R_SURE, R_SAVED, R_LEAVING };
enum { TOP = 24, ROWS = 7, X = 12, ROW = 28, SAVED_MS = 1500 };

static int state;
static uint32_t need; /* what the song takes, as it is in memory now */
static struct {
    int slot;
    char name[STORE_NAME + 1];
    uint32_t bytes;
} list[APP_SONG_SLOTS];
static int count, cur, top;
static uint32_t prev_btns, saved_at;

static int sink(void *ctx, const char *data, uint32_t n)
{
    (void)ctx, (void)data, (void)n;
    return 0;
}

static int kb(uint32_t bytes) { return (int)((bytes + 1023) / 1024); }

/* s + word, for lines built of pieces. */
static char *put(char *s, const char *w)
{
    while (*w)
        *s++ = *w++;
    *s = 0;
    return s;
}

static char *put_kb(char *s, uint32_t bytes)
{
    char n[12];
    return put(put(s, ui_dec(n, kb(bytes))), " KB");
}

static void scan(void)
{
    count = 0;
    for (int s = 0; s < APP_SONG_SLOTS; s++)
        if (s != app_slot && store_info(s, list[count].name, &list[count].bytes) == STORE_OK)
            list[count++].slot = s;
    if (cur >= count)
        cur = count ? count - 1 : 0;
}

void room_open(void)
{
    int32_t n;
    if (state != R_OFF)
        return;
    n = trk_write(&app_song, sink, 0);
    need = n > 0 ? (uint32_t)n : 0;
    state = R_WINDOW;
    cur = top = 0;
    prev_btns = 0xFFFFFFFFu; /* the SAVE that opened it is not a press here */
    gfx_invalidate();
}

bool room_active(void) { return state != R_OFF; }

/* ------------------------------------------------------------ drawing --- */
static void hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 6, word, C_DIM);
}

static void draw_window(void)
{
    char s[40];
    int y = 34;
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND20, X, y, "FLASH FULL", C_FX);
    gfx_text(F_COND20, X, y += ROW, app_name, C_TEXT);
    gfx_text(F_COND20, X, y += ROW, "IS NOT SAVED.", C_TEXT);
    put(put_kb(put(put_kb(put(s, "It takes "), need), ", "), store_free()), " left.");
    gfx_text(F_COND20, X, y += 2 * ROW - 8, s, C_DIM);
    gfx_text(F_COND20, X, y += ROW - 4, "Delete a project and", C_DIM);
    gfx_text(F_COND20, X, y += ROW - 4, "it saves by itself.", C_DIM);
    hint(6, "SEL", "PROJECTS", false);
    hint(PLAT_W - 6, "HOME", "BACK, NOT SAVED", true);
}

static void draw_list(void)
{
    char s[24];
    uint32_t free = store_free();
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND14, 6, 17, "DELETE A PROJECT", C_TITLE);
    put(put_kb(put(s, "NEED "), need > free ? need - free : 0), " MORE");
    gfx_text(F_COND14, PLAT_W - 6 - gfx_text_w(F_COND14, s), 17, s, C_FX);
    gfx_fill(0, TOP - 3, PLAT_W, 1, C_TRACK, 255);
    if (!count)
        gfx_text(F_COND20, X, TOP + 2 + FONTS[F_COND20].ascent, "NO OTHER PROJECT", C_DIM);
    for (int r = 0; r < ROWS && top + r < count; r++) {
        int k = top + r, y = TOP + r * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent;
        bool here = k == cur;
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 14, GRID_ROW - 2, C_ACCENT, 255);
        gfx_text(F_COND20, 14, base, list[k].name, here ? C_ON_ACC : C_TEXT);
        put_kb(s, list[k].bytes);
        gfx_text(F_COND14, PLAT_W - 16 - gfx_text_w(F_COND14, s), base - 2, s, here ? C_ON_ACC : C_DIM);
    }
    if (count > ROWS) {
        int hh = ROWS * GRID_ROW * ROWS / count, y = TOP + ROWS * GRID_ROW * top / count;
        gfx_fill(PLAT_W - 4, TOP, 2, ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, hh, C_DIM, 255);
    }
    if (count)
        hint(6, "SEL", "DELETE", false);
    hint(PLAT_W - 6, "HOME", "BACK", true);
}

static void draw_sure(void)
{
    char s[24];
    int y = 62;
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND20, X, y, "DELETE THIS PROJECT?", C_TEXT);
    gfx_text(F_COND20, X, y += ROW, list[cur].name, C_FX);
    put(put_kb(s, list[cur].bytes), " for good.");
    gfx_text(F_COND20, X, y += ROW, s, C_DIM);
    hint(6, "SEL", "YES, DELETE", false);
    hint(PLAT_W - 6, "HOME", "NO", true);
}

static void draw_saved(void)
{
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND20, X, 90, app_name, C_TEXT);
    gfx_text(F_COND20, X, 90 + ROW, "IS SAVED.", C_ACCENT);
}

static void draw(void)
{
    if (state == R_LIST)
        draw_list();
    else if (state == R_SURE)
        draw_sure();
    else if (state == R_SAVED)
        draw_saved();
    else
        draw_window();
}

/* -------------------------------------------------------------- hands --- */
bool room_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    int was = state;
#define PRESSED(b) ((btns >> (b)) & 1u)

    if (state == R_OFF)
        return false;
    prev_btns = in->btns;
    switch (state) {
    case R_WINDOW:
        if (PRESSED(PB_SEL)) {
            scan();
            state = R_LIST;
        } else if (PRESSED(PB_HOME)) {
            state = R_LEAVING;
        }
        break;
    case R_LIST:
        cur += in->enc[PE_SELECT] + in->enc[PE_K1];
        cur = cur >= count ? count - 1 : cur;
        cur = cur < 0 ? 0 : cur;
        if (count)
            grid_follow(&cur, &top, count, ROWS);
        if (in->enc[PE_SELECT] || in->enc[PE_K1])
            gfx_invalidate();
        if (PRESSED(PB_SEL) && count)
            state = R_SURE;
        else if (PRESSED(PB_HOME))
            state = R_WINDOW;
        break;
    case R_SURE:
        if (PRESSED(PB_SEL)) {
            store_delete(list[cur].slot);
            scan();
            state = R_LIST;
            if (need <= store_free() && app_save() == 0) { /* it was asked for already */
                state = R_SAVED;
                saved_at = in->ms;
            }
        } else if (PRESSED(PB_HOME)) {
            state = R_LIST;
        }
        break;
    case R_SAVED:
        if (btns || in->ms - saved_at >= SAVED_MS)
            state = R_LEAVING;
        break;
    default: /* R_LEAVING: the screen under it gets the hand back empty */
        if (!in->btns && !in->keys) {
            state = R_OFF;
            gfx_invalidate();
            return false;
        }
        break;
    }
    if (state != was)
        gfx_invalidate();
    ui_led_guide(state == R_LIST || state == R_SURE || state == R_WINDOW ? 1u << PB_SEL | 1u << PB_HOME : 0u);
    ui_led_keys(0, 0, 0);
    gfx_frame(draw);
    return true;
#undef PRESSED
}
