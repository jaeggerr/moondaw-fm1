/* SPDX-License-Identifier: GPL-3.0-only */
/* The SECTION's hands without a screen: where the cursor may go, what
 * the keys write, and into which step of which block. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/phrase.h"
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

/* Track 1 is four steps long in a phrase of eight; track 2 is left out
 * of the first phrase; track 3 carries a chord. */
static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 3\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10\n"
    "pat t0 00 len 4\n00 C-4 i00\n"
    "pat t1 00 len 8\n00 G-3 i00\n"
    "pat t2 00 len 8\n00 A-3+C-4+F-4 i00\n02 --- fREV20\n"
    "arr 00 steps auto\nt0 00\nt2 00\n"
    "arr 01 steps auto\nt0 00\nt1 00\nt2 00\n";

static PlatInput in;
static int fails, last;
static void go(void) { last = phrase_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static int press(int b) { int r; in.btns = 1u << b; go(); r = last; in.btns = 0; go(); return r; }
static void key(int k) { in.keys = 1u << k; go(); in.keys = 0; go(); }
static const Step *st(int t, int row) { return song_step(&app_song, t, 0, row); }
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

int main(void)
{
    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    phrase_open(0, 0);
    go();

    enc(PE_K2, 1);
    CHECK("KNOB 2 goes to the next track, even one the phrase leaves out", phrase_track() == 1);
    CHECK("SEL there opens nothing", press(PB_SEL) == PHRASE_STAY);
    enc(PE_K2, -5);
    CHECK("and stops at the first track", phrase_track() == 0);
    enc(PE_SELECT, 5);
    CHECK("a short block repeats: row 5 of the phrase is its step 1", phrase_row() == 1);
    key(7);
    CHECK("a key writes the first note, in the step that plays there", st(0, 1)->notes[0] == 60);
    in.keys = 1u << 7; go(); in.keys |= 1u << 11; go(); in.keys = 0; go();
    CHECK("a second key held with the first makes a chord", st(0, 1)->notes[0] == 60 && st(0, 1)->notes[1] == 64);
    press(PB_OCTUP);
    key(7);
    CHECK("OCT+ moves the keyboard an octave", st(0, 1)->notes[0] == 72);
    enc(PE_SELECT, 100);
    CHECK("SELECT stops at the phrase's last step", phrase_row() == 3);
    CHECK("SEL asks for PATTERN", press(PB_SEL) == PHRASE_PATTERN);
    phrase_resume();
    go();
    CHECK("FX for the mixer", press(PB_FX) == PHRASE_MIXER);
    phrase_resume();
    go();
    CHECK("EDIT, as SEL, asks for PATTERN", press(PB_EDIT) == PHRASE_PATTERN);
    phrase_resume();
    go();
    CHECK("GLO asks for the PROJECT", press(PB_GLO) == PHRASE_PROJECT);
    phrase_resume();
    go();
    CHECK("HOME leaves", press(PB_HOME) == PHRASE_BACK);

    printf("phrase test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
