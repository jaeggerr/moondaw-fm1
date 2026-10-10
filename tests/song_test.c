/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG's hands, without a screen: the arrangement is edited as the
 * panel would and read back. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/songview.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }

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
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
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
    CHECK("KNOB 1 on a cell steps to the track's next block", E(0).pattern[0] == 1);
    enc(PE_K1, 5);
    CHECK("and stops one past the last block that holds something", E(0).pattern[0] == 2);
    enc(PE_K1, -3);
    CHECK("under block 00: ON", E(0).pattern[0] == PATTERN_HOLD);
    enc(PE_K2, -27);
    CHECK("KNOB 2 is the cell's level", E(0).level[0] == 100);
    enc(PE_K1, -1);
    CHECK("under ON: empty, and the level goes with it",
          E(0).pattern[0] == PATTERN_NONE && E(0).level[0] == VOL_FULL);
    enc(PE_K1, 2);
    CHECK("back to block 00", E(0).pattern[0] == 0);

    enc(PE_ALGO, 2);            /* track 3, which has no block at all */
    enc(PE_K1, 9);
    CHECK("a track with nothing gets ON, then a fresh block 00", E(0).pattern[2] == 0);
    press(PB_LFO);
    CHECK("CLEAR empties the cell", E(0).pattern[2] == PATTERN_NONE);

    enc(PE_ALGO, -2);
    press(PB_EDIT);
    CHECK("OPEN on a block asks for PATTERN on that phrase and track", entry == 0 && track == 0);

    /* the phrase's number */
    enc(PE_ALGO, -9);
    enc(PE_K1, 1);
    CHECK("turning up from AUTO starts at the phrase's own length", E(0).steps == 16);
    enc(PE_K1, -4);
    CHECK("and is then a number", E(0).steps == 12);
    enc(PE_K1, -100);
    CHECK("all the way down is AUTO again", E(0).steps == 0);

    press(PB_SEQ);
    CHECK("+ PHRASE copies the phrase right below, without its name",
          app_song.arrange_len == 4 && E(1).pattern[0] == 0 && E(1).pattern[1] == 0 &&
          E(1).name[0] == '\0' && !strcmp(E(0).name, "INTRO"));
    CHECK("and the reference still points at the phrase it pointed at",
          E(3).kind == ENTRY_REF && E(3).ref_target == 2 && E(2).pattern[0] == 1);

    press(PB_LFO);
    enc(PE_SELECT, 1);
    CHECK("one DELETE deletes nothing", app_song.arrange_len == 4);
    enc(PE_SELECT, -1);         /* back on the copy, row 01 */
    press(PB_LFO);
    press(PB_LFO);
    CHECK("two delete the phrase, and the reference follows its target up",
          app_song.arrange_len == 3 && E(1).pattern[0] == 1 && E(2).ref_target == 1);
    press(PB_LFO);
    press(PB_LFO);              /* the phrase a reference points at */
    CHECK("a reference whose phrase is gone becomes an empty phrase",
          app_song.arrange_len == 2 && E(1).kind == ENTRY_PHRASE && E(1).pattern[0] == PATTERN_NONE);

    /* the song's own line */
    enc(PE_SELECT, -9);
    enc(PE_K1, 7);
    enc(PE_K2, 16);
    enc(PE_K3, 6);
    enc(PE_K4, -1);
    CHECK("on the song's line the four knobs are BPM, SWING, TICKS, TRACKS",
          app_song.bpm == 127 && app_song.swing == 66 && app_song.ticks_per_step == 12 &&
          app_song.num_tracks == 3);
    enc(PE_K2, 100);
    CHECK("swing stops at 75", app_song.swing == SWING_MAX);

    press(PB_SAVE);
    CHECK("SAVE saves", saves == 1);
    in.btns = 1u << PB_HOME;
    go();
    CHECK("HOME leaves for the MENU", last == SONGVIEW_MENU);

    printf("song test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
