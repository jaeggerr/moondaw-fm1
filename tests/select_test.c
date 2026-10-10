/* SPDX-License-Identifier: GPL-3.0-only */
/* SELECT on PATTERN: what the rectangle takes, where PASTE puts it, and
 * what it leaves alone. */
#include <stdio.h>
#include <string.h>

#include "../app/pattern.h"
#include "../app/undo.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_flash_read(uint32_t o, void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
int plat_flash_erase(uint32_t s) { (void)s; return -1; }
int plat_flash_prog(uint32_t o, const void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
int app_save(void) { return 0; }
void app_mark_dirty(void) {}

static Song song;
static int fails;
static PlatInput in;

static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 1\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10 name A\n"
    "pat t0 00 len 8\n00 C-4+E-4 i00 fGAT03\n01 D-4 fVOL40\n02 --- fREV20\n"
    "arr 00 steps auto\nt0 00\n";

static void go(void) { undo_frame(in.ms += 2000); pattern_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
static void key(int k) { in.keys = 1u << k; go(); in.keys = 0; go(); }
static const Step *st(int row) { return song_step(&song, 0, 0, row); }
static void at(int row, int col) { enc(PE_SELECT, -100); enc(PE_ALGO, -100); enc(PE_SELECT, row); enc(PE_ALGO, col); }
#define SELECT PB_EDIT
#define COPY   PB_SCL
#define PASTE  PB_ENV
#define CLEAR  PB_LFO
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

int main(void)
{
    Step first;
    int gat, vol;

    trk_load(&song, trk, sizeof trk - 1, NULL);
    player_init(&song);
    undo_reset(&song);
    pattern_open(&song, 0, 0);
    go();
    first = *st(0);
    gat = st(0)->fx[0].cmd;
    vol = st(1)->fx[0].cmd;

    at(4, 1);
    press(SELECT);
    press(PASTE);
    CHECK("PASTE with nothing copied does nothing", step_is_empty(st(4)));
    enc(PE_K1, 3);
    key(7);
    CHECK("in the mode the knobs and the keys write nothing", step_is_empty(st(4)));
    press(SELECT);
    enc(PE_K1, 1);
    CHECK("out of it they write again", st(4)->notes[0] != NOTE_NONE);
    press(CLEAR);
    CHECK("CLEAR out of the mode empties the slot under the cursor", step_is_empty(st(4)));

    /* N1 and N2 of steps 0 and 1 */
    at(0, 1);
    press(SELECT);
    enc(PE_ALGO, 1);
    enc(PE_SELECT, 1);
    press(COPY);
    press(SELECT);
    at(4, 1);
    press(SELECT);
    press(PASTE);
    CHECK("PASTE puts the notes from the cursor down",
          st(4)->notes[0] == 60 && st(4)->notes[1] == 64 && st(5)->notes[0] == 62);
    CHECK("and only the fields that were taken",
          st(4)->instrument == INSTR_NONE && st(4)->fx[0].cmd == FX_NONE && st(5)->fx[0].cmd == FX_NONE);
    enc(PE_K1, 1);
    CHECK("PASTE ends the mode: the knob writes", st(4)->notes[0] == 61);
    press(PB_ARP);
    press(PB_ARP);
    CHECK("one UNDO takes the whole paste back", step_is_empty(st(4)) && step_is_empty(st(5)));

    at(7, 1);
    press(SELECT);
    press(PASTE);
    CHECK("past the end of the block nothing wraps",
          st(7)->notes[0] == 60 && st(0)->notes[0] == 60 && st(0)->notes[1] == 64 && st(1)->notes[0] == 62);

    /* one fx, alone */
    at(0, 6);
    press(SELECT);
    press(COPY);
    press(SELECT);
    at(1, 8);
    press(SELECT);
    press(PASTE);
    CHECK("an fx copied alone lands in the slot under the cursor",
          st(1)->fx[2].cmd == gat && st(1)->fx[2].val == 3);
    CHECK("without touching the step's notes or its other fx",
          st(1)->notes[0] == 62 && st(1)->fx[0].cmd == vol);
    at(0, 7);
    press(SELECT);
    press(PASTE);
    CHECK("an fx the step already says is refused", st(0)->fx[1].cmd == FX_NONE);

    /* whole steps, by their number: COPY then CLEAR is a cut */
    at(0, 0);
    press(SELECT);
    enc(PE_SELECT, 2);
    press(COPY);
    press(CLEAR);
    CHECK("CLEAR empties everything the rectangle covers",
          step_is_empty(st(0)) && step_is_empty(st(1)) && step_is_empty(st(2)));
    at(3, 0);
    press(SELECT);
    press(PASTE);
    CHECK("a step taken by its number comes back whole",
          !memcmp(st(3), &first, sizeof first) && st(5)->fx[0].cmd != FX_NONE);

    printf("select test: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
