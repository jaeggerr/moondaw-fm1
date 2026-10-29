/* SPDX-License-Identifier: GPL-3.0-only */
/* MIXER's hands without a screen (the grid of 28/10: SELECT the track, KNOB 1 the
 * column or the number, KNOB 2 the value), and what MUTE and SOLO do to the sound. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/mixer.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }

Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;
void app_mark_dirty(void) { app_dirty = true; }
int app_save(void) { return 0; }
int app_not_carried(void) { return 0; }

/* track 1 hard left, track 2 as far right as a pan goes (63 of 64: a sixty-fourth of it
 * stays on the left): who sounds is read off the two channels */
static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 2\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10\n"
    "trk 0 vol 100 pan -64\ntrk 1 vol 100 pan 63\n"
    "pat t0 00 len 4\n00 A-4 i00\npat t1 00 len 4\n00 E-5 i00\n"
    "arr 00 steps auto\nt0 00\nt1 00\n";

static PlatInput in;
static int fails;
static void go(void) { mixer_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
static long left, right;
static void listen(void)
{
    static int16_t pcm[8192 * 2];
    player_render(pcm, 8192);
    left = right = 0;
    for (int i = 0; i < 8192; i++) {
        left += pcm[2 * i] < 0 ? -pcm[2 * i] : pcm[2 * i];
        right += pcm[2 * i + 1] < 0 ? -pcm[2 * i + 1] : pcm[2 * i + 1];
    }
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
#define T(i) (app_song.track_init[i])

int main(void)
{
    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    mixer_open("SONG", 0);
    go();                       /* the cursor starts on track 1, VOL */

    enc(PE_K2, -30);
    enc(PE_K1, 1);
    enc(PE_K2, 10);
    enc(PE_K1, 1);
    enc(PE_K2, 40);
    enc(PE_K1, 1);
    enc(PE_K2, 25);
    CHECK("on a track the columns are volume, pan, reverb, delay",
          T(0).volume == 70 && T(0).pan == -54 && T(0).send == 40 && T(0).echo == 25);
    enc(PE_K1, -2);
    enc(PE_K2, -100);
    CHECK("pan stops at hard left", T(0).pan == -64);
    enc(PE_K1, 1);
    enc(PE_K2, -100);
    enc(PE_K1, 1);
    enc(PE_K2, -100);

    enc(PE_SELECT, -1);         /* the returns' row, above track 1 */
    enc(PE_K1, -3);
    enc(PE_K2, 10);
    enc(PE_K1, 2);
    enc(PE_K2, 100);
    CHECK("on the returns: the room, and the delay stops at 96 ticks",
          app_song.reverb_size == 74 && app_song.delay_time == 96);
    enc(PE_K1, -5);
    enc(PE_K2, 1);
    CHECK("the returns' number is not a track: nothing to mute", !player_muted(0));

    player_play_song(0, false);
    listen();
    CHECK("both tracks sound, each on its side", left > 100000 && right > 100000);
    enc(PE_SELECT, 1);
    enc(PE_K2, 1);              /* track 1: MUTE */
    listen();
    listen();
    CHECK("KNOB 2 on a track's number mutes it", player_muted(0) && left * 32 < right && right > 100000);
    enc(PE_K2, -1);
    enc(PE_SELECT, 1);
    enc(PE_K2, 2);              /* track 2: past MUTE, SOLO */
    listen();
    listen();
    listen();
    CHECK("two notches: SOLO, the other silent without being muted",
          player_soloed(1) && !player_muted(1) && !player_muted(0) && left * 32 < right && right > 100000);
    enc(PE_K2, -2);
    for (int i = 0; i < 6; i++)
        listen();
    CHECK("and both are back when it is lifted", !player_soloed(1) && left > 100000 && right > 100000);

    in.btns = 1u << PB_EDIT;
    CHECK("EDIT goes to the PROJECT", mixer_frame(&in) == MIXER_PROJECT);
    in.btns = 0;
    go();
    in.btns = 1u << PB_GLO;
    CHECK("GLO leaves", mixer_frame(&in) == MIXER_BACK);
    printf("mixer test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
