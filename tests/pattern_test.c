/* SPDX-License-Identifier: GPL-3.0-only */
/* PATTERN's hands, without a screen: inputs go in as the panel would
 * send them (three views, SEQ FX ENV, KNOB 2 the column, KNOB 3
 * the value) and the song is read back. */
#include <stdio.h>
#include <string.h>

#include "../app/pattern.h"
#include "../app/loop.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
void plat_led_level(int l, int v) { (void)l; (void)v; }
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_flash_read(uint32_t o, void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
const uint8_t *plat_flash_map(void) { return 0; }
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
static int pat_len_of(void) { return song.patterns[0][song.arrangement[0].pattern[0]].length; }
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

    /* NOTES */
    enc(PE_K1, 2);
    keys(1u << 16); keys(0);
    CHECK("a key writes its note under the cursor", st(2)->notes[0] == 69);
    keys(1u << 16); keys(1u << 16 | 1u << 19); keys(1u << 16 | 1u << 19 | 1u << 23); keys(0);
    CHECK("keys held together fill the next slots", st(2)->notes[1] == 72 && st(2)->notes[2] == 76);
    press(PB_OCTUP);
    keys(1u << 16); keys(0);
    CHECK("OCT+ moves the keyboard an octave", st(2)->notes[0] == 81);
    press(PB_OCTDN);
    enc(PE_K3, -2);
    CHECK("KNOB 3 on a note is semitones", st(2)->notes[0] == 79);
    enc(PE_K2, 3);
    keys(1u << 0); keys(0);
    CHECK("KNOB 2 picks the note: a key writes into slot 4", st(2)->notes[3] == 53);
    enc(PE_K2, -3);
    enc(PE_K1, 1);
    enc(PE_K3, 1);
    CHECK("an empty slot starts from the note above", st(3)->notes[0] == 79);
    enc(PE_K3, -100); /* a frame holds an int8_t of detents */
    enc(PE_K3, -100);
    CHECK("all the way down: OFF, then empty", st(3)->notes[0] == NOTE_NONE);

    /* FX */
    enc(PE_K1, -3);
    press(PB_FX);
    CHECK("the effect GAT is in place", st(0)->fx[0].cmd == FX_GAT);
    enc(PE_K2, 1);
    enc(PE_K4, 1);
    CHECK("KNOB 4 on an effect: the first after --- is ARP", st(0)->fx[1].cmd == FX_ARP);
    enc(PE_K4, -1);
    {
        bool saw_gat = false;
        for (int i = 0; i < 40; i++) {
            enc(PE_K4, 1);
            saw_gat |= st(0)->fx[1].cmd == FX_GAT;
        }
        CHECK("an effect already on the row is stepped over", !saw_gat);
        CHECK("the list ends on WAV", st(0)->fx[1].cmd == FX_WAV);
    }
    enc(PE_K4, -1);
    CHECK("before WAV: VOL", st(0)->fx[1].cmd == FX_VOL);
    enc(PE_K3, 100);
    enc(PE_K3, 100);
    CHECK("KNOB 3 is the value: VOL stops at 7F", st(0)->fx[1].val == 0x7F);
    enc(PE_K2, -1);
    enc(PE_K3, 50);
    CHECK("GAT cannot outlast its step", st(0)->fx[0].val == song.ticks_per_step);

    /* SEL on an effect: the step's SYLLABLE (KEYS: G5 is M, F#3 is A) */
    press(PB_SEL);            /* on GAT: another effect is left alone */
    keys(1u << 26);
    keys(0);
    CHECK("SEL on GAT opens nothing, and in FX G5 only plays", st(0)->notes[0] != 79);
    enc(PE_K2, 2);            /* the third slot, empty (the second holds VOL) */
    press(PB_SEL);
    keys(1u << 26);
    keys(0);
    keys(1u << 1);
    keys(0);
    keys(1u << 4);            /* T, then ALGO takes it back */
    keys(0);
    enc(PE_ALGO, -1);
    enc(PE_ALGO, -1);
    enc(PE_ALGO, 1);
    press(PB_SEL);
    CHECK("the keys type the syllable, SEL writes it in the empty slot",
          st(0)->fx[2].cmd == FX_SYL && !strcmp(song_syl(&song, st(0)->fx[2].val), "MA") && st(0)->notes[0] != 79);
    enc(PE_K2, -1);
    press(PB_SEL);            /* from VOL's slot: the row's syllable, where it is */
    enc(PE_PRESET, 1);        /* WHEEL */
    enc(PE_K2, 2);
    enc(PE_K3, 14);           /* - then A..N */
    press(PB_SEL);
    CHECK("the wheel adds a sign to the same syllable",
          st(0)->fx[1].cmd == FX_VOL && !strcmp(song_syl(&song, st(0)->fx[2].val), "MAN"));
    press(PB_SEL);
    press(PB_LFO);            /* the wheel's slot emptied */
    press(PB_HOME);
    CHECK("HOME leaves it as it was", !strcmp(song_syl(&song, st(0)->fx[2].val), "MAN"));
    press(PB_SEL);
    enc(PE_PRESET, 1);        /* KEYS: LFO rubs the last out */
    press(PB_LFO);
    press(PB_LFO);
    press(PB_LFO);
    press(PB_SEL);
    CHECK("an empty syllable empties the slot", st(0)->fx[2].cmd == FX_NONE);
    enc(PE_K2, -1);

    /* INS */
    press(PB_ENV);
    enc(PE_K1, 2);
    enc(PE_K3, 3);
    CHECK("KNOB 3 on INS picks the instrument", st(2)->instrument == 2);
    enc(PE_K3, -10);
    CHECK("and under 00 there is none", st(2)->instrument == INSTR_NONE);
    press(PB_SEL);            /* CHOOSE INSTRUMENT, on -- */
    enc(PE_K1, 2);
    press(PB_HOME);
    CHECK("HOME leaves the list and changes nothing", st(2)->instrument == INSTR_NONE);
    press(PB_SEL);
    enc(PE_K1, 2);
    press(PB_SEL);
    CHECK("SEL takes the instrument from the list", st(2)->instrument == 1);
    press(PB_SEL);
    enc(PE_K1, -2);
    press(PB_SEL);
    CHECK("and -- names none again", st(2)->instrument == INSTR_NONE);
    press(PB_SEQ);

    /* SELECTION: EDIT in, SELECT stretches, the black keys OP1-4 */
    press(PB_SEQ);            /* NOTES, the cursor on row 2 */
    enc(PE_K2, -10);
    enc(PE_K2, 1);            /* the first note */
    enc(PE_K1, -2);
    {
        uint8_t n0 = st(0)->notes[0], n1 = st(1)->notes[0];
        press(PB_EDIT);
        enc(PE_K1, 1);
        keys(1u << 1);        /* OP1: copy rows 0-1, note 1 */
        keys(0);
        enc(PE_K1, 4);    /* the cursor is free: row 5 */
        keys(1u << 3);        /* OP2: paste */
        keys(0);
        CHECK("OP1 copies, OP2 pastes at the cursor", st(5)->notes[0] == n0 && st(6)->notes[0] == n1);
        press(PB_EDIT);
        press(PB_EDIT);       /* a new anchor at row 5 */
        enc(PE_K1, 1);
        keys(1u << 8);        /* OP4: clear */
        keys(0);
        CHECK("OP4 clears the selection", st(5)->notes[0] == NOTE_NONE && st(6)->notes[0] == NOTE_NONE);
        CHECK("and no key wrote a note meanwhile", st(0)->notes[0] == n0);
        press(PB_EDIT);       /* out, the cursor on row 6 */
        enc(PE_K1, -1);
        press(PB_EDIT);       /* a selection of rows 5-6 */
        enc(PE_K1, 1);
        keys(1u << 13);       /* OP6: the loop */
        keys(0);
        CHECK("OP6 makes the selection the loop", loop_state() == LOOP_SET && loop_from() == 5 && loop_to() == 6);
        press(PB_EDIT);
        enc(PE_K1, -1);
        press(PB_EDIT);
        enc(PE_K1, 1);
        keys(1u << 13);       /* OP6 on the same steps: no loop */
        keys(0);
        CHECK("and OP6 on the same steps forgets it", loop_state() == LOOP_NONE);
        press(PB_EDIT);
        press(PB_EDIT);
        enc(PE_K2, -1);       /* whole steps */
        keys(1u << 5);        /* OP3: cut row 6 */
        keys(0);
        CHECK("OP3 cuts whole steps", step_is_empty(st(6)));
        press(PB_EDIT);
        CHECK("EDIT again: out, on the step's number the keys only play", (keys(1u << 1), keys(0), step_is_empty(st(6))));
        enc(PE_K2, 1);
        CHECK("and in a note column they write", (keys(1u << 1), keys(0), st(6)->notes[0] != NOTE_NONE));
        enc(PE_K2, -1);
        enc(PE_K1, -6);
    }

    /* EDIT held: the keys are commands while it is down -- MONO, the track alone */
    in.btns = 1u << PB_EDIT;
    go();
    keys(1u << 20);
    keys(0);
    in.btns = 0;
    go();
    CHECK("EDIT + MONO: the track alone, and no SELECTION opened", player_soloed(0));
    in.btns = 1u << PB_EDIT;
    go();
    keys(1u << 20);
    keys(0);
    in.btns = 0;
    go();
    CHECK("again: among the others", !player_soloed(0));
    in.btns = 1u << PB_EDIT;  /* MONO held: muted */
    go();
    in.keys = 1u << 20;
    for (int i = 0; i < 31; i++)
        go();
    in.keys = 0;
    go();
    in.btns = 0;
    go();
    CHECK("EDIT + MONO held: the track muted", player_muted(0) && !player_soloed(0));
    in.btns = 1u << PB_EDIT;
    go();
    keys(1u << 20);
    keys(0);
    in.btns = 0;
    go();
    CHECK("and a tap: back to normal", !player_muted(0) && !player_soloed(0));
    enc(PE_SELECT, 1);
    CHECK("SELECT puts the cursor on the step's number: the keys only play",
          (keys(1u << 1), keys(0), st(1)->notes[0] == NOTE_NONE || st(1)->notes[0] != 54));
    enc(PE_SELECT, -1);

    /* REC: armed, a bar counted in, the keys written where the playhead is */
    {
        static int16_t audio[4096 * 2];
        int found = -1, off = -1;
        press(PB_SEQ);
        press(PB_REC);
        in.ms = 1000;
        press(PB_PLAY);       /* the count starts */
        for (int i = 0; i < 6; i++) {
            in.ms += 600;     /* past a beat at 120 bpm each time */
            go();
        }
        player_render(audio, 4096);
        go();
        keys(1u << 10);       /* D#4 */
        player_render(audio, 4096);
        keys(0);              /* let go: an OFF */
        for (int r = 0; r < pat_len_of(); r++) {
            if (st(r)->notes[0] == 63)
                found = r;
            if (st(r)->notes[0] == NOTE_OFF)
                off = r;
        }
        CHECK("REC writes the key where the playhead is", found >= 0);
        CHECK("and an OFF where it was let go, after it", off >= 0 && off != found);
        press(PB_PLAY);       /* stop */
        press(PB_REC);
        for (int i = 0; i < 4; i++)
            enc(PE_ALGO, -1); /* the recording taken back */
    }

    /* the step's number */
    enc(PE_K2, -1);
    enc(PE_K3, -4);
    CHECK("KNOB 3 on the step's number is the block's length", song.patterns[0][0].length == 12);

    press(PB_SAVE);
    CHECK("SAVE asks the app to save, once", saves == 1);
    CHECK("every edit told the app the song changed", dirtied > 10);
    press(PB_ENV);            /* INS; ENV again goes into the instrument */
    in.btns = 1u << PB_ENV;
    CHECK("ENV on INS asks for the instrument of the step", pattern_frame(&in) == PATTERN_INSTR && pattern_instr() == 0);
    in.btns = 0;
    pattern_resume();
    go();
    in.btns = 1u << PB_GLO;
    CHECK("GLO asks for the PROJECT", pattern_frame(&in) == PATTERN_PROJECT);
    in.btns = 0;
    pattern_resume();
    go();
    in.btns = 1u << PB_HOME;
    CHECK("HOME leaves", pattern_frame(&in) == PATTERN_BACK);

    printf("pattern test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
