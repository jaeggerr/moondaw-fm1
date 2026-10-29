/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG's hands, without a screen: the arrangement is edited as the
 * panel would (the grid of 28/10: SELECT the phrase, KNOB 2 the track or
 * the number, KNOB 3 the value, SEL the panel or the section, SEQ the
 * block) and read back. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/songview.h"
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
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
#define E(i) (app_song.arrangement[i])

int main(void)
{
    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    songview_open(true);
    go();

    /* the cursor starts on phrase 00, track 1 */
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

    press(PB_SEL);            /* the panel of track 1's blocks, on 00 */
    enc(PE_SELECT, 1);
    press(PB_GLO);
    CHECK("GLO closes the panel and changes nothing", E(0).pattern[0] == 0);
    press(PB_SEL);
    enc(PE_SELECT, 1);
    press(PB_PLAY);
    CHECK("PLAY in the panel plays that block alone, in the phrase",
          E(0).pattern[0] == 1 && player_soloed(0));
    press(PB_GLO);
    CHECK("and GLO puts back what was there", E(0).pattern[0] == 0 && !player_soloed(0));
    press(PB_SEL);            /* listening: the cursor's block plays once it rests */
    press(PB_PLAY);
    CHECK("PLAY listens from the block under the cursor", E(0).pattern[0] == 0 && player_soloed(0));
    enc(PE_SELECT, 1);
    for (int i = 0; i < 5; i++)
        go();
    CHECK("passing over a block starts nothing yet", E(0).pattern[0] == 0);
    for (int i = 0; i < 12; i++)
        go();
    CHECK("resting on it plays it", E(0).pattern[0] == 1 && player_soloed(0));
    enc(PE_SELECT, -2);
    for (int i = 0; i < 12; i++)
        go();
    CHECK("-- is silence, the listening still on", E(0).pattern[0] == 0 && !player_soloed(0));
    press(PB_PLAY);
    enc(PE_SELECT, 2);
    for (int i = 0; i < 12; i++)
        go();
    CHECK("PLAY again: the cursor plays nothing", E(0).pattern[0] == 0 && !player_soloed(0));
    press(PB_GLO);
    press(PB_SEL);
    enc(PE_SELECT, -1);
    press(PB_SEL);
    CHECK("SEL takes the row: -- first", E(0).pattern[0] == PATTERN_NONE);
    press(PB_SEL);
    enc(PE_SELECT, 1);
    press(PB_SEL);
    CHECK("and a block", E(0).pattern[0] == 0);

    press(PB_SEQ);
    CHECK("SEQ on a block does nothing", last == SONGVIEW_STAY);
    press(PB_EDIT);
    CHECK("EDIT on a block asks for it, in PATTERN", last == SONGVIEW_PATTERN && entry == 0 && track == 0);
    go();
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
    press(PB_EDIT);
    CHECK("EDIT on the number asks for the SECTION", last == SONGVIEW_PHRASE);

    printf("song test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
