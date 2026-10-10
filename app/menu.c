/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "ui.h"

enum {
    NAV_Y = 2, NAV_H = 17,
    LEFT_Y = 20,
    CARD_Y = 42, CARD_H = 30,
    ROWS_Y = 85, ROW_H = 12, ROW_PITCH = 13, ROWS = 10,
};
enum { AT_FX, AT_SCL, AT_ENV, AT_DELETE, AT_OPEN, AT_GLO,
       AT_HOME, AT_SAVE, AT_ARP, AT_SEQ, AT_PLAY, AT_REC };
enum { ASK_NONE, ASK_DELETE, ASK_CHANGES };

/* The list, read once when the screen opens and after each change: a
 * frame is drawn fifteen times and must not go to the flash. */
static struct {
    uint8_t slot;
    char name[STORE_NAME + 1];
    uint32_t bytes;
} songs[APP_SONG_SLOTS];
static int nsongs, used_kb;
static int cur = -1; /* -1 the song in memory, 0.. a song in flash, nsongs the NEW row */
static int top, ask, pending; /* pending: what CHANGES was asked for -- a list row */
static uint32_t prev_btns;

static void refresh(void)
{
    nsongs = 0;
    for (int s = 0; s < APP_SONG_SLOTS; s++)
        if (store_info(s, songs[nsongs].name, &songs[nsongs].bytes) == STORE_OK)
            songs[nsongs++].slot = (uint8_t)s;
    /* by name: the slots are where things happened to land */
    for (int i = 1; i < nsongs; i++)
        for (int j = i; j > 0; j--) {
            const char *a = songs[j - 1].name, *b = songs[j].name;
            while (*a && *a == *b)
                a++, b++;
            if (*a <= *b)
                break;
            __typeof__(songs[0]) t = songs[j];
            songs[j] = songs[j - 1];
            songs[j - 1] = t;
        }
    used_kb = (int)((PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR - store_free()) / 1024u);
    if (cur > nsongs)
        cur = nsongs;
}

void menu_open(void)
{
    refresh();
    cur = -1;
    top = 0;
    ask = ASK_NONE;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

/* ---------------------------------------------------------- drawing --- */
static bool playing;
static uint32_t held;

static bool lit(int at)
{
    if (ask == ASK_DELETE)
        return at == AT_DELETE;
    if (ask == ASK_CHANGES)
        return at == AT_DELETE || at == AT_SAVE;
    if (at == AT_OPEN)
        return true;
    if (at == AT_PLAY)
        return cur == -1; /* only the song in memory can be heard: there is one song's worth of RAM */
    return at == AT_DELETE && cur >= 0 && cur < nsongs;
}

static const char *word(int at)
{
    if (at == AT_DELETE)
        return ask == ASK_DELETE ? "SURE" : ask == ASK_CHANGES ? "DISCARD" : "DELETE";
    if (at == AT_SAVE)
        return ask == ASK_CHANGES ? "SAVE" : "";
    if (at == AT_OPEN)
        return "OPEN";
    if (at == AT_PLAY)
        return playing ? "STOP" : "PLAY";
    return "";
}

static void draw(void)
{
    char t[24], n[8];
    int x, load, worst;
    bool on_card = cur == -1;

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text(F_COND12, APP_X0, NAV_Y + 13, "M O O N D A W", C_TITLE);
    x = APP_X0 + APP_INNER - gfx_text_w(F_MONO8, " / 292 KB");
    gfx_text(F_MONO8, x, NAV_Y + 12, " / 292 KB", C_DIM);
    x -= gfx_text_w(F_MONO8, ui_dec(n, used_kb));
    gfx_text(F_MONO8, x, NAV_Y + 12, n, C_TEXT);
    gfx_text(F_MONO8, x - gfx_text_w(F_MONO8, "FLASH "), NAV_Y + 12, "FLASH ", C_DIM);
    gfx_fill(APP_X0, NAV_Y + NAV_H, APP_INNER, 1, C_DIM, 77);

    x = gfx_text(F_MONO8, APP_X0, LEFT_Y + 8, "SELECT", C_DIM);
    gfx_text(F_MONO8, x + 3, LEFT_Y + 8, "SONG", C_TEXT);
    if ((load = plat_audio_load(&worst)) >= 0) {
        /* the audio interrupt's share of its time, and its worst half */
        x = gfx_text(F_MONO8, 150, LEFT_Y + 8, "CPU", C_DIM);
        x = gfx_text(F_MONO8, x + 3, LEFT_Y + 8, ui_dec(n, load), load > 70 ? C_MUTE : C_TEXT);
        x = gfx_text(F_MONO8, x + 4, LEFT_Y + 8, "MAX", C_DIM);
        gfx_text(F_MONO8, x + 3, LEFT_Y + 8, ui_dec(n, worst), worst > 90 ? C_MUTE : C_TEXT);
    }

    /* What is in memory: the one thing to continue. */
    gfx_text(F_MONO8, APP_X0 + 2, CARD_Y - 3, "CONTINUE", C_DIM);
    if (ask == ASK_CHANGES) {
        gfx_slab(APP_X0, CARD_Y, APP_INNER, CARD_H, C_MUTE, 255);
        gfx_text(F_COND14, APP_X0 + 5, CARD_Y + 14, app_name, C_WHITE);
        gfx_text(F_MONO8, APP_X0 + 5, CARD_Y + 26, "HAS CHANGES", C_WHITE);
    } else {
        uint16_t ink = on_card ? C_ON_ACC : C_TEXT, dim = on_card ? C_ON_ACC : C_DIM;
        gfx_slab(APP_X0, CARD_Y, APP_INNER, CARD_H, on_card ? C_ACCENT : C_TITLE, on_card ? 255 : 26);
        x = gfx_text(F_COND14, APP_X0 + 5, CARD_Y + 14, app_name, on_card ? C_ON_ACC : C_TITLE);
        if (app_dirty)
            gfx_text(F_MONO8, x + 6, CARD_Y + 13, "NOT SAVED", on_card ? C_ON_ACC : C_FX);
        x = gfx_text(F_MONO8, APP_X0 + 5, CARD_Y + 26, ui_dec(n, app_song.arrange_len), ink);
        x = gfx_text(F_MONO8, x + 3, CARD_Y + 26, "PHRASES", dim);
        x = gfx_text(F_MONO8, x + 7, CARD_Y + 26, ui_dec(n, app_song.num_tracks), ink);
        x = gfx_text(F_MONO8, x + 3, CARD_Y + 26, "TRACKS", dim);
        x = gfx_text(F_MONO8, x + 7, CARD_Y + 26, ui_dec(n, app_song.bpm), ink);
        gfx_text(F_MONO8, x + 3, CARD_Y + 26, "BPM", dim);
    }

    gfx_text(F_MONO8, APP_X0 + 2, ROWS_Y - 3, "SONGS", C_DIM);
    for (int i = 0; i < ROWS && top + i <= nsongs; i++) {
        int k = top + i, y = ROWS_Y + i * ROW_PITCH;
        bool here = cur == k;

        if (k == nsongs) { /* the row that makes one */
            if (here)
                gfx_slab(APP_X0, y, APP_INNER, ROW_H, C_ACCENT, 255);
            else
                gfx_frame_rect(APP_X0, y, APP_INNER, ROW_H, C_DIM, 76);
            gfx_text(F_COND11, APP_X0 + 5, y + 10, "+ NEW", here ? C_ON_ACC : C_DIM);
            continue;
        }
        if (here && ask == ASK_DELETE) {
            int i2 = 0;
            for (const char *s = "DELETE "; *s; s++)
                t[i2++] = *s;
            for (const char *s = songs[k].name; *s; s++)
                t[i2++] = *s;
            t[i2++] = '?';
            t[i2] = '\0';
            gfx_slab(APP_X0, y, APP_INNER, ROW_H, C_MUTE, 255);
            gfx_text(F_COND11, APP_X0 + 5, y + 10, t, C_WHITE);
            continue;
        }
        gfx_slab(APP_X0, y, APP_INNER, ROW_H, here ? C_ACCENT : C_TEXT, here ? 255 : 11);
        /* the song in memory keeps its name in cyan in the list: where one is */
        gfx_text(F_COND11, APP_X0 + 5, y + 10, songs[k].name,
                 here ? C_ON_ACC : songs[k].slot == app_slot ? C_TITLE : C_TEXT);
        ui_dec(t, (int)((songs[k].bytes + 1023u) / 1024u));
        x = APP_X0 + APP_INNER - 5 - gfx_text_w(F_MONO8, " KB");
        gfx_text(F_MONO8, x, y + 9, " KB", here ? C_ON_ACC : C_DIM);
        gfx_text(F_MONO8, x - gfx_text_w(F_MONO8, t), y + 9, t, here ? C_ON_ACC : C_DIM);
    }

    {
        const char *words[12];
        uint8_t st[12];
        for (int b = 0; b < 12; b++) {
            words[b] = word(b);
            st[b] = !lit(b) ? UI_BTN_NONE : b == AT_DELETE && ask != ASK_NONE ? UI_BTN_ASK
                    : b == AT_PLAY && playing ? UI_BTN_LIVE : UI_BTN_ON;
        }
        ui_btn_map(words, st, held);
    }
}

/* -------------------------------------------------------------- api --- */
/* What OPEN on a list row or on NEW does once nothing stands in its way. */
static void go(int row)
{
    if (row == nsongs)
        app_new();
    else
        app_open_slot(songs[row].slot);
    refresh();
    cur = -1; /* the song just opened is the one to continue */
    top = 0;
}

bool menu_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool moved = in->enc[PE_SELECT] != 0, open_it = false;
#define PRESSED(at) ((btns >> APP_BTN_AT[at]) & 1u)

    prev_btns = in->btns;
    held = in->btns;
    playing = player_playing();

    if (ask != ASK_NONE) {
        /* A question is on the screen. Its buttons answer it; anything
         * else is "no". */
        if (ask == ASK_DELETE && PRESSED(AT_DELETE)) {
            if (songs[cur].slot == app_slot) {
                app_slot = -1; /* the song stays in memory, and is now nowhere else */
                app_dirty = true;
            }
            store_delete(songs[cur].slot);
            refresh();
        } else if (ask == ASK_CHANGES && PRESSED(AT_SAVE)) {
            if (app_save() == 0) {
                refresh();
                go(pending);
            }
        } else if (ask == ASK_CHANGES && PRESSED(AT_DELETE)) {
            go(pending);
        } else if (!btns && !moved) {
            gfx_frame(draw);
            return false;
        }
        ask = ASK_NONE;
        gfx_frame(draw);
        return false;
    }

    cur += in->enc[PE_SELECT];
    cur = cur < -1 ? -1 : cur > nsongs ? nsongs : cur;
    if (cur >= 0 && cur < top)
        top = cur;
    if (cur >= top + ROWS)
        top = cur - ROWS + 1;
    if (cur == -1)
        top = 0;

    if (PRESSED(AT_OPEN)) {
        if (cur == -1) {
            open_it = true;
        } else if (cur < nsongs && songs[cur].slot == app_slot && !app_dirty) {
            cur = -1; /* it is the song in memory already */
            open_it = true;
        } else if (app_dirty) {
            pending = cur;
            ask = ASK_CHANGES;
        } else {
            go(cur);
        }
    }
    if (PRESSED(AT_DELETE) && cur >= 0 && cur < nsongs)
        ask = ASK_DELETE;
    if (PRESSED(AT_PLAY) && cur == -1) {
        if (playing)
            player_stop();
        else
            player_play_song(0, false);
    }
    plat_led_btn(PB_PLAY, playing);
    gfx_frame(draw);
    return open_it;
#undef PRESSED
}
