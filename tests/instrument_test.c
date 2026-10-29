/* SPDX-License-Identifier: GPL-3.0-only */
/* INSTRUMENT's hands, without a screen (rows of settings, KNOB 2 the
 * column, KNOB 3 the value), the name, the preview on PLAY, and the
 * audition they rely on: a key must make a sound outside the song, and stop. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/instrument.h"
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
    "version 3\nbpm 120\nticks 6\ntracks 1\n"
    "inst 03 engine PULSE timbre 1 adsr 8 60 170 40 gain -4 filter 168 40 72 name CHORDS\n"
    "inst 04 engine SMPL timbre 0 adsr 0 0 255 12 name KICK\n"
    "inst 05 engine VOX timbre 0 adsr 0 0 255 12 name ROBOT\n"
    "pat t0 00 len 16\n00 C-4 i03\narr 00 steps auto\nt0 00\n";

static PlatInput in;
static int fails;
static int last;
static void go(void) { last = instrument_frame(&in); memset(in.enc, 0, sizeof in.enc); in.ms += 16; }
static int press(int b) { int r; in.btns = 1u << b; go(); r = last; in.btns = 0; go(); return r; }
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

    /* rows: NAME, SOUND, ENV, FILTER, LEVEL, then the layers */
    enc(PE_SELECT, 2);
    enc(PE_K3, 2);
    enc(PE_K2, 1);
    enc(PE_K3, -10);
    enc(PE_K2, 1);
    enc(PE_K3, 100);
    enc(PE_K2, 1);
    enc(PE_K3, 5);
    CHECK("on ENV the columns are attack, decay, sustain, release",
          I3.attack == 10 && I3.decay == 50 && I3.sustain == 255 && I3.release == 45);
    enc(PE_SELECT, 1);
    enc(PE_K2, -3);
    enc(PE_K3, -8);
    enc(PE_K2, 2);
    enc(PE_K3, 100);
    CHECK("on FILTER: cutoff, and the env share stops at its top", I3.cutoff == 160 && I3.filt_env == 127);
    enc(PE_SELECT, 1);
    enc(PE_K2, 1);            /* LEVEL has three: GAIN LEVEL VIB */
    enc(PE_K3, -1);
    CHECK("under vibphase 0: FREE", I3.vib_phase == VIB_PHASE_FREE);
    enc(PE_K2, -2);
    enc(PE_K3, -100);
    CHECK("gain stops at -64 dB", I3.gain == GAIN_MIN_DB);
    enc(PE_K3, 64);

    enc(PE_SELECT, -3);       /* SOUND */
    enc(PE_K2, 1);
    enc(PE_K3, 9);
    CHECK("a pulse has four duties", I3.engine == ENGINE_PULSE && I3.timbre == 3);
    enc(PE_K2, -1);
    enc(PE_K3, 1);
    CHECK("changing the engine brings the timbre into its range", I3.engine == ENGINE_NOISE && I3.timbre == 1);
    enc(PE_K3, 1);
    CHECK("then WAVE", I3.engine == ENGINE_WAVE);
    enc(PE_K3, 1);
    CHECK("then the voice", I3.engine == ENGINE_VOX);
    enc(PE_SELECT, 4);
    CHECK("a voice has its VOICE row: tract and accent", I3.engine == ENGINE_VOX);
    enc(PE_K2, 1);
    enc(PE_K3, 1);
    CHECK("the accent goes to EN", I3.accent == VOX_ACCENT_EN);
    enc(PE_SELECT, -4);
    enc(PE_K2, -1);
    enc(PE_K3, 1);
    CHECK("then a drum kit", I3.engine == ENGINE_DRUM);
    enc(PE_K2, 1);
    enc(PE_K3, -20);
    enc(PE_K3, 4);
    CHECK("its timbre is the kit, STD to PERC", I3.timbre == 4);
    enc(PE_SELECT, 4);
    enc(PE_K2, -1);
    enc(PE_K3, 3);
    enc(PE_K2, 2);
    enc(PE_K3, -100);
    CHECK("a kit has its DRUM row: TUNE TONE DECAY SNAP, from -64 to 63",
          I3.drum[0] == 3 && I3.drum[1] == 0 && I3.drum[2] == -64 && I3.drum[3] == 0);
    enc(PE_K3, 64);
    enc(PE_SELECT, -4);
    enc(PE_K2, -1);
    enc(PE_K3, -2);
    CHECK("and back to WAVE", I3.engine == ENGINE_WAVE);

    /* the layers: + LYR is the row after LEVEL */
    enc(PE_SELECT, 9);
    enc(PE_K3, 2);
    CHECK("KNOB 3 up on + LYR makes a layer", I3.nlayers == 1 && I3.layers[0].engine == ENGINE_PULSE &&
          I3.layers[0].timbre == 1);
    enc(PE_K2, 1);
    enc(PE_K3, -12);
    enc(PE_K2, 1);
    enc(PE_K3, 7);
    enc(PE_K2, 1);
    enc(PE_K3, -6);
    CHECK("its columns are semitones, fine tune, gain",
          I3.layers[0].transpose == -12 && I3.layers[0].detune == 7 && I3.layers[0].gain == -6);
    enc(PE_SELECT, 9);
    enc(PE_K3, 9);
    CHECK("the next + LYR makes a second, one knob for engine and timbre",
          I3.nlayers == 2 && I3.layers[1].engine == ENGINE_WAVE && I3.layers[1].timbre == 2);
    enc(PE_SELECT, -1);
    enc(PE_K2, -3);
    enc(PE_K3, -20);
    CHECK("all the way down removes the layer, and the one under it moves up",
          I3.nlayers == 1 && I3.layers[0].engine == ENGINE_WAVE);

    /* the name */
    enc(PE_SELECT, -20);
    press(PB_SEL);
    enc(PE_K2, 6);
    enc(PE_K3, 2);            /* a B after CHORDS */
    press(PB_SEL);
    CHECK("SEL on NAME types it; seven letters at most", !strcmp(I3.name, "CHORDSB"));

    /* the keys, and the preview */
    I3.gain = 0;
    energy(256);
    CHECK("nothing sounds before a key", energy(2048) == 0);
    in.keys = 1u << 16;
    go();
    CHECK("a key plays the instrument, with the song stopped", energy(4096) > 100000 && !player_playing());
    in.keys = 0;
    go();
    for (int i = 0; i < 13; i++)
        energy(4096);
    CHECK("and lets go", energy(2048) == 0);
    press(PB_PLAY);
    CHECK("PLAY is a preview of the instrument, not the song", energy(4096) > 100000 && !player_playing());
    press(PB_PLAY);
    for (int i = 0; i < 13; i++)
        energy(4096);
    CHECK("and PLAY again stops it", energy(2048) == 0);

    /* an instrument this machine cannot sound */
    enc(PE_PRESET, 1);
    CHECK("PRESETS steps to the next instrument", !strcmp(app_song.instruments[4].name, "KICK"));
    in.keys = 1u << 16;
    go();
    CHECK("an instrument on SMPL is silent here", energy(4096) == 0);
    in.keys = 0;
    go();
    enc(PE_SELECT, -9);
    enc(PE_SELECT, 1);
    enc(PE_K2, -1);
    enc(PE_K3, 1);
    CHECK("and one turn of ENGINE gives it one of ours", app_song.instruments[4].engine == ENGINE_PULSE);

    CHECK("GLO asks for the PROJECT", press(PB_GLO) == INSTRUMENT_PROJECT);
    instrument_resume();
    go();
    CHECK("HOME leaves", press(PB_HOME) == INSTRUMENT_BACK);
    printf("instrument test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
