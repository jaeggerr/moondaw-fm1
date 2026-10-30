/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG's hands, without a screen: the arrangement is edited as the
 * panel would (the grid of 28/10: SELECT the phrase, KNOB 2 the track or
 * the number, KNOB 3 the value, SEL the block or the section, SEQ a copy of
 * the phrase) and read back. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/loop.h"
#include "../app/songview.h"
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
/* EDIT is also a shift: it answers at the release */
static void tap_edit(void) { in.btns = 1u << PB_EDIT; go(); in.btns = 0; go(); }
static void keys(uint32_t held) { in.keys = held; go(); }
/* the player takes PLAY and STOP in its render, as on the machine: a short
 * one makes what the hands just asked for true */
static void pump(void)
{
    static int16_t pcm[256 * 2];
    player_render(pcm, 256);
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
#define E(i) (app_song.arrangement[i])

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
    /* EDIT: the SELECTION of blocks, the black keys its commands (30/10) */
    tap_edit();               /* on, anchored on phrase 00 / track 1 */
    enc(PE_K3, 1);
    CHECK("the knobs only move while the SELECTION is on", E(0).pattern[0] == 0);
    enc(PE_K1, 1);            /* stretched over phrases 00 and 01 */
    keys(1u << UI_OP_CLEAR);
    keys(0);
    CHECK("OP4 clears every cell the SELECTION covers",
          E(0).pattern[0] == PATTERN_NONE && E(1).pattern[0] == PATTERN_NONE);
    tap_edit();               /* off */
    enc(PE_K3, 1);
    CHECK("and the knobs are the cell's again", E(1).pattern[0] == 0);
    tap_edit();               /* on, anchored on phrase 01 */
    keys(1u << UI_OP_COPY);   /* OP1 takes the cells and collapses on the cursor, as PATTERN's */
    keys(0);
    enc(PE_K1, -1);
    keys(1u << UI_OP_PASTE);
    keys(0);
    CHECK("OP2 puts what OP1 took where the cursor is", E(0).pattern[0] == 0);
    tap_edit();               /* off */

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
    tap_edit();               /* on, anchored on phrase 00 */
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
    tap_edit();               /* off */
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
    enc(PE_K3, 1);
    CHECK("KNOB 3 on the number: up from AUTO starts at the phrase's own length", E(0).steps == 16);
    enc(PE_K3, -4);
    CHECK("and is then a number", E(0).steps == 12);
    enc(PE_K3, -20);
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
    tap_edit();
    keys(1u << UI_OP_CLEAR);
    keys(0);
    CHECK("EDIT on the number takes whole phrases: OP4 clears every track of it",
          E(0).pattern[0] == PATTERN_NONE && E(0).pattern[1] == PATTERN_NONE);

    printf("song test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
