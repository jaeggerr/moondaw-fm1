/* SPDX-License-Identifier: GPL-3.0-only */
#include "help.h"

#include "../core/player.h"
#include "../core/song.h"
#include "fx_help.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"
#include "ui.h"

/* A gesture: the control, and what it does -- each 24 characters at most,
 * a line of CONDENSED 20 on the panel. Written from the screens' own
 * comments: a change there is a change here. */
typedef struct {
    const char *key, *say;
} Item;

static const Item home_items[] = {
    { "SELECT, KNOB 1", "move" },
    { "SEL", "go: CONTINUE, PROJECTS..." },
    { "SYSTEM", "flash left, version, CPU" },
    { "PLAY on CONTINUE", "play the open project" },
    { "in PROJECTS: SEL", "open the project" },
    { "in PROJECTS: PLAY", "play it, staying here" },
    { "in PROJECTS: EDIT", "rename, duplicate, delete" },
    { "HOME", "back to the menu" },
};
static const Item song_items[] = {
    { "SELECT", "phrase, onto its number" },
    { "KNOB 1", "phrase" },
    { "KNOB 2", "track" },
    { "KNOB 3 on a cell", "its pattern" },
    { "  at rest", "it plays by itself" },
    { "  past the last", "a new pattern, NEW" },
    { "KNOB 3 on a number", "phrase, or a reference" },
    { "KNOB 3 on a REF", "the phrase it plays" },
    { "  past the phrases", "a whole section" },
    { "KNOB 4 on a cell", "its level in the phrase" },
    { "KNOB 4 on a number", "the phrase's length" },
    { "KNOB 4 on a REF", "how long it plays" },
    { "KNOB 4 on a marker", "phrases the section holds" },
    { "PRESETS on a cell", "choose a pattern" },
    { "PRESETS on a REF", "choose a phrase" },
    { "ENV", "section: these phrases" },
    { "  with no selection", "the one under the cursor" },
    { "  in the list", "--, a section, or NEW" },
    { "  NEW", "makes one, and names it" },
    { "SEL on a cell", "edit the pattern" },
    { "SEL on a number", "the phrase, step by step" },
    { "SEL on a REF", "go to what it plays" },
    { "SEL on a marker", "name the section" },
    { "EDIT held", "black keys: commands" },
    { "EDIT tapped", "the same, as a caps lock" },
    { "  tapped again", "out" },
    { "EDIT + OP5", "selection, in and out" },
    { "  then OP5 alone", "the same, once it is on" },
    { "  on a number", "whole phrases" },
    { "OP1 OP2 OP3", "copy, paste, cut" },
    { "OP4", "clear" },
    { "OP4 on a marker", "the section, not its phrases" },
    { "OP6", "loop these phrases" },
    { "OP6, none stretched", "a loop's end, then other" },
    { "MONO", "solo; held: mute" },
    { "SEQ on a number", "copy the phrase below" },
    { "LFO on a number", "delete the phrase: twice" },
    { "keys", "the track's instrument" },
    { "OCT- OCT+", "the keyboard's octave" },
    { "FX", "the mixer" },
    { "GLO", "the project" },
    { "PLAY", "the song, from the cursor" },
    { "  again", "stop, the cursor there" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back to the menu" },
};
static const Item phrase_items[] = {
    { "SELECT, KNOB 1", "step" },
    { "KNOB 2", "track" },
    { "keys", "write the note" },
    { "OCT- OCT+", "the keyboard's octave" },
    { "SEL, EDIT", "the track's pattern" },
    { "EDIT held + MONO", "solo; held: mute" },
    { "EDIT held + OP6", "a loop's end, then other" },
    { "FX", "the mixer" },
    { "GLO", "the project" },
    { "PLAY", "the phrase, round" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back to SONG" },
};
static const Item pattern_items[] = {
    { "SELECT", "step, onto its number" },
    { "KNOB 1", "step" },
    { "KNOB 2", "column" },
    { "KNOB 3", "note, instr., effect" },
    { "KNOB 4", "an effect's value" },
    { "SEQ, FX, ENV", "notes, effects, instr." },
    { "ENV, SEL on instr.", "edit the instrument" },
    { "PRESETS on instr.", "choose an instrument" },
    { "SEL on an effect", "the syllable" },
    { "ARP on notes", "the chords of the key" },
    { "keys", "write in a note column" },
    { "keys elsewhere", "play, write nothing" },
    { "OCT- OCT+", "the keyboard's octave" },
    { "EDIT held", "black keys: commands" },
    { "EDIT tapped", "the same, as a caps lock" },
    { "  tapped again", "out" },
    { "EDIT + OP5", "selection, in and out" },
    { "  then OP5 alone", "the same, once it is on" },
    { "OP1 OP2 OP3", "copy, paste, cut" },
    { "OP4", "clear" },
    { "OP6", "loop the selection" },
    { "OP6, no selection", "a loop's end, then other" },
    { "MONO", "solo; held: mute" },
    { "ALGO", "undo left, redo right" },
    { "PLAY", "the phrase, round" },
    { "  again", "stop, the cursor there" },
    { "REC", "armed: PLAY counts in" },
    { "  then keys", "record; OFF when let go" },
    { "GLO", "the project" },
    { "HOME", "back" },
};
static const Item syllable_items[] = {
    { "PRESETS", "keys or wheel" },
    { "KEYS: white keys", "consonants" },
    { "KEYS: black keys", "vowels" },
    { "KEYS: OCT", "the other signs" },
    { "KEYS: LFO", "rub the last out" },
    { "KEYS: ARP", "weak (.)" },
    { "WHEEL: KNOB 2", "the slot" },
    { "WHEEL: KNOB 3", "its sign" },
    { "WHEEL: LFO", "empty the slot" },
    { "PLAY", "sing it again" },
    { "ALGO", "undo left, redo right" },
    { "SEL", "write it in the step" },
    { "HOME", "back, nothing written" },
};
static const Item instrument_items[] = {
    { "SELECT, KNOB 1", "row" },
    { "KNOB 2", "column" },
    { "KNOB 3", "value" },
    { "PRESETS", "another instrument" },
    { "SEL on NAME", "name it" },
    { "keys", "play it" },
    { "OCT- OCT+", "the keyboard's octave" },
    { "PLAY", "a preview" },
    { "GLO", "the project" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back to the pattern" },
};
static const Item mixer_items[] = {
    { "SELECT", "track, onto its number" },
    { "KNOB 1", "track" },
    { "KNOB 2", "column" },
    { "KNOB 3", "value" },
    { "KNOB 3 on a number", "normal, mute, solo" },
    { "MONO", "solo; held: mute" },
    { "EDIT, GLO", "the project" },
    { "SEQ", "SONG" },
    { "PLAY", "the song" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back" },
};
static const Item project_items[] = {
    { "SELECT, KNOB 1", "setting" },
    { "KNOB 3", "its value" },
    { "SEL on NAME", "name the project" },
    { "FX", "the mixer" },
    { "SEQ", "SONG" },
    { "PLAY", "the song" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back" },
};
static const Item settings_items[] = {
    { "SELECT, KNOB 1", "setting" },
    { "KNOB 3", "its value" },
    { "LEDS", "off, state, guide" },
    { "LIGHT", "how bright the guide" },
    { "COUNT-IN", "bars before REC records" },
    { "HOME", "back, kept" },
};
static const Item gestures_items[] = {
    { "KNOB 1, KNOB 2", "move: row, column" },
    { "KNOB 3, KNOB 4", "change" },
    { "SELECT", "row, onto its number" },
    { "SEL", "into what is under" },
    { "HOME", "back" },
    { "HOME held", "the menu, from anywhere" },
    { "GLO", "the project" },
    { "ALGO", "undo left, redo right" },
    { "EDIT held", "black keys: commands" },
    { "lit buttons", "what this screen does" },
    { "SAVE", "save the project" },
    { "PLAY", "play, stop" },
};

#define PAGE(a) { a, (int)(sizeof a / sizeof a[0]) }
static const struct {
    const Item *items;
    int n;
} pages[HELP_EFFECTS] = {
    PAGE(home_items),     PAGE(song_items),       PAGE(phrase_items),  PAGE(pattern_items),
    PAGE(syllable_items), PAGE(instrument_items), PAGE(mixer_items),   PAGE(project_items),
    PAGE(settings_items), PAGE(gestures_items),
};
static const char *const topic_word[HELP_TOPICS] = { "HOME",  "SONG",    "PHRASE",   "PATTERN",  "SYLLABLE", "INSTRUMENT",
                                                     "MIXER", "PROJECT", "SETTINGS", "GESTURES", "EFFECTS" };

enum { LIST_TOP = 18, LIST_ROWS = 7, PAGE_TOP = 22, ITEM_H = 52, ITEMS = 4 };
/* EFFECTS: the effects in a list, and an effect's whole page (fx_help.h),
 * wrapped to the panel in CONDENSED 20 and read a line at a time. */
enum { FX_TEXT_TOP = 54, FX_LINE = 26, FX_LINES = 7, FX_TEXT_W = PLAT_W - 22 };
static int page = -1, cur, top, scroll, here;
static int fx_cur, fx_top, fx_open = -1; /* fx_open: the effect whose page is shown */
static uint32_t prev_btns;

/* --------------------------------------------------- EFFECTS' pages --- */
static int text_w(const char *a, const char *b)
{
    char s[96];
    int n = (int)(b - a) < 95 ? (int)(b - a) : 95;
    for (int i = 0; i < n; i++)
        s[i] = a[i];
    s[n] = 0;
    return gfx_text_w(F_COND20, s);
}

/* The lines of `t` wrapped to FX_TEXT_W, a blank one between paragraphs:
 * out(line, length, k) for each. Returns how many. */
static int wrap(const char *t, void (*out)(const char *, int, int))
{
    int k = 0;
    while (t && *t) {
        const char *start = t, *end = t;
        while (*t && *t != '\n') { /* the words that fit, the first one always */
            const char *w = t;
            while (*w == ' ')
                w++;
            while (*w && *w != ' ' && *w != '\n')
                w++;
            if (end > start && text_w(start, w) > FX_TEXT_W)
                break;
            end = t = w;
        }
        if (out)
            out(start, (int)(end - start), k);
        k++;
        t = end;
        while (*t == ' ')
            t++;
        if (*t == '\n' && *++t) {
            if (out)
                out("", 0, k);
            k++;
        }
    }
    return k;
}

static void line_out(const char *s, int n, int k)
{
    char b[96];
    int v = k - scroll;
    if (v < 0 || v >= FX_LINES || n == 0)
        return;
    n = n < 95 ? n : 95;
    for (int i = 0; i < n; i++)
        b[i] = s[i];
    b[n] = 0;
    gfx_text(F_COND20, 8, FX_TEXT_TOP + 20 + v * FX_LINE, b[0] == ' ' ? b + 1 : b, C_TEXT);
}

static void draw_effects(void)
{
    int x = gfx_text(F_COND12, 6, 14, "HELP", C_TITLE);
    gfx_text(F_COND12, x + 6, 14, "EFFECTS", C_TEXT);
    if (fx_open < 0) { /* the list: the name, and its line */
        for (int v = 0; v < LIST_ROWS && fx_top + v < FX_COUNT - 1; v++) {
            int i = fx_top + v, y = LIST_TOP + v * GRID_ROW, base = y + 2 + FONTS[F_COND20].ascent;
            bool on = i == fx_cur;
            if (on)
                gfx_slab(2, y + 1, PLAT_W - 4, GRID_ROW - 2, C_ACCENT, 255);
            gfx_text(F_COND20, 12, base, fx_name((uint8_t)(i + 1)), on ? C_ON_ACC : C_TEXT);
            gfx_text(F_COND14, 64, base - 1, fx_desc((uint8_t)(i + 1)), on ? C_ON_ACC : C_DIM);
        }
        gfx_text(F_COND12, 6, PLAT_H - 4, "SEL  the whole page", C_DIM);
        return;
    }
    {
        uint8_t c = (uint8_t)(fx_open + 1);
        const char *t = fx_help(c);
        int n = wrap(t, 0);
        gfx_text(F_COND20, 8, 42, fx_name(c), ui_fx_ink(c));
        gfx_text(F_COND14, 60, 41, fx_desc(c), C_DIM);
        gfx_fill(0, 50, PLAT_W, 1, C_TRACK, 255);
        if (!t)
            gfx_text(F_COND20, 8, FX_TEXT_TOP + 20, "no page yet", C_DIM);
        wrap(t, line_out);
        if (n > FX_LINES) { /* where the page is */
            int h = FX_LINES * FX_LINE * FX_LINES / n, y = FX_TEXT_TOP + FX_LINES * FX_LINE * scroll / n;
            gfx_fill(PLAT_W - 4, FX_TEXT_TOP, 2, FX_LINES * FX_LINE, C_TRACK, 255);
            gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
        }
    }
}

/* A page's length, and its item k: EFFECTS is the effects themselves,
 * their names and moondaw's line for each (core/song.c). */
static int page_len(void) { return page == HELP_EFFECTS ? FX_COUNT - 1 : pages[page].n; }
static Item page_item(int k)
{
    if (page == HELP_EFFECTS)
        return (Item){ fx_name((uint8_t)(k + 1)), fx_desc((uint8_t)(k + 1)) };
    return pages[page].items[k];
}

void help_open(int topic)
{
    page = -1;
    here = topic < 0 || topic >= HELP_TOPICS ? HELP_GESTURES : topic;
    cur = here;
    top = cur > LIST_ROWS - 1 ? cur - (LIST_ROWS - 1) : 0;
    prev_btns = 0xFFFFFFFFu; /* what is held on the way in is not a press */
    gfx_invalidate();
}

static void draw(void)
{
    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    if (page < 0) {
        gfx_text(F_COND12, 6, 14, "HELP", C_TITLE);
        for (int v = 0; v < LIST_ROWS && top + v < HELP_TOPICS; v++) {
            int i = top + v, y = LIST_TOP + v * GRID_ROW;
            bool on = i == cur;
            if (on)
                gfx_slab(2, y + 1, PLAT_W - 4, GRID_ROW - 2, C_ACCENT, 255);
            gfx_text(F_COND20, 14, y + 2 + FONTS[F_COND20].ascent, topic_word[i],
                     on ? C_ON_ACC : i == here ? C_TEXT : C_DIM);
        }
        if (here != HELP_GESTURES) /* why the cursor starts where it does */
            gfx_text(F_COND12, 6, PLAT_H - 4, "white: where you came from", C_DIM);
        return;
    }
    if (page == HELP_EFFECTS) {
        draw_effects();
        return;
    }
    {
        int n = page_len();
        int x = gfx_text(F_COND12, 6, 14, "HELP", C_TITLE);
        gfx_text(F_COND12, x + 6, 14, topic_word[page], C_TEXT);
        for (int v = 0; v < ITEMS && scroll + v < n; v++) {
            Item it = page_item(scroll + v);
            int y = PAGE_TOP + v * ITEM_H;
            gfx_text(F_COND20, 10, y + 20, it.key, C_TEXT);
            gfx_text(F_COND20, 10, y + 42, it.say, C_DIM);
        }
        if (n > ITEMS) { /* where the page is */
            int h = ITEMS * ITEM_H * ITEMS / n, y = PAGE_TOP + ITEMS * ITEM_H * scroll / n;
            gfx_fill(PLAT_W - 4, PAGE_TOP, 2, ITEMS * ITEM_H, C_TRACK, 255);
            gfx_fill(PLAT_W - 4, y, 2, h, C_DIM, 255);
        }
    }
}

bool help_frame(const PlatInput *in)
{
    uint32_t btns = in->btns & ~prev_btns;
    int d = in->enc[PE_SELECT] + in->enc[PE_K1];
    prev_btns = in->btns;

    if (page < 0) {
        cur += d;
        cur = cur < 0 ? 0 : cur >= HELP_TOPICS ? HELP_TOPICS - 1 : cur;
        grid_follow(&cur, &top, HELP_TOPICS, LIST_ROWS);
        if (btns & 1u << PB_SEL) {
            page = cur;
            scroll = 0;
            fx_open = -1;
            gfx_invalidate();
        }
        if (btns & 1u << PB_HOME) {
            gfx_frame(draw);
            return false;
        }
    } else if (page == HELP_EFFECTS && fx_open < 0) { /* the list of effects */
        fx_cur += d;
        fx_cur = fx_cur < 0 ? 0 : fx_cur >= FX_COUNT - 1 ? FX_COUNT - 2 : fx_cur;
        grid_follow(&fx_cur, &fx_top, FX_COUNT - 1, LIST_ROWS);
        if (btns & 1u << PB_SEL) {
            fx_open = fx_cur;
            scroll = 0;
        }
        if (btns & 1u << PB_HOME) {
            page = -1;
            gfx_invalidate();
        }
    } else if (page == HELP_EFFECTS) { /* an effect's page: KNOB 2 the one before or after */
        int n;
        if (in->enc[PE_K2]) {
            fx_open += in->enc[PE_K2];
            fx_open = fx_open < 0 ? 0 : fx_open >= FX_COUNT - 1 ? FX_COUNT - 2 : fx_open;
            fx_cur = fx_open;
            grid_follow(&fx_cur, &fx_top, FX_COUNT - 1, LIST_ROWS);
            scroll = 0;
        }
        n = wrap(fx_help((uint8_t)(fx_open + 1)), 0);
        scroll += d;
        scroll = scroll < 0 ? 0 : scroll > (n > FX_LINES ? n - FX_LINES : 0) ? (n > FX_LINES ? n - FX_LINES : 0) : scroll;
        if (btns & 1u << PB_HOME) /* back to the list, on it */
            fx_open = -1;
    } else {
        int last = page_len() > ITEMS ? page_len() - ITEMS : 0;
        scroll += d;
        scroll = scroll < 0 ? 0 : scroll > last ? last : scroll;
        if (btns & 1u << PB_HOME) { /* back to the list, on this topic */
            page = -1;
            gfx_invalidate();
        }
    }
    if ((btns >> PB_PLAY & 1u) && player_playing()) /* stop, from anywhere */
        player_stop();
    ui_led_state(PB_PLAY, player_playing());
    gfx_frame(draw);
    return true;
}
