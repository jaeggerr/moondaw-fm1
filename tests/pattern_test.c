/* SPDX-License-Identifier: GPL-3.0-only */
/* PATTERN's hands, without a screen: inputs go in as the panel would
 * send them and the song is read back. */
#include <stdio.h>
#include <string.h>

#include "../app/pattern.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_flash_read(uint32_t o, void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
int plat_flash_erase(uint32_t s) { (void)s; return -1; }
int plat_flash_prog(uint32_t o, const void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
static int saves, dirtied;
int app_save(void) { return saves++, 0; }
void app_mark_dirty(void) { dirtied++; }

static Song song;
static int fails;
static PlatInput in;

static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 2\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10 name A\n"
    "pat t0 00 len 16\n00 C-4 i00 fGAT03\n"
    "pat t0 01 len 8\n00 E-4\n"
    "arr 00 steps auto\nt0 00\n";

static void go(void) { pattern_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
static void keys(uint32_t held) { in.keys = held; go(); }
static const Step *st(int row) { return song_step(&song, 0, song.arrangement[0].pattern[0], row); }
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

int main(void)
{
    trk_load(&song, trk, sizeof trk - 1, NULL);
    player_init(&song);
    CHECK("opens the phrase's block", pattern_open(&song, 0, 0));
    CHECK("refuses a track the phrase leaves out", !pattern_open(&song, 0, 1));
    pattern_open(&song, 0, 0);
    go(); /* the frame after the way in: nothing held counts as a press */

    /* the keys: A4 is key 16 (F3 = 53) */
    enc(PE_SELECT, 2);
    keys(1u << 16);
    CHECK("a key writes its note under the cursor", st(2)->notes[0] == 69);
    keys(1u << 16 | 1u << 19);
    keys(1u << 16 | 1u << 19 | 1u << 23);
    CHECK("keys held together fill the next slots", st(2)->notes[1] == 72 && st(2)->notes[2] == 76);
    keys(0);
    press(PB_OCTUP);
    keys(1u << 16);
    CHECK("OCT+ moves the keyboard an octave", st(2)->notes[0] == 81);
    keys(0);
    press(PB_OCTDN);

    /* KNOB 1 and 2 on a note */
    enc(PE_K1, -2);
    CHECK("KNOB 1 is semitones", st(2)->notes[0] == 79);
    enc(PE_K2, -1);
    CHECK("KNOB 2 is octaves", st(2)->notes[0] == 67);
    enc(PE_SELECT, 1);
    enc(PE_K1, 1);
    CHECK("an empty slot starts from the note above", st(3)->notes[0] == 67);
    enc(PE_K1, -100);
    CHECK("all the way down empties the slot", st(3)->notes[0] == NOTE_NONE);
    enc(PE_K1, 1);
    enc(PE_K1, -(67 - NOTE_MIN) - 1);
    CHECK("under the lowest note of N1: OFF", st(3)->notes[0] == NOTE_OFF);
    enc(PE_ALGO, 1);
    enc(PE_K1, 1);
    CHECK("a note on a cut step replaces the cut", st(3)->notes[0] == NOTE_NONE && st(3)->notes[1] <= NOTE_MAX);

    /* instrument */
    enc(PE_ALGO, 3);
    enc(PE_K1, 3);
    CHECK("KNOB 1 on IN picks the instrument", st(3)->instrument == 2);
    enc(PE_K1, -10);
    CHECK("and under 00 there is none", st(3)->instrument == INSTR_NONE);

    /* fx: alphabetical from ---, ARP is the first */
    enc(PE_SELECT, -3);
    enc(PE_ALGO, 2); /* FX2 of step 0, whose FX1 is GAT03 */
    enc(PE_K1, 1);
    CHECK("the first effect after --- is ARP", st(0)->fx[1].cmd == FX_ARP);
    for (int i = 0; i < 40 && st(0)->fx[1].cmd != FX_PAN; i++)
        enc(PE_K1, 1);
    {
        bool saw_gat = false;
        enc(PE_K1, -40);
        for (int i = 0; i < 40; i++) {
            enc(PE_K1, 1);
            saw_gat |= st(0)->fx[1].cmd == FX_GAT;
        }
        CHECK("an effect already on the row is stepped over", !saw_gat);
        CHECK("the list ends on WAV", st(0)->fx[1].cmd == FX_WAV);
    }
    enc(PE_K1, -1);
    CHECK("before WAV: VOL", st(0)->fx[1].cmd == FX_VOL);
    enc(PE_K2, 100);
    enc(PE_K2, 100);
    CHECK("VOL stops at 7F", st(0)->fx[1].val == 0x7F);
    enc(PE_K1, -1);
    CHECK("before VOL: VIB", st(0)->fx[1].cmd == FX_VIB);
    enc(PE_K2, -100);
    enc(PE_K3, -100);
    enc(PE_K2, 5);
    enc(PE_K3, 3);
    CHECK("a pair is two knobs, one per nibble", st(0)->fx[1].val == 0x53);
    enc(PE_ALGO, -1);
    enc(PE_K2, 100);
    CHECK("GAT cannot outlast its step", st(0)->fx[0].val == song.ticks_per_step);

    /* the buttons */
    press(PB_LFO);
    CHECK("CLEAR empties the slot under the cursor", st(0)->fx[0].cmd == FX_NONE && st(0)->notes[0] == 60);
    enc(PE_ALGO, -9);
    enc(PE_K1, -4);
    CHECK("KNOB 1 on the step number is the length", song.patterns[0][0].length == 12);
    press(PB_LFO);
    CHECK("CLEAR on the step number empties the step", step_is_empty(st(0)));
    press(PB_FX);
    enc(PE_K1, 1);
    CHECK("FX goes to the first free fx slot", st(0)->fx[0].cmd == FX_ARP);

    /* PRESETS */
    enc(PE_PRESET, 1);
    CHECK("PRESETS steps to the track's next block, in the phrase",
          song.arrangement[0].pattern[0] == 1 && st(0)->notes[0] == 64);
    enc(PE_PRESET, 1);
    CHECK("and wraps", song.arrangement[0].pattern[0] == 0);

    press(PB_SAVE);
    CHECK("SAVE asks the app to save, once", saves == 1);
    CHECK("every edit told the app the song changed", dirtied > 20);

    in.btns = 1u << PB_ENV;
    CHECK("INSTR asks for the instrument of the step", pattern_frame(&in) == PATTERN_INSTR && pattern_instr() == 0);
    in.btns = 0;
    go();
    in.btns = 1u << PB_HOME;
    CHECK("HOME leaves", pattern_frame(&in) == PATTERN_BACK);
    printf("pattern test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
