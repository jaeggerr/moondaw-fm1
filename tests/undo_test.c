/* SPDX-License-Identifier: GPL-3.0-only */
/* UNDO through the hands that make the edits: what one press takes
 * back, what counts as one gesture, and what survives the pool being
 * compacted and the journal filling up. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/pattern.h"
#include "../app/phrase.h"
#include "../app/songview.h"
#include "../app/undo.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
void plat_led_level(int l, int v) { (void)l; (void)v; }

Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;
void app_mark_dirty(void) { app_dirty = true; }
int app_save(void) { return 0; }
int app_not_carried(void) { return 0; }

static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 2\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10\n"
    "pat t0 00 len 64\n00 C-4 i00\n"
    "pat t0 01 len 16\n00 E-4 i00\n"
    "pat t1 00 len 16\n00 G-3 i00\n"
    "arr 00 steps auto\nt0 00\nt1 00\n"
    "arr 01 steps auto\nt0 01\n"
    "arr 02 steps auto\nt0 00\n";

static PlatInput in;
static int fails;
static enum { ON_SECTION, ON_PATTERN, ON_SONG } on;
static void go(void)
{
    int e, t;
    undo_frame(in.ms);
    if (on == ON_SONG)
        songview_frame(&in, &e, &t);
    else if (on == ON_PATTERN)
        pattern_frame(&in);
    else
        phrase_frame(&in);
    memset(in.enc, 0, sizeof in.enc);
    in.ms += 16;
}
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
static void key(int k) { in.keys = 1u << k; go(); in.keys = 0; go(); }
static void wait(int ms) { in.ms += (uint32_t)ms; go(); }
static void undo(void) { enc(PE_ALGO, -1); }
static void redo(void) { enc(PE_ALGO, 1); }
static const Step *st(int row) { return song_step(&app_song, 0, 0, row); }
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

int main(void)
{
    static SongEntry before[SONG_ARRANGE_LEN];
    int n;

    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    undo_reset(&app_song);
    phrase_open(0, 0);
    go();
    CHECK("a song just opened has nothing to take back", !undo_can());

    enc(PE_SELECT, 3);
    key(7);
    CHECK("a key wrote a note", st(3)->notes[0] == 60 && undo_can());
    undo();
    CHECK("UNDO takes it back", step_is_empty(st(3)) && !undo_can());

    /* a knob turned over several frames is one gesture: KNOB 3 on a note in PATTERN */
    wait(2000);
    on = ON_PATTERN;
    pattern_open(&app_song, 0, 0);
    go();
    for (int i = 0; i < 12; i++)
        enc(PE_K3, 1);
    CHECK("twelve notches up", st(0)->notes[0] == 72);
    undo();
    CHECK("are taken back by one press", st(0)->notes[0] == 60 && !undo_can());

    /* the same place a second later is another gesture */
    wait(2000);
    enc(PE_K3, 2);
    wait(2000);
    enc(PE_K3, 3);
    undo();
    CHECK("a pause makes two gestures: the second goes first", st(0)->notes[0] == 62);
    undo();
    CHECK("then the first", st(0)->notes[0] == 60 && !undo_can());
    redo();
    CHECK("REDO does the first again", st(0)->notes[0] == 62 && undo_can_redo());
    redo();
    CHECK("then the second", st(0)->notes[0] == 65 && !undo_can_redo());
    undo();
    CHECK("UNDO again after a REDO", st(0)->notes[0] == 62);
    wait(2000);
    enc(PE_K3, 1);
    CHECK("an edit after an UNDO forgets what could be done again", st(0)->notes[0] == 63 && !undo_can_redo());
    undo();
    undo();
    CHECK("and the journal goes back as before", st(0)->notes[0] == 60 && !undo_can());

    wait(2000);
    enc(PE_K3, 100);
    enc(PE_K3, 100);
    n = st(0)->notes[0];
    wait(2000);
    enc(PE_K3, 100); /* against the stop: nothing changes */
    go();
    undo();
    CHECK("a knob against its stop is not a gesture", st(0)->notes[0] == 60);
    CHECK("(it was at the top)", n == NOTE_MAX);

    /* the pool hands back a chunk emptied by the knobs; the step still returns */
    wait(2000);
    enc(PE_K3, -100);         /* the note: OFF, then nothing */
    enc(PE_K3, -100);
    wait(2000);
    press(PB_ENV);            /* INS */
    enc(PE_K3, -10);          /* the instrument: none */
    press(PB_SEQ);
    CHECK("the block's only step is empty", step_is_empty(st(0)));
    song_pool_compact(&app_song);
    undo();
    undo();
    CHECK("UNDO puts it back after the pool was compacted",
          st(0)->notes[0] == 60 && st(0)->instrument == 0);

    /* SONG: deleting a phrase moves every row below it */
    wait(2000);
    on = ON_SONG;
    songview_open(true);
    go();
    memcpy(before, app_song.arrangement, sizeof before);
    enc(PE_SELECT, 1);
    enc(PE_K2, -1);
    press(PB_LFO);
    press(PB_LFO);
    CHECK("LFO, twice, removed phrase 01",
          app_song.arrange_len == 2 && app_song.arrangement[1].pattern[0] == 0);
    undo();
    CHECK("UNDO brings the phrase back where it was",
          app_song.arrange_len == 3 && !memcmp(before, app_song.arrangement, 3 * sizeof(SongEntry)));
    press(PB_SEQ);
    CHECK("SEQ on the number added one", app_song.arrange_len == 4);
    undo();
    CHECK("and UNDO removes it", app_song.arrange_len == 3 && !undo_can());

    /* more gestures than the journal holds: the oldest are forgotten, the last are right */
    on = ON_PATTERN;
    pattern_open(&app_song, 0, 0);
    go();
    for (int r = 1; r < 64; r++) {
        enc(PE_K1, 1);
        for (int pass = 0; pass < 8; pass++) {
            wait(2000);
            enc(PE_K3, pass == 0 || pass % 2 ? 1 : -1); /* stays clear of the top note */
        }
    }
    CHECK("504 gestures written", st(63)->notes[0] != NOTE_NONE && undo_can());
    n = st(63)->notes[0];
    undo();
    CHECK("the last one is taken back exactly", st(63)->notes[0] == n - 1);
    for (n = 1; undo_can() && n < 1000; n++)
        undo();
    CHECK("the journal forgot the oldest and stopped there", n > 100 && n < 504 && st(1)->notes[0] != NOTE_NONE);

    printf("undo test: %d of 504 gestures kept -> %s\n", n, fails ? "FAIL" : "ok");
    return fails != 0;
}
