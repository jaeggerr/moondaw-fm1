/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOP: the three presses, and that the player stays in the steps they name. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/loop.h"
#include "../app/phrase.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }

Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;
void app_mark_dirty(void) { app_dirty = true; }
int app_save(void) { return 0; }
int app_not_carried(void) { return 0; }

static const char trk[] =
    "version 3\nbpm 240\nticks 3\ntracks 1\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10\n"
    "pat t0 00 len 16\n00 C-4 i00\n"
    "arr 00 steps auto\nt0 00\n"
    "arr 01 steps auto\nt0 00\n";

static PlatInput in;
static int fails;
static void go(void) { phrase_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
/* Four seconds of the player, and which of the sixteen steps it was seen on. */
static unsigned listen(void)
{
    static int16_t pcm[256 * 2];
    unsigned seen = 0;
    for (int i = 0; i < 700; i++) {
        player_render(pcm, 256);
        if (player_playing())
            seen |= 1u << (player_row() & 15);
    }
    return seen;
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

int main(void)
{
    unsigned seen;

    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    phrase_open(0, 0);
    go();

    press(PB_PLAY);
    seen = listen();
    CHECK("without a loop the phrase plays whole", seen == 0xFFFFu);

    enc(PE_SELECT, 9);
    press(PB_SCL);
    CHECK("the first press sets one end", loop_state() == LOOP_ONE_END && loop_from() == 9);
    seen = listen();
    CHECK("one end alone changes nothing to the sound", seen == 0xFFFFu);
    enc(PE_SELECT, -4);
    press(PB_SCL);
    CHECK("the second, set above the first, trades places with it",
          loop_state() == LOOP_SET && loop_from() == 5 && loop_to() == 9);
    listen(); /* whatever was playing when it was set */
    seen = listen();
    CHECK("set while playing, the player stays in steps 5 to 9", seen == 0x03E0u);
    press(PB_PLAY);
    listen();
    press(PB_PLAY);
    seen = listen();
    CHECK("PLAY with a loop starts in it", seen == 0x03E0u);

    press(PB_SCL);
    CHECK("the third press forgets it", loop_state() == LOOP_NONE);
    seen = listen();
    CHECK("and the phrase plays whole again", seen == 0xFFFFu);

    enc(PE_SELECT, -100);
    press(PB_SCL);
    CHECK("on a column head LOOP sets nothing", loop_state() == LOOP_NONE);
    enc(PE_SELECT, 3);
    press(PB_SCL);
    enc(PE_SELECT, 2);
    press(PB_SCL);
    CHECK("a loop of steps 2 to 4", loop_state() == LOOP_SET && loop_from() == 2 && loop_to() == 4);
    enc(PE_PRESET, 1);
    CHECK("another phrase: the loop was the first one's and is gone", loop_state() == LOOP_NONE);
    press(PB_PLAY);
    listen(); /* the stop is taken up by the audio side */
    press(PB_PLAY);
    seen = listen();
    CHECK("and the player no longer keeps to it", seen == 0xFFFFu);

    printf("loop test: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
