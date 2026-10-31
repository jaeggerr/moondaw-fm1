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
#include "ramflash.h"

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
    "smp 00 tr909/tr9_kick.wav\n"
    "inst 03 engine PULSE timbre 1 adsr 8 60 170 40 gain -4 filter 168 40 72 name CHORDS\n"
    "inst 04 engine SMPL timbre 0 adsr 0 0 255 12 name KICK\n"
    "zone 04 C-0 G-9 root C-4 smp 00\n"
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
    /* The knob's default moved to English on 30/10, the file's meaning did
     * not: a `.trk` that never wrote an `accent` line still means French,
     * as every song written until then was written. ROBOT is such a voice. */
    CHECK("a voice read from a file with no accent line is French",
          app_song.instruments[5].engine == ENGINE_VOX &&
              app_song.instruments[5].accent == VOX_ACCENT_FR);
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
    CHECK("then SMPL, on the knob since 30/10", I3.engine == ENGINE_SAMPLE);
    enc(PE_K3, -1);
    CHECK("a turn back from SMPL is WAVE, not the first of the list", I3.engine == ENGINE_WAVE);
    enc(PE_K3, 2);
    CHECK("then the voice", I3.engine == ENGINE_VOX);
    CHECK("a voice made on the ENGINE knob sings in English (30/10)", I3.accent == VOX_ACCENT_EN);
    enc(PE_SELECT, 4);
    CHECK("a voice has its VOICE row: tract and accent", I3.engine == ENGINE_VOX);
    enc(PE_K2, 1);
    enc(PE_K3, -1);
    CHECK("the ACCENT knob still turns it to FR", I3.accent == VOX_ACCENT_FR);
    enc(PE_SELECT, -4);
    enc(PE_K2, -1);
    enc(PE_K3, -1);
    CHECK("a turn away from the voice is SMPL", I3.engine == ENGINE_SAMPLE);
    enc(PE_K3, 1);
    CHECK("and a turn back is the voice again", I3.engine == ENGINE_VOX);
    CHECK("away from VOX and back keeps the accent he chose", I3.accent == VOX_ACCENT_FR);
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
    enc(PE_K3, -3);
    CHECK("and back to WAVE", I3.engine == ENGINE_WAVE);

    /* the layers: + LAY is the row after LEVEL */
    enc(PE_SELECT, 9);
    enc(PE_K3, 2);
    CHECK("KNOB 3 up on + LAY makes a layer", I3.nlayers == 1 && I3.layers[0].engine == ENGINE_PULSE &&
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
    CHECK("the next + LAY makes a second, one knob for engine and timbre",
          I3.nlayers == 2 && I3.layers[1].engine == ENGINE_WAVE && I3.layers[1].timbre == 2);
    enc(PE_SELECT, -1);
    enc(PE_K2, -3);
    enc(PE_K3, -20);
    CHECK("all the way down removes the layer, and the one under it moves up",
          I3.nlayers == 1 && I3.layers[0].engine == ENGINE_WAVE);

    /* the name */
    enc(PE_SELECT, -20);
    press(PB_SEL);
    enc(PE_K1, 6);            /* KNOB 1 the letter, KNOB 2 changes it (30/10) */
    enc(PE_K2, 2);            /* a B after CHORDS */
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

    /* an instrument on SMPL: no bank is mounted here, so it has nothing to
     * play -- but the knob no longer throws the engine away (30/10) */
    enc(PE_PRESET, 1);
    CHECK("PRESETS steps to the next instrument", !strcmp(app_song.instruments[4].name, "KICK"));
    in.keys = 1u << 16;
    go();
    CHECK("an instrument on SMPL with no bank is silent", energy(4096) == 0);
    in.keys = 0;
    go();
    enc(PE_SELECT, -9);
    enc(PE_SELECT, 1);
    enc(PE_K2, -1);
    CHECK("and it is still on SMPL, sitting on the engine cell", app_song.instruments[4].engine == ENGINE_SAMPLE);
    enc(PE_K3, 1);
    CHECK("one turn of ENGINE steps it along the list, not back to the first",
          app_song.instruments[4].engine == ENGINE_VOX);
    enc(PE_K3, -1);
    CHECK("and a turn back returns it to SMPL", app_song.instruments[4].engine == ENGINE_SAMPLE);
    enc(PE_K2, 1);
    enc(PE_K3, 1);
    CHECK("SMPL has no timbre: there is no column to move to, and the knob still turns the engine",
          app_song.instruments[4].engine == ENGINE_VOX);
    enc(PE_K3, -1);

    /* the SAMPLE row (30/10): one zone over the whole keyboard, edited here */
    enc(PE_SELECT, 4);
    enc(PE_K3, 1);
    CHECK("with no bank mounted there is no sample to choose, and nothing moves",
          song_zone(&app_song, 4, 60) && song_zone(&app_song, 4, 60)->smp == 0);
    enc(PE_K2, 1);
    enc(PE_K3, 2);
    enc(PE_K2, 1);
    enc(PE_K3, -5);
    enc(PE_K2, 1);
    enc(PE_K3, 3);
    {
        const Zone *z = song_zone(&app_song, 4, 60);
        CHECK("ROOT, TUNE and GAIN write the instrument's own zone",
              z && z->root == 62 && z->tune == -5 && z->gain == 3);
        CHECK("and it still covers the whole keyboard", z && z->lo == NOTE_MIN && z->hi == NOTE_MAX);
        CHECK("the song has the one zone, not a second", app_song.nzones == 1);
    }

    /* ROBOT, the voice the file carried with no `accent` line: French, and
     * the screen must not turn it English behind his back. Only a voice
     * MADE on the knob starts in English (30/10). */
    instrument_open(5, 0);
    go();
    CHECK("the file's own voice opens in French", app_song.instruments[5].accent == VOX_ACCENT_FR);
    enc(PE_SELECT, 1);
    enc(PE_K3, -1);
    CHECK("one turn down from the voice is SMPL", app_song.instruments[5].engine == ENGINE_SAMPLE);
    enc(PE_K3, 1);
    CHECK("and back to the voice", app_song.instruments[5].engine == ENGINE_VOX);
    CHECK("a voice from a file turned away and back is French still",
          app_song.instruments[5].accent == VOX_ACCENT_FR);

    instrument_open(3, 0);
    go();
    CHECK("GLO asks for the PROJECT", press(PB_GLO) == INSTRUMENT_PROJECT);
    instrument_resume();
    go();
    CHECK("HOME leaves", press(PB_HOME) == INSTRUMENT_BACK);
    /* The factory presets (31/10). Their whole point is that a track
     * taken fresh sounds like an instrument, so the test is the ear's
     * test and not the struct's: every slot song_init seeds is played
     * and its energy counted. A preset that says BASS and makes no
     * sound is worse than no preset at all. The first slot past them
     * must still be the neutral ground song_instr_init lays. */
    song_init(&app_song);
    player_init(&app_song);
    player_set_master(1023);
    {
        int n = song_preset_count();
        uint8_t nn_scratch[STEP_MAX_NOTES];

        CHECK("song_init seeds slots, and leaves some free", n > 0 && n < SONG_MAX_INSTR);
        for (int i = 0; i < n; i++) {
            const InstrumentDef *in = &app_song.instruments[i];
            /* A kit answers to the drums it carries, not to middle C. */
            uint8_t note = in->engine == ENGINE_DRUM ? 36 : 60;
            long e = 0;
            char what[64];

            snprintf(what, sizeof what, "preset %d carries a name", i);
            CHECK(what, in->name[0] != '\0');
            snprintf(what, sizeof what, "preset %d (%s) is not the silent engine", i, in->name);
            CHECK(what, in->engine != ENGINE_PLUCK);

            player_note_on(i, note);
            for (int k = 0; k < 3; k++)
                e += energy(4096);
            player_note_off();
            energy(4096);
            snprintf(what, sizeof what, "preset %d (%s) makes a sound", i, in->name);
            CHECK(what, e > 0);

            /* Its demo phrase (31/10): step_ms divides, so a zero there
             * would divide by zero on the screen rather than sound wrong,
             * and a note out of range would be refused by the player
             * instead of playing. A kit's notes are drums on a map, so
             * only the melodic ones are held to NOTE_MIN. */
            {
                const Demo *d = song_instr_demo(&app_song, i);

                snprintf(what, sizeof what, "preset %d (%s) has a phrase of its own", i, in->name);
                CHECK(what, d != song_instr_demo(&app_song, SONG_MAX_INSTR - 1));
                snprintf(what, sizeof what, "preset %d (%s): its phrase is playable", i, in->name);
                CHECK(what, d->step_ms > 0 && d->n > 0 && d->n <= SONG_DEMO_MAX);
                /* A chord the track cannot hold, or a phrase whose notes
                 * do not divide into chords, would silently drop the
                 * slots left over. */
                snprintf(what, sizeof what, "preset %d (%s): its chords come out whole", i, in->name);
                CHECK(what, d->chord <= 1 ||
                            (d->chord <= STEP_MAX_NOTES && d->n % d->chord == 0));
                CHECK("a phrase always has a strike to play", song_demo_steps(d) >= 1);
                for (int k = 0; k < d->n; k++) {
                    snprintf(what, sizeof what, "preset %d (%s) note %d is a note or a rest",
                             i, in->name, k);
                    CHECK(what, d->note[k] == 0 ||
                                (d->note[k] >= NOTE_MIN && d->note[k] <= NOTE_MAX));
                }
                for (int k = 0; k < song_demo_steps(d); k++) {
                    uint8_t nn[STEP_MAX_NOTES];
                    int got = song_demo_notes(d, k, nn);

                    snprintf(what, sizeof what, "preset %d (%s) strike %d fits a step",
                             i, in->name, k);
                    CHECK(what, got >= 0 && got <= STEP_MAX_NOTES);
                }
                CHECK("past the end a phrase strikes nothing",
                      song_demo_notes(d, song_demo_steps(d), nn_scratch) == 0);
            }
        }

        /* The phrase follows the preset, not the slot: rename slot 0 and
         * it stops claiming to show BASS off. */
        app_song.instruments[0].name[0] = 'Z';
        CHECK("a renamed slot falls back to the plain phrase",
              song_instr_demo(&app_song, 0) == song_instr_demo(&app_song, SONG_MAX_INSTR - 1));
        app_song.instruments[0].name[0] = 'B';
        CHECK("and named back, it is BASS's phrase again",
              song_instr_demo(&app_song, 0) != song_instr_demo(&app_song, SONG_MAX_INSTR - 1));
        {
            /* A free slot turned into a kit plays drums, though no preset
             * speaks for it. */
            const Demo *plain = song_instr_demo(&app_song, SONG_MAX_INSTR - 1);
            app_song.instruments[SONG_MAX_INSTR - 1].engine = ENGINE_DRUM;
            CHECK("any kit plays a kit's phrase",
                  song_instr_demo(&app_song, SONG_MAX_INSTR - 1) != plain);
        }
        CHECK("the slot past the presets is the neutral ground still",
              app_song.instruments[n].name[0] == '\0' &&
              app_song.instruments[n].engine == ENGINE_PULSE &&
              app_song.instruments[n].sustain == 255);
    }

    printf("instrument test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
