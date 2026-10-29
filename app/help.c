/* SPDX-License-Identifier: GPL-3.0-only */
#include "help.h"

#include "../core/song.h"
#include "gfx.h"
#include "grid.h"
#include "theme.h"

/* A gesture: the control, and what it does -- each 24 characters at most,
 * a line of CONDENSED 20 on the panel. Written from the screens' own
 * comments: a change there is a change here. */
typedef struct {
    const char *key, *say;
} Item;

static const Item home_items[] = {
    { "SELECT, KNOB 1", "move" },
    { "SEL", "go: CONTINUE, PROJECTS..." },
    { "PLAY on CONTINUE", "play the open project" },
    { "in PROJECTS: SEL", "open the project" },
    { "in PROJECTS: PLAY", "play it, staying here" },
    { "HOME", "back to the menu" },
};
static const Item song_items[] = {
    { "SELECT", "phrase, onto its number" },
    { "KNOB 1", "phrase" },
    { "KNOB 2", "track" },
    { "KNOB 3 on a cell", "its pattern" },
    { "KNOB 3 on a number", "the phrase's length" },
    { "SEL on a cell", "choose a pattern" },
    { "  in it: PLAY", "listen as you move" },
    { "SEL on a number", "the section" },
    { "EDIT on a cell", "edit the pattern" },
    { "EDIT on a number", "the section" },
    { "SEQ on a number", "copy the phrase below" },
    { "LFO on a number", "delete the phrase: twice" },
    { "EDIT held + MONO", "solo; held: mute" },
    { "FX", "the mixer" },
    { "GLO", "the project" },
    { "PLAY", "the song" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back to the menu" },
};
static const Item section_items[] = {
    { "SELECT, KNOB 1", "step" },
    { "KNOB 2", "track" },
    { "keys", "write the note" },
    { "OCT- OCT+", "the keyboard's octave" },
    { "SEL, EDIT", "the track's pattern" },
    { "EDIT held + MONO", "solo; held: mute" },
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
    { "KNOB 3", "value" },
    { "KNOB 4", "the effect's type" },
    { "SEQ, FX, ENV", "notes, effects, instr." },
    { "ENV on instruments", "edit the instrument" },
    { "SEL on an effect", "the syllable" },
    { "SEL on instruments", "choose an instrument" },
    { "keys", "write in a note column" },
    { "keys elsewhere", "play, write nothing" },
    { "OCT- OCT+", "the keyboard's octave" },
    { "EDIT", "selection, in and out" },
    { "EDIT held", "black keys: commands" },
    { "OP1 OP2 OP3", "copy, paste, cut" },
    { "OP4", "clear" },
    { "OP6", "loop the selection" },
    { "MONO", "solo; held: mute" },
    { "ALGO", "undo left, redo right" },
    { "PLAY", "the phrase, round" },
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
    { "PLAY", "the song" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back" },
};
static const Item project_items[] = {
    { "SELECT, KNOB 1", "setting" },
    { "KNOB 3", "its value" },
    { "SEL on NAME", "name the project" },
    { "FX", "the mixer" },
    { "PLAY", "the song" },
    { "ALGO", "undo left, redo right" },
    { "HOME", "back" },
};
static const Item settings_items[] = {
    { "SELECT, KNOB 1", "setting" },
    { "KNOB 3", "its value" },
    { "LEDS", "off, state, guide" },
    { "LIGHT", "how bright the guide" },
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
    PAGE(home_items),     PAGE(song_items),       PAGE(section_items), PAGE(pattern_items),
    PAGE(syllable_items), PAGE(instrument_items), PAGE(mixer_items),   PAGE(project_items),
    PAGE(settings_items), PAGE(gestures_items),
};
static const char *const topic_word[HELP_TOPICS] = { "HOME",  "SONG",    "SECTION",  "PATTERN",  "SYLLABLE", "INSTRUMENT",
                                                     "MIXER", "PROJECT", "SETTINGS", "GESTURES", "EFFECTS" };

enum { LIST_TOP = 18, LIST_ROWS = 7, PAGE_TOP = 22, ITEM_H = 52, ITEMS = 4 };
static int page = -1, cur, top, scroll, here;
static uint32_t prev_btns;

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
            gfx_invalidate();
        }
        if (btns & 1u << PB_HOME) {
            gfx_frame(draw);
            return false;
        }
    } else {
        int last = page_len() > ITEMS ? page_len() - ITEMS : 0;
        scroll += d;
        scroll = scroll < 0 ? 0 : scroll > last ? last : scroll;
        if (btns & 1u << PB_HOME) { /* back to the list, on this topic */
            page = -1;
            gfx_invalidate();
        }
    }
    gfx_frame(draw);
    return true;
}
