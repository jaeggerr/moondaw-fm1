/* SPDX-License-Identifier: GPL-3.0-only */
#include "samples.h"

#include "../core/bank.h"
#include "../core/player.h"
#include "../core/sampler.h"
#include "../core/store.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"

enum { TOP = 24, ROWS = 7, X = 12, ROW = 28 };

static int nrows, cur, top;
static bool asking;
static const char *said; /* what the last delete answered with, or NULL */
static uint32_t free_kb;
static uint32_t prev_btns;

/* s + word, for lines built of pieces (as app/room.c). */
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
    return put(put(s, ui_dec(n, (int)((bytes + 1023) / 1024))), " KB");
}

/* What a sample takes of the flash: its blocks, which is what the hole it
 * would leave is worth (core/bank.h). */
static uint32_t row_bytes(int i)
{
    char name[BANK_NAME_LEN];
    uint32_t frames;

    if (!bank_entry(i, name, &frames))
        return 0;
    return (frames + SMP_BLOCK_FRAMES - 1) / SMP_BLOCK_FRAMES * SMP_BLOCK;
}

/* Read when the screen opens and after a delete: store_free goes to the
 * flash, and a frame is drawn fifteen times. */
static void refresh(void)
{
    nrows = bank_count();
    free_kb = store_free();
    grid_follow(&cur, &top, nrows, ROWS);
}

void samples_open(void)
{
    cur = top = 0;
    asking = false;
    said = 0;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    refresh();
    gfx_invalidate();
}

static void hint(int x, const char *key, const char *word, bool right)
{
    int w = gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word);
    if (right)
        x -= w;
    x = gfx_text(F_COND14, x, PLAT_H - 6, key, C_TEXT);
    gfx_text(F_COND14, x + 5, PLAT_H - 6, word, C_DIM);
}

static void hint_mid(const char *key, const char *word)
{
    hint((PLAT_W - (gfx_text_w(F_COND14, key) + 5 + gfx_text_w(F_COND14, word))) / 2, key, word, false);
}

/* The sample under the cursor, heard as it was recorded: through no
 * instrument of the song and no zone, dry at the preview level
 * (core/player.h). A name like tr9_mtom says little; the sound says
 * whether it can go. */
static void hear(int i)
{
    uint32_t at, frames;

    if (!bank_place(i, &at, &frames))
        return;
    player_audition_at(-1, 0, 0);
    player_audition_sample(at, frames);
}

static void draw_list(void)
{
    char s[24];

    grid_clear();
    gfx_text(F_COND14, 6, 17, "SAMPLES", C_TITLE);
    if (said)
        gfx_text(F_COND14, PLAT_W - 6 - gfx_text_w(F_COND14, said), 17, said, C_FX);
    else {
        put_kb(put(s, "FREE "), free_kb);
        gfx_text(F_COND14, PLAT_W - 6 - gfx_text_w(F_COND14, s), 17, s, C_ACCENT);
    }
    gfx_fill(0, TOP - 3, PLAT_W, 1, C_TRACK, 255);
    if (!nrows) {
        gfx_text(F_COND20, X, TOP + 2 + FONTS[F_COND20].ascent, "NO SAMPLES", C_DIM);
        gfx_text(F_COND14, X, TOP + 2 + ROW + FONTS[F_COND14].ascent, "THE COMPUTER FILLS THE BANK", C_DIM);
    }
    for (int r = 0; r < ROWS && top + r < nrows; r++) {
        char name[BANK_NAME_LEN];
        uint32_t frames;
        int k = top + r, y = TOP + r * ROW, base = y + 2 + FONTS[F_COND20].ascent;
        bool here = k == cur;
        if (!bank_entry(k, name, &frames))
            break;
        if (here)
            gfx_slab(4, y + 1, PLAT_W - 14, ROW - 2, C_ACCENT, 255);
        gfx_text(F_COND20, 14, base, name, here ? C_ON_ACC : C_TEXT);
        put_kb(s, row_bytes(k));
        gfx_text(F_COND14, PLAT_W - 16 - gfx_text_w(F_COND14, s), base - 2, s, here ? C_ON_ACC : C_DIM);
    }
    if (nrows > ROWS) { /* where the screen is in the list */
        int h = ROWS * ROW * ROWS / nrows, y = TOP + ROWS * ROW * top / nrows;
        gfx_fill(PLAT_W - 4, TOP, 2, ROWS * ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h < 4 ? 4 : h, C_DIM, 255);
    }
    if (nrows) {
        hint(6, "SEL", "DELETE", false);
        hint_mid("PLAY", "HEAR");
    }
    hint(PLAT_W - 6, "HOME", "BACK", true);
}

/* Asked first, as PROJECTS' DELETE is: a flash erase has no UNDO, and
 * what goes back is said in the same breath -- the room is the reason the
 * screen exists. */
static void draw_sure(void)
{
    char name[BANK_NAME_LEN], s[32];
    uint32_t frames;
    int y = 62;

    grid_clear();
    if (!bank_entry(cur, name, &frames))
        return;
    gfx_text(F_COND20, X, y, "DELETE THIS SAMPLE?", C_TEXT);
    gfx_text(F_COND20, X, y += ROW, name, C_FX);
    put(put_kb(s, row_bytes(cur)), " back, for good.");
    gfx_text(F_COND20, X, y += ROW, s, C_DIM);
    hint(6, "SEL", "YES, DELETE", false);
    hint(PLAT_W - 6, "HOME", "NO", true);
}

static void draw(void)
{
    if (asking)
        draw_sure();
    else
        draw_list();
}

bool samples_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool was = asking;
    int cur0 = cur;

    prev_btns = in->btns;
    /* PLAY is the sample under the cursor -- and, while a song plays, the
     * reflex every screen has, which is to stop it. */
    if (btns >> PB_PLAY & 1u) {
        if (player_playing())
            player_stop();
        else if (nrows && !asking)
            hear(cur);
    }
    ui_led_state(PB_PLAY, player_playing());
    ui_led_guide(1u << PB_SEL | 1u << PB_HOME);
    if (asking) {
        if (btns >> PB_SEL & 1u) {
            /* The voices go quiet and the song stops: the flash is about
             * to be written, and nothing may read it in place while it
             * is (as app/link.c does before it writes the bank). */
            player_stop();
            said = bank_delete(cur) == STORE_OK ? 0 : "NOT DELETED";
            asking = false;
            refresh();
        } else if (btns >> PB_HOME & 1u) {
            asking = false;
        }
    } else {
        cur += in->enc[PE_SELECT] + in->enc[PE_K1];
        grid_follow(&cur, &top, nrows, ROWS);
        if ((btns >> PB_SEL & 1u) && nrows) {
            asking = true;
            said = 0;
        } else if (btns >> PB_HOME & 1u) {
            gfx_invalidate();
            return false;
        }
    }
    if (asking != was || cur != cur0)
        gfx_invalidate();
    gfx_frame(draw);
    return true;
}
