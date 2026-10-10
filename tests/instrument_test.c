/* SPDX-License-Identifier: GPL-3.0-only */
/* INSTRUMENT's hands, without a screen, and the audition they rely on:
 * a key must make a sound outside the song, and stop. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/instrument.h"
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
    "version 3\nbpm 120\nticks 6\ntracks 1\n"
    "inst 03 engine PULSE timbre 1 adsr 8 60 170 40 gain -4 filter 168 40 72 name CHORDS\n"
    "inst 04 engine SMPL timbre 0 adsr 0 0 255 12 name KICK\n"
    "pat t0 00 len 16\n00 C-4 i03\narr 00 steps auto\nt0 00\n";

static PlatInput in;
static int fails;
static void go(void) { instrument_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static long energy(int frames)
{
    static int16_t pcm[4096 * 2];
    long sum = 0;
    player_render(pcm, frames);
    for (int i = 0; i < frames; i++)
        sum += pcm[2 * i] < 0 ? -pcm[2 * i] : pcm[2 * i];
    return sum;
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
#define I3 (app_song.instruments[3])

int main(void)
{
    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    instrument_open(3, 0);
    go();

    /* it opens on the envelope */
    enc(PE_K1, 2);
    enc(PE_K2, -10);
    enc(PE_K3, 100);
    enc(PE_K4, 5);
    CHECK("on the envelope the four knobs are attack, decay, sustain, release",
          I3.attack == 10 && I3.decay == 50 && I3.sustain == 255 && I3.release == 45);
    enc(PE_SELECT, 1);
    enc(PE_K1, -8);
    enc(PE_K3, 100);
    CHECK("on the filter: cutoff, and the env share stops at its top", I3.cutoff == 160 && I3.filt_env == 127);
    enc(PE_SELECT, 1);
    enc(PE_K1, -1);
    CHECK("under vibphase 0: FREE", I3.vib_phase == VIB_PHASE_FREE);

    enc(PE_SELECT, -3);
    enc(PE_K2, 9);
    CHECK("a pulse has four duties", I3.engine == ENGINE_PULSE && I3.timbre == 3);
    enc(PE_K1, 1);
    CHECK("changing the engine brings the timbre into its range", I3.engine == ENGINE_NOISE && I3.timbre == 1);
    enc(PE_K1, 9);
    CHECK("three engines and no more", I3.engine == ENGINE_WAVE);
    enc(PE_K3, -100);
    CHECK("gain stops at -64 dB", I3.gain == GAIN_MIN_DB);
    enc(PE_K3, 64);

    /* the layers */
    enc(PE_SELECT, 9);
    enc(PE_K1, 2);
    CHECK("KNOB 1 on a free row makes a layer", I3.nlayers == 1 && I3.layers[0].engine == ENGINE_PULSE &&
          I3.layers[0].timbre == 1);
    enc(PE_K2, -12);
    enc(PE_K3, 7);
    enc(PE_K4, -6);
    CHECK("and the other three are transpose, detune, gain",
          I3.layers[0].transpose == -12 && I3.layers[0].detune == 7 && I3.layers[0].gain == -6);
    enc(PE_SELECT, 9);
    enc(PE_K1, 9);
    CHECK("the next free row makes a second, on one knob for engine and timbre",
          I3.nlayers == 2 && I3.layers[1].engine == ENGINE_WAVE && I3.layers[1].timbre == 2);
    enc(PE_SELECT, -1);
    enc(PE_K1, -20);
    CHECK("all the way down removes the layer, and the one under it moves up",
          I3.nlayers == 1 && I3.layers[0].engine == ENGINE_WAVE);

    /* the keys */
    I3.gain = 0;
    energy(256);
    CHECK("nothing sounds before a key", energy(2048) == 0);
    in.keys = 1u << 16;
    go();
    CHECK("a key plays the instrument, with the song stopped", energy(4096) > 100000 && !player_playing());
    in.keys = 0;
    go();
    energy(4096);
    for (int i = 0; i < 12; i++)
        energy(4096);
    CHECK("and lets go", energy(2048) == 0);

    /* an instrument this machine cannot sound */
    enc(PE_PRESET, 1);
    enc(PE_SELECT, -9);
    CHECK("PRESETS steps to the next instrument", !strcmp(app_song.instruments[4].name, "KICK"));
    in.keys = 1u << 16;
    go();
    CHECK("an instrument on SMPL is silent here", energy(4096) == 0);
    in.keys = 0;
    go();
    enc(PE_K1, 1);
    CHECK("and one turn of ENGINE gives it one of ours", app_song.instruments[4].engine == ENGINE_PULSE);

    in.btns = 1u << PB_HOME;
    CHECK("HOME leaves", !instrument_frame(&in));
    printf("instrument test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
