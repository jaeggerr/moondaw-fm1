/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu.h"

#include "../core/player.h"
#include "app.h"
#include "gfx.h"
#include "namer.h"
#include "theme.h"
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
 * it is not the open one), HOME goes back to the root. EDIT on a project:
 * a panel -- OPEN (EDIT where SEL was meant), RENAME, DUPLICATE (KIDS 2, then the open one), DELETE (asked
 * first: a flash erase has no UNDO; the open one deleted, the first left
 * is opened in its place, or an empty project when none is). */
enum { ROW = 28, ROWS = 8, TEXT_X = 12 };
/* The root's top bar, chosen on the panel on 31/10 out of three
 * (proto/home.html): the wordmark left, the charge right as a figure AND
 * a cell, a rule under them. The rows start below it, and the foot gives
 * up what the bar now says -- the version, which SYSTEM holds as well,
 * and MAX, which SYSTEM holds as CPU MAX. PROJECTS has neither bar nor
 * foot (1/11). */
enum {
    BAR_TOP = 32,            /* where the rows begin */
    BAR_BASE = 20,           /* the bar's baseline */
    BAR_MID = 14,            /* the cell's middle */
    BAR_RULE = 28,
    BAR_RIGHT = PLAT_W - 10, /* the rows' own right margin */
    CELL_W = 26, CELL_H = 13,
    LOW = 15,                /* at or under this the charge goes red */
};

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
enum { ASK_NONE, ASK_OPEN, ASK_PLAY, ASK_HOME }; /* what the question leads to once answered */
/* HOME again leaves without keeping the changes; every other button is
 * stay, so a press on a question nobody read lands on stay eleven times
 * out of twelve. A hold was asked for first and taken back: "le home
 * maintenu est inutile vu que de toute facon tous les autres boutons
 * annulent deja" (30/10). It cost the one reflex of tapping HOME twice. */
static bool ask_armed; /* HOME let go since the question opened -- it opened under a held HOME */
static int ask_failed; /* what SAVE answered the question with, 0 until it failed */
static uint32_t prev_btns;
static int batt = -1; /* 0..100, or -1: read once a frame, never from a draw */
static bool listing; /* in PROJECTS, not the root */
/* EDIT on a project (29/10): a panel of what can be done to it. */
enum { OPT_OPEN, OPT_RENAME, OPT_DUPLICATE, OPT_DELETE, OPTS };
static const char *const opt_word[OPTS] = { "OPEN", "RENAME", "DUPLICATE", "DELETE" }; /* OPEN: EDIT where SEL was meant */
static bool opts, deleting, naming;
static int opt_cur, opt_row;    /* opt_row: the project the panel is about */
static char new_name[STORE_NAME + 1];
static const char *opt_said;
static int item;     /* the root's cursor */
static const char *const item_word[MENU_ITEMS] = { "CONTINUE", "PROJECTS", "SETTINGS", "SYSTEM", "HELP" };

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
    if (app_slot < 0 && app_loaded) {
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
    opts = deleting = naming = false;
    refresh();
    scroll_to(open_row >= 0 ? open_row : 0, true);
    listing = false;
    item = at;
    asking = ASK_NONE;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

/* The root reached over changes: the question, before anything is put
 * down. It opens under a held HOME -- the hold that got here -- so the
 * next hold only counts once that one has been let go. */
void menu_ask_home(void)
{
    opts = deleting = naming = listing = false;
    asking = ASK_HOME;
    ask_failed = 0;
    pending = open_row;
    ask_armed = false;
    prev_btns = 0xFFFFFFFFu;
    gfx_invalidate();
}

void menu_reload(void)
{
    refresh();
    scroll_to(cur > nrows ? nrows : cur, false);
    gfx_invalidate();
}

/* ---------------------------------------------------- EDIT's panel --- */
static bool name_taken(const char *n)
{
    for (int i = 0; i < nrows; i++) {
        int k = 0;
        while (n[k] && n[k] == rows[i].name[k])
            k++;
        if (n[k] == rows[i].name[k])
            return true;
    }
    return false;
}

/* DUPLICATE's name: the project's, then 2, 3... while one is taken. */
static void copy_name(const char *from)
{
    for (int k = 2; k < 100; k++) {
        int n = 0;
        char d[4];
        const char *q = ui_dec(d, k);
        int len = 0, tail;
        while (q[len])
            len++;
        tail = STORE_NAME - len - 1;
        while (from[n] && n < tail)
            new_name[n] = from[n], n++;
        new_name[n++] = ' ';
        for (int i = 0; q[i]; i++)
            new_name[n++] = q[i];
        new_name[n] = 0;
        if (!name_taken(new_name))
            return;
    }
}

/* RENAME and DUPLICATE go through the song in memory, as SAVE does (the
 * flash cannot read one file while it writes another): the project is
 * opened first when it is not the open one -- not over unsaved changes. */
static bool continue_song(void);

static bool take_in_hand(void)
{
    if (opt_row == open_row) /* the root holds no song: pick it up to work on it */
        return continue_song();
    if (app_dirty) {
        opt_said = "SAVE THE OPEN ONE FIRST";
        return false;
    }
    return rows[opt_row].slot >= 0 && app_open_slot(rows[opt_row].slot) == 0;
}

static void opt_go(void)
{
    opt_said = 0;
    if (opt_cur == OPT_OPEN)
        return; /* menu_frame opens it, as SEL in the list */
    if (opt_cur == OPT_DELETE) {
        deleting = true;
    } else if (take_in_hand()) {
        int i = 0;
        if (opt_cur == OPT_RENAME)
            for (; (new_name[i] = app_name[i]) != 0; i++)
                ;
        else
            copy_name(app_name);
        namer_open(new_name, STORE_NAME);
        naming = true;
    }
}

/* The name kept: RENAME rewrites the project under it, in its place;
 * DUPLICATE writes a new project, which is then the open one. */
static void opt_named(void)
{
    int i = 0;
    if (opt_cur == OPT_DUPLICATE)
        app_slot = -1; /* a new place in flash */
    else if (rows[opt_row].slot < 0)
        app_slot = -1;
    for (; (app_name[i] = new_name[i]) != 0; i++)
        ;
    opt_said = app_save() == 0 ? (opt_cur == OPT_RENAME ? "RENAMED" : "DUPLICATED") : "NOT SAVED: FLASH FULL";
    refresh();
    for (int r = 0; r < nrows; r++)
        if (rows[r].slot == app_slot)
            scroll_to(r, false);
    opts = false;
}

static void opt_delete(void)
{
    int slot = rows[opt_row].slot;
    bool was_open = opt_row == open_row;
    if (slot >= 0)
        store_delete(slot);
    if (was_open) { /* deleted is deleted (his words): another takes its place in memory */
        int next = -1;
        refresh();
        for (int r = 0; r < nrows && next < 0; r++)
            if (rows[r].slot >= 0)
                next = rows[r].slot;
        app_dirty = false;
        if (next < 0 || app_open_slot(next) != 0)
            app_new(); /* nothing left: an empty project */
    }
    refresh();
    scroll_to(cur > nrows ? nrows : cur, false);
    opts = deleting = false;
}

static void draw_opts(void)
{
    gfx_text(F_COND12, TEXT_X, 14, "PROJECT", C_TITLE);
    gfx_text(F_COND20, TEXT_X, 46, rows[opt_row].name, C_TEXT);
    if (deleting) {
        gfx_text(F_COND20, TEXT_X, 46 + 2 * ROW, "Delete it for good?", C_TEXT);
        gfx_text(F_COND20, TEXT_X, 46 + 3 * ROW, "SEL  delete", C_MUTE);
        gfx_text(F_COND20, TEXT_X, 46 + 4 * ROW, "HOME  keep it", C_DIM);
        return;
    }
    for (int i = 0; i < OPTS; i++) {
        int y = 60 + i * ROW, base = y + 2 + FONTS[F_COND20].ascent;
        bool here = i == opt_cur;
        if (here)
            gfx_slab(2, y + 1, PLAT_W - 4, ROW - 2, i == OPT_DELETE ? C_MUTE : C_ACCENT, 255);
        /* DELETE is red wherever the cursor is, not only under it: the one
         * row of the panel that does not come back (30/10). */
        gfx_text(F_COND20, TEXT_X, base, opt_word[i],
                 here ? C_ON_ACC : i == OPT_DELETE ? C_MUTE : C_TEXT);
    }
    if (opt_said)
        gfx_text(F_COND14, TEXT_X, PLAT_H - 30, opt_said, C_FX);
    gfx_text(F_COND12, TEXT_X, PLAT_H - 4, "SEL  do it     HOME  back", C_DIM);
}

/* ---------------------------------------------------------- drawing --- */
static void draw_question(void)
{
    const char *name = pending < nrows ? rows[pending].name : "a new project";
    int y = 34;
    gfx_text(F_COND20, TEXT_X, y, app_name, C_FX);
    gfx_text(F_COND20, TEXT_X, y += ROW, "is not saved.", C_TEXT);
    gfx_text(F_COND20, TEXT_X, y += 2 * ROW, "SAVE  keep the changes", C_TEXT);
    gfx_text(F_COND20, TEXT_X, y += ROW, "HOME again  lose them", C_TEXT);
    if (ask_failed == STORE_ERR_FULL) /* the way out that keeps the changes */
        gfx_text(F_COND20, TEXT_X, y += ROW, "SEL  make room: PROJECTS", C_TEXT);
    gfx_text(F_COND20, TEXT_X, y += ROW, "anything else: stay", C_DIM);
    if (ask_failed) {
        char s[32], n[12];
        int j = 0;
        const char *q;
        if (ask_failed == STORE_ERR_FULL) {
            for (q = "FLASH FULL: "; *q; q++)
                s[j++] = *q;
            for (q = ui_dec(n, (int)(store_free() / 1024)); *q; q++)
                s[j++] = *q;
            for (q = " KB LEFT"; *q; q++)
                s[j++] = *q;
        } else {
            for (q = "NOT SAVED: THE FLASH REFUSED"; *q; q++)
                s[j++] = *q;
        }
        s[j] = 0;
        gfx_text(F_COND14, TEXT_X, y + ROW, s, C_FX);
    } else if (asking == ASK_HOME) {
        gfx_text(F_COND12, TEXT_X, y += 2 * ROW - 8, "then puts the song down", C_DIM);
    } else {
        const char *then = asking == ASK_PLAY ? "then plays " : "then opens ";
        gfx_text(F_COND12, TEXT_X, y += 2 * ROW - 8, then, C_DIM);
        gfx_text(F_COND12, TEXT_X + gfx_text_w(F_COND12, then), y, name, C_DIM);
    }
}

/* MOONDAW as the Swift app's bar has it -- gold MOON, cyan DAW, the
 * cursor after them -- in this machine's own CONDENSED rather than as
 * the app's pixel drawing: that one is 130 x 16, over half the screen.
 * The two inks are the splash's (app/logo.h), so one wordmark, three
 * sizes. */
static void draw_mark(void)
{
    int x = gfx_text(F_COND14, TEXT_X, BAR_BASE, "MOON", C_FX);
    x = gfx_text(F_COND14, x, BAR_BASE, "DAW", C_TITLE);
    gfx_text(F_COND14, x + 1, BAR_BASE, "_", C_DIM);
}

/* A cell lying on its side: a grey outline and its nub, what is left of
 * the charge inside. The outline stays whatever the charge is, so nearly
 * empty reads as a sliver and not as a hole. */
static void draw_cell(int pct, uint16_t ink)
{
    int x = BAR_RIGHT - 2 - CELL_W, y = BAR_MID - CELL_H / 2;
    int w = (CELL_W - 4) * pct / 100;
    gfx_frame_rect(x, y, CELL_W, CELL_H, C_DIM, 255);
    gfx_fill(x + CELL_W, y + 4, 2, CELL_H - 8, C_DIM, 255);
    if (w > 0)
        gfx_fill(x + 2, y + 2, w, CELL_H - 4, ink, 255);
}

/* The bar. A machine that does not know the charge draws no gauge at all
 * -- not an empty cell, which reads as a flat one, and not a "--", which
 * reads as a fault: the FM-1 is there today (platform.h, plat_battery). */
static void draw_bar(void)
{
    gfx_fill(0, BAR_RULE, PLAT_W, 1, C_TRACK, 255);
    draw_mark();
    if (batt < 0)
        return;
    {
        char b[8];
        char *p = ui_dec(b, batt);
        uint16_t ink = batt <= LOW ? C_MUTE : C_TEXT;
        while (*p)
            p++;
        p[0] = '%', p[1] = 0;
        gfx_text(F_COND14, BAR_RIGHT - CELL_W - 8 - gfx_text_w(F_COND14, b), BAR_BASE, b, ink);
        draw_cell(batt, ink);
    }
}

static void draw_root(void)
{
    for (int i = 0; i < MENU_ITEMS; i++) {
        int y = BAR_TOP + i * ROW, base = y + 2 + FONTS[F_COND20].ascent;
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
        draw_bar();
        draw_root();
        draw_foot();
        return;
    }
    if (naming) {
        namer_draw();
        return;
    }
    if (opts) {
        draw_opts();
        return;
    }
    /* One row more than fit: the next one, cut by the screen's edge, says
     * the list goes on. The cursor never reaches it without scrolling. */
    for (int r = 0; r <= ROWS && top + r < count; r++) {
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
        int h = PLAT_H * ROWS / count, y = PLAT_H * top / count;
        gfx_fill(PLAT_W - 4, 0, 2, PLAT_H, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
    }
}

/* The root, while a song plays: what it costs, stage by stage -- that one
 * stays, it is the only place it is said. MAX went with the version
 * (31/10, his call): SYSTEM holds the same number as CPU MAX. */
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

/* CONTINUE, with nothing in memory: the root put the song down, so it is
 * picked up again from where it was written. One that never reached the
 * flash has nothing to pick up, and a fresh one takes its place. */
static bool continue_song(void)
{
    if (app_loaded)
        return true;
    if (app_slot >= 0 && app_open_slot(app_slot) == STORE_OK)
        return true;
    app_new();
    return app_loaded;
}

int menu_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    bool open_it = false;
    int to = MENU_STAY;

    prev_btns = in->btns;
    if (asking) {
        /* SAVE keeps the changes, HOME again leaves without them, anything
         * else is "stay" -- PLAY stopping what plays as well. The question
         * opens under the held HOME that reached the root, so nothing counts
         * until that one has been let go (30/10). */
        int then = asking;
        bool answered = false, decided = false;

        if (btns & 1u << PB_PLAY && player_playing())
            player_stop();
        if (!(in->btns >> PB_HOME & 1u))
            ask_armed = true;
        {   /* "anything else" is anything: the keys and every knob, not the
             * buttons alone -- the line says so, so it had better be true. */
            bool touched = btns || in->keys;
            for (int e = 0; e < PLAT_ENCS; e++)
                touched |= in->enc[e] != 0;
            if (!ask_armed || !touched) {
                gfx_frame(draw);
                return MENU_STAY;
            }
        }
        if (btns & 1u << PB_SAVE) {
            /* Not saved, nothing is decided: leaving now, without a word, would
             * look as if SAVE did nothing. The question stays, and says why. */
            ask_failed = app_save();
            if (ask_failed) {
                gfx_invalidate();
                gfx_frame(draw);
                return MENU_STAY;
            }
            answered = decided = true;
        } else if (ask_failed == STORE_ERR_FULL && (btns & 1u << PB_SEL)) {
            /* PROJECTS with the song still in hand: the one place room can be
             * made, and CONTINUE picks the song up afterwards, changes and all. */
            asking = ASK_NONE;
            ask_failed = 0;
            listing = true;
            refresh();
            scroll_to(open_row >= 0 ? open_row : 0, true);
            gfx_invalidate();
            gfx_frame(draw);
            return MENU_STAY;
        } else if (btns & 1u << PB_HOME)
            answered = decided = true;
        else
            decided = true; /* anything else: stay */
        if (!decided) {
            gfx_frame(draw);
            return MENU_STAY;
        }
        asking = ASK_NONE; /* "stay": the list as it was, the cursor where it was */
        ask_failed = 0;
        if (then == ASK_HOME) {
            /* Saved or lost, the song is put down -- that is what reaching
             * the root means now. Unanswered, the hand goes back into it. */
            if (answered)
                app_unload();
            else
                return MENU_BACK;
            refresh();
            scroll_to(open_row >= 0 ? open_row : 0, true);
            gfx_frame(draw);
            return MENU_STAY;
        }
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
                to = continue_song() ? MENU_SONG : MENU_STAY;
            else if (item == MENU_PROJECTS) {
                listing = true;
                refresh();
                scroll_to(open_row >= 0 ? open_row : 0, true);
            } else if (item == MENU_SETTINGS)
                to = MENU_TO_SETTINGS;
            else if (item == MENU_SYSTEM)
                to = MENU_TO_SYSTEM;
            else if (item == MENU_HELP)
                to = MENU_TO_HELP;
        }
        if (btns & 1u << PB_PLAY && item == MENU_CONTINUE) {
            if (player_playing())
                player_stop();
            else if (continue_song())
                play();
        }
    } else if (naming) {
        int r = namer_frame(in, btns);
        if (btns & 1u << PB_PLAY && player_playing()) /* stop, from anywhere */
            player_stop();
        if (r == NAMER_KEPT)
            opt_named();
        if (r != NAMER_TYPING)
            naming = false;
        gfx_frame(draw);
        return MENU_STAY;
    } else if (opts) {
        if (deleting) {
            if (btns & 1u << PB_SEL)
                opt_delete();
            else if (btns & 1u << PB_HOME)
                deleting = false;
        } else {
            opt_cur += in->enc[PE_SELECT] + in->enc[PE_K1];
            opt_cur = opt_cur < 0 ? 0 : opt_cur >= OPTS ? OPTS - 1 : opt_cur;
            if (btns & 1u << PB_SEL && opt_cur == OPT_OPEN) { /* as SEL on the row */
                opts = false;
                cur = opt_row;
                if (cur == open_row) {
                    gfx_frame(draw);
                    return MENU_SONG;
                }
                if (app_dirty) {
                    pending = cur;
                    asking = ASK_OPEN;
                } else {
                    load(cur);
                    gfx_frame(draw);
                    return MENU_SONG;
                }
            } else if (btns & 1u << PB_SEL)
                opt_go();
            else if (btns & 1u << PB_HOME)
                opts = false;
        }
        if (btns & 1u << PB_PLAY) { /* stop, from here too; or the project, as PLAY on its row */
            if (player_playing())
                player_stop();
            else if (opt_row == open_row)
                play();
            else if (!app_dirty) {
                load(opt_row);
                play();
            }
        }
        ui_led_state(PB_PLAY, player_playing());
        gfx_frame(draw);
        return MENU_STAY;
    } else {
        if (btns & 1u << PB_EDIT && cur < nrows) { /* EDIT: what can be done to the project */
            opts = true, deleting = naming = false;
            opt_cur = 0, opt_row = cur, opt_said = 0;
            gfx_frame(draw);
            return MENU_STAY;
        }
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
    /* Once a frame, and only on the root, which is the one screen that
     * shows it: a draw runs fifteen times for the same frame. */
    batt = listing ? -1 : plat_battery(0);
    if (!player_playing())
        player_profile_reset();
    ui_led_state(PB_PLAY, player_playing());
    gfx_frame(draw);
    return open_it ? MENU_SONG : to;
}

bool menu_in_list(void) { return listing && !opts && !naming && !asking; }
bool menu_asking(void) { return asking != ASK_NONE; }
