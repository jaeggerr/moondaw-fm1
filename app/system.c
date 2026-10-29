/* SPDX-License-Identifier: GPL-3.0-only */
#include "system.h"

#include "../core/bank.h"
#include "../core/player.h"
#include "../core/store.h"
#include "app.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"
#include "version.h"

enum { LABEL_X = 14, VALUE_X = 112, BAR_Y = 112, BAR_H = 12 };

/* Read once when the screen opens: a frame is drawn fifteen times, and
 * must not go to the flash. */
static int songs, samples;
static uint32_t used, avail, total, bank;
static uint32_t prev_btns;

static void kb(char *s, uint32_t bytes) /* "103 KB" */
{
    char *p = ui_dec(s, (int)((bytes + 1023) / 1024));
    while (*p)
        p++;
    p[0] = ' ', p[1] = 'K', p[2] = 'B', p[3] = 0;
}

void system_open(void)
{
    uint32_t bytes;
    char name[STORE_NAME + 1];
    songs = 0, used = 0;
    for (int s = 0; s < APP_SONG_SLOTS; s++)
        if (store_info(s, name, &bytes) == STORE_OK)
            songs++, used += bytes;
    samples = bank_count();
    bank = store_has(BANK_SLOT) ? bank_bytes() : 0;
    avail = store_free();
    total = PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

static void row(int y, const char *label, const char *value, uint16_t ink)
{
    gfx_text(F_COND20, LABEL_X, y, label, C_DIM);
    gfx_text(F_COND20, VALUE_X, y, value, ink);
}

/* "3 / 103 KB" */
static void count_kb(char *s, int n, uint32_t bytes)
{
    char t[12], *p = ui_dec(s, n);
    while (*p)
        p++;
    p[0] = ' ', p[1] = '/', p[2] = ' ', p[3] = 0;
    kb(t, bytes);
    for (int i = 0; t[i]; i++)
        p[3 + i] = t[i], p[4 + i] = 0;
}

static void draw(void)
{
    char s[24];
    /* in KB, 32 bits: no 64-bit division in the image */
    int w = PLAT_W - 2 * LABEL_X, all = (int)(total / 1024), x;
    int used_w = all ? (int)(used / 1024) * w / all : 0, free_w = all ? (int)(avail / 1024) * w / all : 0;
    int bank_w = all ? (int)(bank / 1024) * w / all : 0;

    grid_clear();
    grid_corner("SYSTEM", C_DIM, 0, 0);
    kb(s, avail);
    row(44, "FREE", s, C_TEXT);
    count_kb(s, songs, used);
    row(72, "SONGS", s, C_TEXT);
    if (bank)
        count_kb(s, samples, bank);
    row(100, "SAMPLES", bank ? s : "--", bank ? C_TEXT : C_DIM);
    /* the flash at a glance: songs, the sample bank after them, then what
     * is free; the rest of it is the files' own headers and what an
     * unfinished save left */
    gfx_fill(LABEL_X, BAR_Y + 6, w, BAR_H, C_TRACK, 255);
    gfx_fill(LABEL_X, BAR_Y + 6, used_w, BAR_H, C_TITLE, 255);
    gfx_fill(LABEL_X + used_w, BAR_Y + 6, bank_w, BAR_H, C_SAMPLE, 255);
    gfx_fill(LABEL_X + w - free_w, BAR_Y + 6, free_w, BAR_H, C_ACCENT, 255);
    kb(s, total);
    x = gfx_text(F_COND12, LABEL_X, BAR_Y + 34, "songs", C_TITLE) + 10;
    if (bank)
        x = gfx_text(F_COND12, x, BAR_Y + 34, "samples", C_SAMPLE) + 10;
    gfx_text(F_COND12, x, BAR_Y + 34, "free", C_ACCENT);
    gfx_text(F_COND12, PLAT_W - LABEL_X - gfx_text_w(F_COND12, s), BAR_Y + 34, s, C_DIM);
    row(188, "FIRMWARE", "MOONDAW " MOONDAW_VERSION, C_TEXT);
    ui_dec(s, app_worst);
    {
        char *p = s;
        while (*p)
            p++;
        p[0] = ' ', p[1] = '%', p[2] = 0;
    }
    row(216, "CPU MAX", s, app_worst > 90 ? C_MUTE : C_TEXT);
}

bool system_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    prev_btns = in->btns;
    if ((btns >> PB_PLAY & 1u) && player_playing()) /* stop, from anywhere */
        player_stop();
    ui_led_state(PB_PLAY, player_playing());
    gfx_frame(draw);
    return !(btns >> PB_HOME & 1u);
}
