/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG's hands, without a screen: the arrangement is edited as the
 * panel would (the grid of 28/10: SELECT the phrase, KNOB 2 the track or
 * the number, KNOB 3 the value, SEL the block or the section, SEQ a copy of
 * the phrase) and read back. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/grid.h"
#include "../app/loop.h"
#include "../app/songview.h"
#include "../app/undo.h"
#include "../app/ui.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
void plat_led_level(int l, int v) { (void)l; (void)v; }

/* the app's side, as far as this screen needs it */
Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;
static int saves;
void app_mark_dirty(void) { app_dirty = true; }
int app_save(void) { return saves++, 0; }
int app_not_carried(void) { return 0; }

static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 4\n"
    "pat t0 00 len 16\n00 C-4\npat t0 01 len 32\n00 E-4\npat t1 00 len 8\n00 G-4\n"
    "arr 00 steps auto\nname INTRO\nt0 00\nt1 00\n"
    "arr 01 steps auto\nt0 01\n"
    "arr 02 ref arr 01\n";

static PlatInput in;
static int fails, entry, track, last;
static void go(void) { last = songview_frame(&in, &entry, &track); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
/* `last` is what the press itself answered, not the release after it */
static void press(int b) { int r; in.btns = 1u << b; go(); r = last; in.btns = 0; go(); last = r; }
/* The SELECTION, in and out: EDIT held with OP5 under it (31/10). A black
 * key alone is a note here, so the first way in is always the shift; once
 * the mode is on the keys are commands and OP5 alone would do. */
static void sel_mode(void)
{
    in.btns = 1u << PB_EDIT;
    go();
    in.keys = 1u << UI_OP_SEL;
    go();
    in.keys = 0;
    go();
    in.btns = 0;
    go();
}
/* EDIT alone: the shift's CAPS LOCK, on or off (ui.h). */
static void tap_edit(void) { in.btns = 1u << PB_EDIT; go(); in.btns = 0; go(); }
static void keys(uint32_t held) { in.keys = held; go(); }
/* EDIT held, and a black key under it: the commands (ui.h) */
static void edit_key(int k)
{
    in.btns = 1u << PB_EDIT;
    go();
    in.keys = 1u << k;
    go();
    in.keys = 0;
    go();
    in.btns = 0;
    go();
}
/* A letter typed in the namer: KNOB 2 is what it is, KNOB 1 the next place. */
static void letter(char c)
{
    static const char LETTERS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    int i = 0;
    while (LETTERS[i] && LETTERS[i] != c)
        i++;
    enc(PE_K2, i);
    enc(PE_K1, 1);
}
/* the player takes PLAY and STOP in its render, as on the machine: a short
 * one makes what the hands just asked for true */
static void pump(void)
{
    static int16_t pcm[256 * 2];
    player_render(pcm, 256);
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
#define E(i) (app_song.arrangement[i])
/* The slots of the grid the page spends from phrase `top` down to `play`:
 * one for the phrase, one more where a section opens over it. Past
 * GRID_ROWS the phrase is below the floor, whatever the row count says. */
static int slots_to(int top, int play)
{
    int n = 0;
    for (int r = top; r <= play; r++)
        n += 1 + (E(r).section != SECTION_NONE && (r == 0 || E(r - 1).section != E(r).section));
    return n;
}

int main(void)
{
    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    songview_open(true);
    go();

    /* the cursor starts on phrase 00, track 1 */
    enc(PE_K4, -4);
    CHECK("KNOB 4 on a cell lowers its level, 4 a notch", E(0).level[0] == VOL_FULL - 16);
    enc(PE_K4, 100);
    CHECK("and stops at full", E(0).level[0] == VOL_FULL);
    enc(PE_K1, 1);
    CHECK("KNOB 1 moves to the next phrase, as SELECT does", songview_entry() == 1 && E(0).pattern[0] == 0);
    enc(PE_K1, -1);
    enc(PE_K3, 1);
    CHECK("KNOB 3 on a cell steps to the track's next block", E(0).pattern[0] == 1);
    enc(PE_K3, 5);
    CHECK("and stops one past the last block that holds something", E(0).pattern[0] == 2);
    enc(PE_K3, -9);
    CHECK("under block 00: --, and no further", E(0).pattern[0] == PATTERN_NONE);
    enc(PE_K3, 1);
    CHECK("back to block 00", E(0).pattern[0] == 0);

    press(PB_SEL);
    CHECK("SEL on a cell opens its block, in PATTERN", last == SONGVIEW_PATTERN && entry == 0 && track == 0);
    go();

    /* KNOB 3 chooses the block, and the ear follows it at rest: the panel of
     * blocks that stood in between is gone (30/10) */
    enc(PE_K3, 1);
    for (int i = 0; i < 5; i++)
        go();
    pump();
    CHECK("a block passed over is not played yet", !player_playing());
    for (int i = 0; i < 12; i++)
        go();
    pump();
    CHECK("the block the hand rests on plays by itself", player_playing() && player_soloed(0));
    press(PB_PLAY);
    pump();
    CHECK("PLAY ends the audition, the track back among the others", !player_playing() && !player_soloed(0));
    press(PB_PLAY);
    pump();
    CHECK("and the PLAY after it plays the song", player_playing() && !player_soloed(0));
    enc(PE_K3, -1);
    for (int i = 0; i < 20; i++)
        go();
    pump();
    CHECK("KNOB 3 while the song plays never interrupts it",
          player_playing() && !player_soloed(0) && E(0).pattern[0] == 0);
    press(PB_PLAY);
    pump();

    /* the new block: one only, and nothing kept while nothing is written in it */
    enc(PE_K3, 9);
    CHECK("KNOB 3 stops one past the last block that holds something", E(0).pattern[0] == 2);
    enc(PE_K3, 9);
    CHECK("and turning on from it makes no second new block", E(0).pattern[0] == 2);
    for (int i = 0; i < 14; i++)
        go();
    pump();
    CHECK("an empty block is silence: nothing is played for it", !player_playing());
    enc(PE_K3, -2);
    CHECK("turning back from it leaves nothing behind",
          E(0).pattern[0] == 0 && song_pattern_blank(&app_song, 0, 2));

    press(PB_SEQ);
    CHECK("SEQ on a block does nothing", last == SONGVIEW_STAY);
    /* EDIT: the SELECTION of blocks, the black keys its commands (30/10).
     * A tap of EDIT is the CAPS LOCK of that shift and nothing more: the
     * keys turn into commands, the SELECTION stays OP5's (31/10). */
    tap_edit();
    enc(PE_K3, 1);
    CHECK("a tap of EDIT locks no SELECTION on: the knob is still the cell's",
          E(0).pattern[0] == 1);
    keys(1u << UI_OP_SEL);    /* the keys being commands under it, OP5 alone reaches the mode */
    keys(0);
    enc(PE_K3, 1);
    CHECK("but the keys are commands, so OP5 alone opens the SELECTION",
          E(0).pattern[0] == 1);
    keys(1u << UI_OP_SEL);    /* the SELECTION off; the CAPS LOCK is still on */
    keys(0);
    tap_edit();               /* and off */
    enc(PE_K3, -1);
    CHECK("tapped again, the knob is the cell's once more", E(0).pattern[0] == 0);
    sel_mode();               /* on, anchored on phrase 00 / track 1 */
    enc(PE_K3, 1);
    CHECK("the knobs only move while the SELECTION is on", E(0).pattern[0] == 0);
    enc(PE_K1, 1);            /* stretched over phrases 00 and 01 */
    keys(1u << UI_OP_CLEAR);
    keys(0);
    CHECK("OP4 clears every cell the SELECTION covers",
          E(0).pattern[0] == PATTERN_NONE && E(1).pattern[0] == PATTERN_NONE);
    sel_mode();               /* off */
    enc(PE_K3, 1);
    CHECK("and the knobs are the cell's again", E(1).pattern[0] == 0);
    sel_mode();               /* on, anchored on phrase 01 */
    keys(1u << UI_OP_COPY);   /* OP1 takes the cells and collapses on the cursor, as PATTERN's */
    keys(0);
    enc(PE_K1, -1);
    keys(1u << UI_OP_PASTE);
    keys(0);
    CHECK("OP2 puts what OP1 took where the cursor is", E(0).pattern[0] == 0);
    sel_mode();               /* off */

    /* EDIT held: its commands while it is down, and no SELECTION left behind */
    in.btns = 1u << PB_EDIT;
    go();
    keys(1u << UI_OP_CLEAR);
    keys(0);
    in.btns = 0;
    go();
    CHECK("EDIT held gives the commands", E(0).pattern[0] == PATTERN_NONE);
    enc(PE_K3, 1);
    CHECK("and leaves no SELECTION on when it is let go", E(0).pattern[0] == 0);

    /* OP6: the phrases PLAY goes round (30/10) */
    sel_mode();               /* on, anchored on phrase 00 */
    enc(PE_K1, 1);            /* stretched over phrases 00 and 01 */
    keys(1u << UI_KEY_LOOP);
    keys(0);
    CHECK("OP6 makes the SELECTION's phrases the loop",
          loop_state() == LOOP_SET && loop_from() == 0 && loop_to() == 1);
    keys(1u << UI_KEY_LOOP);  /* nothing stretched any more: the third press forgets it */
    keys(0);
    CHECK("and OP6 again forgets it", loop_state() == LOOP_NONE);
    enc(PE_K1, -1);
    keys(1u << UI_KEY_LOOP);  /* one end, at the cursor */
    keys(0);
    CHECK("with nothing stretched, OP6 sets one end", loop_state() == LOOP_ONE_END && loop_from() == 0);
    keys(1u << UI_KEY_LOOP);  /* the other, the same phrase: 00 alone */
    keys(0);
    CHECK("and the press after it closes the loop",
          loop_state() == LOOP_SET && loop_from() == 0 && loop_to() == 0);
    sel_mode();               /* off */
    enc(PE_K1, 1);            /* the cursor on phrase 01, outside the loop */
    press(PB_PLAY);           /* the song, from where the cursor is */
    pump();
    CHECK("PLAY with a loop over phrases starts in it", player_entry() == 0);
    for (int i = 0; i < 500; i++) /* 2.9 s: past the 16 steps of phrase 00 */
        pump();
    CHECK("and the song goes round them instead of running on", player_playing() && player_entry() == 0);
    press(PB_PLAY);
    pump();

    /* STOP leaves the cursor on the phrase that was playing (30/10): the song
     * runs from phrase 01 while the hand walks back to 00 */
    loop_clear();             /* the loop set above would hold the song on phrase 00 */
    enc(PE_K1, -20);          /* wherever the cursor was left: on phrase 00, then 01 */
    enc(PE_K1, 1);
    press(PB_PLAY);
    pump();
    CHECK("PLAY starts from the cursor's phrase", player_playing() && player_entry() == 1);
    enc(PE_K1, -1);
    CHECK("the cursor moves while it plays, the song is not followed", songview_entry() == 0);
    press(PB_PLAY);
    pump();
    CHECK("STOP puts the cursor where the song stopped", !player_playing() && songview_entry() == 1);
    enc(PE_K1, -1);

    enc(PE_K2, -1);           /* the phrase's number */
    press(PB_SEQ);
    CHECK("SEQ on the number copies the phrase right below, without its name",
          app_song.arrange_len == 4 && E(1).pattern[0] == 0 && !E(1).name[0] && songview_entry() == 1);
    CHECK("and the reference still points at the phrase it pointed at",
          E(3).kind == ENTRY_REF && E(3).ref_target == 2);
    press(PB_LFO);
    CHECK("one LFO deletes nothing", app_song.arrange_len == 4);
    press(PB_LFO);
    CHECK("two delete the phrase, and the reference follows its target up",
          app_song.arrange_len == 3 && E(2).ref_target == 1);

    enc(PE_SELECT, -5);
    enc(PE_K4, 1);
    CHECK("KNOB 4 on the number: up from AUTO starts at the phrase's own length", E(0).steps == 16);
    enc(PE_K4, -4);
    CHECK("and is then a number", E(0).steps == 12);
    enc(PE_K4, -20);
    CHECK("all the way down is AUTO again", E(0).steps == 0);

    press(PB_SEL);
    CHECK("SEL on the number asks for its SECTION", last == SONGVIEW_PHRASE && entry == 0);
    go();
    enc(PE_ALGO, -1);
    CHECK("ALGO to the left takes the last edit back", E(0).steps != 0 || app_song.arrange_len == 3);
    press(PB_SAVE);
    CHECK("SAVE saves", saves == 1);
    press(PB_FX);
    CHECK("FX asks for the MIXER", last == SONGVIEW_MIXER);
    go();
    press(PB_GLO);
    CHECK("GLO asks for the PROJECT", last == SONGVIEW_PROJECT);
    go();
    press(PB_HOME);
    CHECK("HOME goes back to HOME's root", last == SONGVIEW_BACK);
    go();
    sel_mode();
    keys(1u << UI_OP_CLEAR);
    keys(0);
    CHECK("EDIT on the number takes whole phrases: OP4 clears every track of it",
          E(0).pattern[0] == PATTERN_NONE && E(0).pattern[1] == PATTERN_NONE);


    /* CHOOSE PATTERN, which PRESETS opens (30/10): the list came back for
     * what a bare number never said and for NEW; KNOB 3 is untouched. */
    sel_mode();               /* the SELECTION off */
    enc(PE_K2, 1);            /* the cursor on track 1's cell */
    enc(PE_K3, -30);
    CHECK("the cell is empty to start with", E(0).pattern[0] == PATTERN_NONE);
    enc(PE_K2, -1);           /* the phrase's number: no cell, no list */
    enc(PE_PRESET, 1);
    press(PB_HOME);
    CHECK("PRESETS on the phrase's number opens nothing", last == SONGVIEW_BACK);
    go();
    enc(PE_K2, 1);
    enc(PE_PRESET, 1);        /* the list: --, 00, 01, NEW */
    press(PB_HOME);
    CHECK("HOME leaves the list, and leaves the cell as it was",
          last == SONGVIEW_STAY && E(0).pattern[0] == PATTERN_NONE);
    enc(PE_PRESET, 1);
    enc(PE_SELECT, 1);        /* its second row: block 00 */
    press(PB_SEL);
    CHECK("SEL takes the block the cursor is on and closes the list",
          last == SONGVIEW_STAY && E(0).pattern[0] == 0);
    /* PLAY in the list: the block the cursor rests on, alone -- the ear the
     * panel of 29/10 had, which is what he missed when it went (30/10) */
    enc(PE_PRESET, 1);        /* it opens on block 00, the cell's own */
    enc(PE_SELECT, 1);        /* block 01, which the cell does not hold */
    press(PB_PLAY);
    pump();
    CHECK("PLAY listens to the block the cursor is on, the track alone",
          player_playing() && player_soloed(0));
    press(PB_HOME);
    pump();
    CHECK("HOME ends the ear and gives the cell back what it held",
          !player_playing() && !player_soloed(0) && E(0).pattern[0] == 0);
    enc(PE_PRESET, 1);
    enc(PE_SELECT, 1);
    press(PB_PLAY);
    pump();
    press(PB_SEL);
    pump();
    CHECK("SEL while it sounds takes that block and ends the ear",
          !player_playing() && !player_soloed(0) && E(0).pattern[0] == 1);

    CHECK("blocks 00 and 01 hold something and 02 does not",
          !song_pattern_blank(&app_song, 0, 0) && !song_pattern_blank(&app_song, 0, 1) &&
              song_pattern_blank(&app_song, 0, 2));
    {
        bool named = false;
        for (int i = 0; i < app_song.arrange_len; i++)
            named = named || E(i).pattern[0] == 2;
        CHECK("and no phrase names 02", !named);
    }
    {   /* a block shortened once keeps what lies past its length and never
         * saves it: a block called new must not hand it back */
        Step *ghost = song_step_w(&app_song, 0, 2, 10);
        if (ghost)
            ghost->notes[0] = 72;
        app_song.patterns[0][2].length = 8;
        CHECK("block 02 is blank within its length, with a ghost past it",
              song_pattern_blank(&app_song, 0, 2));
    }
    enc(PE_PRESET, 1);
    enc(PE_SELECT, 20);       /* its last row, however long the list is: NEW */
    press(PB_SEL);
    CHECK("NEW takes the first number the track does not use", E(0).pattern[0] == 2);
    CHECK("as long as a fresh block and with nothing in it, the ghost rubbed out",
          app_song.patterns[0][2].length == PATTERN_DEF_STEPS && song_pattern_blank(&app_song, 0, 2) &&
              step_is_empty(song_step(&app_song, 0, 2, 10)));
    /* what one UNDO takes back of it: undo_test, where the clock runs */

    /* The reference, made by hand (31/10): KNOB 3 on the number says what
     * the row IS, KNOB 3 on the reference where it points, KNOB 4 how long
     * it plays, PRESETS the list of what it could point at, SEL the phrase
     * it plays. The journal is told what a conversion drops, so one ALGO to
     * the left is one turn too many taken back -- here the clock has to be
     * moved by hand for the gestures to be separate ones. */
    {
        uint32_t t = 1000;
        int before;

        undo_reset(&app_song); /* the journal: nothing above here used it */
        enc(PE_SELECT, -20);  /* phrase 00, the top of the song */
        enc(PE_K2, -1);       /* its number */
        press(PB_SEQ);        /* two copies below it: something to point at */
        press(PB_SEQ);
        enc(PE_SELECT, -20);
        before = app_song.arrange_len;
        CHECK("phrase 00 holds blocks of its own to start with",
              E(0).kind == ENTRY_PHRASE && E(0).pattern[0] == 2 && before >= 4);

        undo_frame(t += 1000);
        enc(PE_K3, 1);
        CHECK("KNOB 3 on the number turns the phrase into a reference, pointing below it",
              E(0).kind == ENTRY_REF && E(0).ref_kind == REF_PHRASE && E(0).ref_target == 1);
        CHECK("and the blocks it held are gone, the row keeping its place",
              E(0).pattern[0] == PATTERN_NONE && app_song.arrange_len == before);

        undo_frame(t += 1000);
        enc(PE_K3, 1);
        CHECK("another detent, either way, is a phrase again -- an empty one",
              E(0).kind == ENTRY_PHRASE && E(0).pattern[0] == PATTERN_NONE);

        undo_frame(t += 1000);
        enc(PE_ALGO, -1);
        CHECK("ALGO to the left takes the conversion back", E(0).kind == ENTRY_REF);

        undo_frame(t += 1000);
        {   /* a row others point at cannot become a reference without
             * something going quiet: the screen says so */
            int tgt = E(0).ref_target;
            enc(PE_SELECT, 40);
            enc(PE_SELECT, -40);
            CHECK("a phrase a reference points at is still a phrase", E(tgt).kind == ENTRY_PHRASE);
            enc(PE_SELECT, tgt);
            enc(PE_K2, -1);
            enc(PE_K3, 1);
            CHECK("turning it into a reference too is allowed, and said out loud",
                  app_song.arrangement[tgt].kind == ENTRY_REF);
            enc(PE_K3, 1); /* back to a phrase, and back to the reference above */
            enc(PE_SELECT, -40);
            enc(PE_K2, -1);
        }
        enc(PE_K2, 1);
        CHECK("KNOB 2 walks onto the reference's one cell, and no further",
              songview_entry() == 0 && E(0).kind == ENTRY_REF);
        enc(PE_K2, 5);
        enc(PE_K3, 1);
        CHECK("KNOB 3 on the cell walks where it points", E(0).ref_target == 2);
        enc(PE_K3, -20);
        CHECK("and stops at the first of them", E(0).ref_target == 1);

        undo_frame(t += 1000);
        enc(PE_K4, 1);
        CHECK("KNOB 4 on a reference is how long it plays, up from AUTO", E(0).steps > 0);
        enc(PE_K4, -40);
        CHECK("and all the way down is AUTO again", E(0).steps == 0);

        enc(PE_PRESET, 1);    /* CHOOSE PHRASE */
        press(PB_HOME);
        CHECK("HOME leaves the list and the reference as it was",
              last == SONGVIEW_STAY && E(0).ref_target == 1);
        enc(PE_PRESET, 1);
        enc(PE_SELECT, 1);
        press(PB_SEL);
        CHECK("SEL takes the phrase the cursor is on and closes the list",
              last == SONGVIEW_STAY && E(0).ref_target == 2);
        enc(PE_PRESET, 1);
        press(PB_PLAY);
        pump();
        CHECK("PLAY listens to the phrase the cursor rests on", player_playing());
        press(PB_HOME);
        pump();
        CHECK("HOME ends the ear", !player_playing());

        {
            int at = song_entry_resolve(&app_song, 0, 0);
            press(PB_SEL);
            CHECK("SEL on a reference goes to the phrase it plays, and opens no list",
                  last == SONGVIEW_STAY && at == 2 && songview_entry() == at);
        }
    }

    /* The SECTION, made by hand (31/10): a run of consecutive phrases with
     * a name on it, which until today could only arrive in a file. EDIT
     * takes the run, ENV envelops it; the marker over the row it opens is
     * a place the cursor stops on, where KNOB 4 says how many phrases it
     * holds, SEL names it and EDIT + OP4 clears it away. */
    {
        uint32_t t = 100000;
        int base, r0;

        undo_frame(t += 1000);
        enc(PE_SELECT, 40); /* the last phrase, then three fresh ones under it */
        enc(PE_K2, -1);
        while (app_song.arrangement[songview_entry()].kind == ENTRY_REF)
            enc(PE_SELECT, -1);
        press(PB_SEQ);
        press(PB_SEQ);
        press(PB_SEQ);
        base = app_song.arrange_len;
        r0 = base - 3;
        CHECK("three phrases of their own, in no section",
              E(r0).section == SECTION_NONE && E(r0 + 1).section == SECTION_NONE &&
                  E(r0 + 2).section == SECTION_NONE && app_song.section_count == 0);

        /* EDIT on a number takes whole phrases; the cursor stretches it */
        enc(PE_SELECT, 40);
        enc(PE_SELECT, -2);
        enc(PE_K2, -1);
        sel_mode();
        enc(PE_SELECT, 2);
        undo_frame(t += 1000);
        press(PB_ENV);
        CHECK("ENV leaves the song as it was until SEL takes something",
              app_song.section_count == 0);
        press(PB_SEL); /* the list opens on NEW: these rows are in no section */
        CHECK("SEL on NEW makes a section of the phrases the selection covered",
              app_song.section_count == 1 && E(r0).section == 0 && E(r0 + 1).section == 0 &&
                  E(r0 + 2).section == 0);
        CHECK("and the phrases it holds are still phrases",
              app_song.arrange_len == base && E(r0).kind == ENTRY_PHRASE);
        press(PB_HOME); /* the namer opened on it: HOME leaves it unnamed */
        CHECK("the namer opens on a new section, and HOME leaves it without a name",
              app_song.sections[0][0] == 0 && app_song.section_count == 1);

        sel_mode(); /* the selection away: the cursor alone from here */
        enc(PE_SELECT, -40);
        enc(PE_SELECT, r0 - 1);
        enc(PE_SELECT, 1); /* onto the marker, which is a slot of its own */
        CHECK("the cursor stops on the marker, over the row the section opens at",
              songview_entry() == r0);

        undo_frame(t += 1000);
        enc(PE_K3, 1);
        CHECK("KNOB 3 on a marker turns nothing: a section has no other kind",
              E(r0).kind == ENTRY_PHRASE);
        enc(PE_K4, -1);
        CHECK("KNOB 4 to the left gives the last phrase back",
              E(r0 + 2).section == SECTION_NONE && E(r0).section == 0 && E(r0 + 1).section == 0);
        enc(PE_K4, 1);
        CHECK("and to the right it swallows the row below again", E(r0 + 2).section == 0);
        enc(PE_K4, 40);
        CHECK("it stops at the end of the song, taking no phrase that is not there",
              app_song.arrange_len == base && E(base - 1).section == 0);

        undo_frame(t += 1000);
        press(PB_LFO); /* LFO is the phrase's, never the section's */
        press(PB_LFO);
        CHECK("LFO on a marker deletes nothing", app_song.arrange_len == base);

        press(PB_SEL); /* SEL on a marker: its name, typed */
        letter('O');
        letter('U');
        letter('T');
        press(PB_SEL);
        CHECK("SEL on a marker names the section",
              app_song.sections[0][0] == 'O' && app_song.sections[0][1] == 'U' &&
                  app_song.sections[0][2] == 'T' && app_song.sections[0][3] == 0);

        /* what a save and a load make of it: the file has carried sections
         * from the first day, and what the panel writes has to survive one */
        {
            static char out[1 << 18];
            static Song back;
            int32_t m = trk_save(&app_song, out, sizeof out);
            CHECK("the song saves", m > 0);
            CHECK("and comes back with its section and its name",
                  m > 0 && trk_load(&back, out, (uint32_t)m, NULL) == TRK_OK &&
                      back.section_count == 1 && back.sections[0][0] == 'O' &&
                      back.arrangement[r0].section == 0 && back.arrangement[base - 1].section == 0);
        }

        undo_frame(t += 1000);
        edit_key(UI_OP_CLEAR); /* EDIT held + OP4: the section goes, its phrases stay */
        CHECK("OP4 on a marker clears the section away and leaves its phrases",
              app_song.section_count == 0 && app_song.arrange_len == base &&
                  E(r0).section == SECTION_NONE && E(r0).kind == ENTRY_PHRASE);
        enc(PE_ALGO, -1);
        CHECK("and ALGO to the left brings it back", app_song.section_count == 1);
    }

    /* The view follows the phrase that plays through the markers too
     * (1/11): a marker costs a slot of the grid, so the eight slots hold
     * fewer than eight phrases as soon as a section opens on the page.
     * Counting rows, the view sat still while the playhead was already
     * below the last slot -- one phrase for every marker above it -- and
     * then jumped past them. LEOMOTEUR, its INTRO and its VERSE. */
    {
        static const char marked[] =
            "version 3\nbpm 120\nticks 6\ntracks 1\n"
            "pat t0 00 len 16\n00 C-4\n"
            "sec 00\nname INTRO\n"
            "arr 00 steps auto\nt0 00\narr 01 steps auto\nt0 00\narr 02 steps auto\nt0 00\n"
            "sec ---\narr 03 steps auto\nt0 00\n"
            "sec 01\nname VERSE\n"
            "arr 04 steps auto\nt0 00\narr 05 steps auto\nt0 00\narr 06 steps auto\nt0 00\n"
            "sec ---\n"
            "arr 07 steps auto\nt0 00\narr 08 steps auto\nt0 00\narr 09 steps auto\nt0 00\n"
            "arr 0A steps auto\nt0 00\narr 0B steps auto\nt0 00\n";

        trk_load(&app_song, marked, sizeof marked - 1, NULL);
        player_init(&app_song);
        songview_open(true);
        CHECK("the marked song comes back whole", app_song.arrange_len == 12 && app_song.section_count == 2);

        /* INTRO over phrase 00 and VERSE over 04: from the top of the song
         * the eight slots hold 00 to 05, and 06 is already off the page */
        player_play_song(6, false);
        pump();
        for (int i = 0; i <= GRID_FOLLOW_IDLE; i++) /* the hand still: the view is the song's */
            go();
        CHECK("the page turns on the phrase that plays, the markers counted",
              player_entry() == 6 && songview_top() <= 6 && slots_to(songview_top(), 6) <= GRID_ROWS);
        CHECK("and the page it turns to is a full one", songview_top() == 4);

        /* the last phrase: the page comes up to fill the grid, but never so
         * far that the marker above pushes the playhead off its foot */
        player_play_song(11, false);
        pump();
        for (int i = 0; i <= GRID_FOLLOW_IDLE; i++)
            go();
        CHECK("the last phrase of a marked song stays on the page",
              songview_top() == 5 && slots_to(songview_top(), 11) <= GRID_ROWS);
        player_stop();
        pump();
    }

    printf("song test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
