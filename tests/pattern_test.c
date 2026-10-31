/* SPDX-License-Identifier: GPL-3.0-only */
/* PATTERN's hands, without a screen: inputs go in as the panel would
 * send them (three views, SEQ FX ENV, KNOB 2 the column, KNOB 3
 * the value) and the song is read back. */
#include <stdio.h>
#include <string.h>

#include "../app/pattern.h"
#include "../app/loop.h"
#include "../app/undo.h"
#include "../app/ui.h"
#include "../core/drum.h"
#include "../core/player.h"
#include "../core/trk.h"

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
static int led[PLAT_BTNS + PLAT_KEYS]; /* the guide, as the panel would have it: buttons then keys */
void plat_led_level(int l, int v) { if (l >= 0 && l < PLAT_BTNS + PLAT_KEYS) led[l] = v; }
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
/* the player takes PLAY and STOP in its render, as on the machine */
static void pump(void)
{
    static int16_t pcm[256 * 2];
    player_render(pcm, 256);
}
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
    enc(PE_K3, 1);
    CHECK("KNOB 3 on an effect: the first after --- is ARP", st(0)->fx[1].cmd == FX_ARP);
    enc(PE_K3, -1);
    {
        bool saw_gat = false;
        for (int i = 0; i < 40; i++) {
            enc(PE_K3, 1);
            saw_gat |= st(0)->fx[1].cmd == FX_GAT;
        }
        CHECK("an effect already on the row is stepped over", !saw_gat);
        CHECK("the list ends on WAV", st(0)->fx[1].cmd == FX_WAV);
    }
    { /* PRB, the one effect of this firmware's own */
        bool found = false;
        for (int i = 0; i < 40 && !found; i++) {
            enc(PE_K3, -1);
            found = st(0)->fx[1].cmd == FX_PRB;
        }
        CHECK("PRB is in KNOB 3's list", found);
        enc(PE_K4, 100); /* the knob is an int8_t: 7F in two turns */
        enc(PE_K4, 100);
        CHECK("PRB stops at 7F", st(0)->fx[1].val == 0x7F);
        enc(PE_K3, 40); /* back to the end of the list */
        CHECK("SYL and SMP are not in the list", st(0)->fx[1].cmd == FX_WAV);
    }
    enc(PE_K3, -1);
    CHECK("before WAV: VOL", st(0)->fx[1].cmd == FX_VOL);
    enc(PE_K4, 100);
    enc(PE_K4, 100);
    CHECK("KNOB 4 is the value: VOL stops at 7F", st(0)->fx[1].val == 0x7F);
    enc(PE_K2, -1);
    enc(PE_K4, 50);
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
    /* The ear on the column (30/10): the hand rests and the step sounds with
     * the instrument it has just been given. Nothing else is playing here,
     * so what the player renders is the audition and silence says there was
     * none. */
    {
        static int16_t audio[1024 * 2];
        bool quiet = true, heard = false;
        player_note_off();
        for (int i = 0; i < 40; i++)
            player_render(audio, 1024);
        for (int i = 0; i < 1024 * 2; i++)
            quiet &= audio[i] == 0;
        CHECK("nothing sounds before the knob is turned", quiet);
        enc(PE_K3, 1);        /* instrument 00 on the step */
        for (int i = 0; i < 20; i++)
            go();             /* the hand rests: past SETTLE */
        player_render(audio, 1024);
        for (int i = 0; i < 1024 * 2; i++)
            heard |= audio[i] != 0;
        CHECK("KNOB 3 on INS is heard on the step", heard);
        CHECK("and the knob still writes the instrument", st(2)->instrument == 0);
        for (int i = 0; i < 80; i++) /* it rings for a while and stops by itself */
            go(), player_render(audio, 1024);
        quiet = true;
        for (int i = 0; i < 1024 * 2; i++)
            quiet &= audio[i] == 0;
        CHECK("and nothing is left hanging", quiet);
        enc(PE_K3, -10);
        CHECK("under 00 there is none again", st(2)->instrument == INSTR_NONE);
    }
    { /* a step holding no note is still heard in its place, on the nearest
       * note above or below it in the column */
        static int16_t audio[1024 * 2];
        bool heard = false;
        enc(PE_K1, 9);
        CHECK("the step further down holds no note", step_is_empty(st(11)));
        player_note_off();
        for (int i = 0; i < 40; i++)
            player_render(audio, 1024);
        enc(PE_K3, 1);
        for (int i = 0; i < 20; i++)
            go();
        player_render(audio, 1024);
        for (int i = 0; i < 1024 * 2; i++)
            heard |= audio[i] != 0;
        CHECK("and the nearest note in the column is what sounds", heard);
        enc(PE_K3, -10);      /* the step names none again */
        enc(PE_K1, -9);
        for (int i = 0; i < 80; i++)
            go(), player_render(audio, 1024);
    }
    /* CHOOSE INSTRUMENT is the PRESETS knob's now (30/10) */
    enc(PE_PRESET, 1);        /* the list, on -- */
    enc(PE_K1, 2);
    press(PB_HOME);
    CHECK("HOME leaves the list and changes nothing", st(2)->instrument == INSTR_NONE);
    enc(PE_PRESET, 1);
    enc(PE_K1, 2);
    press(PB_SEL);
    CHECK("SEL takes the instrument from the list", st(2)->instrument == 1);
    enc(PE_PRESET, -1);       /* the knob either way opens it */
    enc(PE_K1, -2);
    press(PB_SEL);
    CHECK("and -- names none again", st(2)->instrument == INSTR_NONE);
    { /* PLAY in the list is the instrument's own preview phrase, not the
       * step's note: it sounds on a step that names none and holds none */
        static int16_t audio[1024 * 2];
        bool heard = false, quiet = true;
        player_note_off();
        for (int i = 0; i < 40; i++)
            player_render(audio, 1024);
        enc(PE_PRESET, 1);
        enc(PE_K1, 1);        /* instrument 00 */
        press(PB_PLAY);
        for (int i = 0; i < 20; i++) {
            in.ms += 16;
            go();
        }
        player_render(audio, 1024);
        for (int i = 0; i < 1024 * 2; i++)
            heard |= audio[i] != 0;
        CHECK("PLAY in the list plays the sound's own preview phrase", heard);
        press(PB_PLAY);       /* and PLAY again hushes it */
        for (int i = 0; i < 60; i++) {
            in.ms += 16;
            go(), player_render(audio, 1024);
        }
        for (int i = 0; i < 1024 * 2; i++)
            quiet &= audio[i] == 0;
        CHECK("PLAY again stops it", quiet);
        press(PB_HOME);
    }
    { /* + NEW, the last row of the list: the first free slot, on the step,
       * through the journal as the screen's other edits are.
       *
       * The first free slot is past the factory presets (31/10): they
       * carry names, and ch_free_instr leaves a named slot alone -- which
       * is the point of naming them. So + NEW lands on the first slot
       * song_init did not fill, and the ten sounds stay where they are. */
        int fresh = song_preset_count();

        undo_reset(&song);    /* the journal does nothing until it knows the song */
        enc(PE_PRESET, 1);
        enc(PE_K1, 100);      /* all the way down */
        press(PB_SEL);
        CHECK("+ NEW takes the first slot past the presets, and names the step",
              st(2)->instrument == fresh);
        CHECK("the slot it took is neutral and unnamed",
              !song.instruments[fresh].name[0] && song.instruments[fresh].sustain == 255);
        in.btns = 1u << PB_SEL;
        CHECK("and the list is gone: SEL asks for that instrument's screen",
              pattern_frame(&in) == PATTERN_INSTR && pattern_instr() == fresh);
        in.btns = 0;
        pattern_resume();
        go();
        enc(PE_ALGO, -1);
        CHECK("ALGO takes the new instrument back off the step", st(2)->instrument == INSTR_NONE);
    }
    { /* every slot named: the + NEW row says FULL, and takes nothing */
        for (int i = 1; i < SONG_MAX_INSTR; i++)
            song.instruments[i].name[0] = 'X', song.instruments[i].name[1] = 0;
        enc(PE_PRESET, 1);
        enc(PE_K1, 100);
        press(PB_SEL);
        CHECK("with no slot free + NEW takes nothing", st(2)->instrument == INSTR_NONE);
        press(PB_HOME);
        for (int i = 1; i < SONG_MAX_INSTR; i++)
            song.instruments[i].name[0] = 0;
    }
    press(PB_SEQ);

    /* KNOB 2 through the whole step, across the views: the four notes, the
     * three effects, the instrument, and round to the notes again -- the
     * effects before the column since 30/10, the hand going there far more
     * often. Where the cursor stands is read back from what the knobs and
     * the keys do there: a key writes in its own note slot, KNOB 3 names the
     * effect and the instrument, and on a step's number KNOB 3 is the
     * block's length. Going right the numbers are stepped over, going left
     * they are walked into, one group at a time. */
    {
        int len0 = pat_len_of();
        enc(PE_K1, 8);            /* row 10, clear of the rows the rest of this test works on */
        enc(PE_K2, -100);         /* the far left: the first number, where the knob stops */
        enc(PE_K2, 4);            /* the fourth note, with a note in it to find later */
        keys(1u << 16);
        keys(0);
        enc(PE_K2, 1);
        enc(PE_K3, 1);
        CHECK("right from the fourth note: the first effect, not the step's number",
              st(10)->fx[0].cmd != FX_NONE && pat_len_of() == len0);
        enc(PE_K2, 2);            /* on to the third effect */
        enc(PE_K3, 1);
        CHECK("and on to the third effect", st(10)->fx[2].cmd != FX_NONE);
        enc(PE_K2, 1);
        enc(PE_K3, 3);
        CHECK("right from the last effect: the instrument column, not the step's number",
              st(10)->instrument == 2 && pat_len_of() == len0);
        enc(PE_K2, 1);
        keys(1u << 19);
        keys(0);
        CHECK("past the instrument it comes round to the first note", st(10)->notes[0] == 72);
        enc(PE_K2, 7);            /* the instrument again, the notes and the effects passed */
        enc(PE_K2, -1);
        enc(PE_K3, -1);
        CHECK("left from the instrument: that group's own number", pat_len_of() == len0 - 1);
        enc(PE_K2, -1);
        enc(PE_K3, -10);
        CHECK("and only the turn after it crosses: the third effect", st(10)->fx[2].cmd == FX_NONE);
        enc(PE_K2, -3);           /* the second effect, the first, then that group's number */
        enc(PE_K3, -1);
        CHECK("left again: that group's own number", pat_len_of() == len0 - 2);
        enc(PE_K2, -1);
        enc(PE_K3, -100);         /* a frame holds an int8_t of detents */
        enc(PE_K3, -100);
        CHECK("and across it, the fourth note, where the note was left", st(10)->notes[3] == NOTE_NONE);
        /* the row and the block left as they were found */
        enc(PE_K2, -3);           /* from the fourth note to the first */
        enc(PE_K3, -100);
        enc(PE_K3, -100);
        enc(PE_K2, 4);            /* the first effect */
        enc(PE_K3, -10);
        enc(PE_K2, 3);            /* the second, the third, then the instrument */
        enc(PE_K3, -10);
        CHECK("the row is left as it was found", step_is_empty(st(10)));
        enc(PE_K2, -1);           /* that group's number */
        enc(PE_K3, 2);
        CHECK("and the block's length too", pat_len_of() == len0);
        enc(PE_K1, -8);
    }
    /* The SELECTION does not cross: half a block of notes and half of
     * effects is nothing one could paste, and the clipboard's fields are a
     * view's (30/10). The knob there still stops at the edges of the group,
     * which is what it clears that says. */
    {
        enc(PE_K1, 9);            /* row 11, its own */
        press(PB_SEQ);
        enc(PE_K2, -10);
        enc(PE_K2, 1);
        keys(1u << 16);
        keys(0);                  /* a note, to see it survive */
        enc(PE_K2, 5);            /* the first effect */
        enc(PE_K3, 1);            /* an effect, to see it go */
        press(PB_EDIT);           /* in, the anchor on it */
        enc(PE_K2, 10);           /* right, hard: loose, the strip would come round to the notes */
        keys(1u << 8);            /* OP4: clear */
        keys(0);
        press(PB_EDIT);           /* out */
        CHECK("selecting, the knob stays in the view: the effects go and the note stays",
              st(11)->fx[0].cmd == FX_NONE && st(11)->notes[0] == 69);
        press(PB_FX);             /* the first effect, an effect on it again */
        enc(PE_K3, 1);
        press(PB_EDIT);
        enc(PE_K2, -10);          /* left, hard: no further than its own number */
        keys(1u << 8);            /* OP4: clear -- whole steps, from a number */
        keys(0);
        press(PB_EDIT);
        CHECK("and left it stops at that group's number: the whole step", step_is_empty(st(11)));
        enc(PE_K2, 1);
        enc(PE_K3, 1);
        CHECK("and the view under it never turned over", st(11)->fx[0].cmd != FX_NONE);
        enc(PE_K3, -1);
        enc(PE_K1, -9);
        press(PB_SEQ);
    }

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
        /* EDIT held past a tap and let go, no key used: the SELECTION is left
         * as it was -- only a quick tap locks it on or off (30/10) */
        press(PB_EDIT);
        in.btns = 1u << PB_EDIT;
        for (int i = 0; i < EDIT_TAP + 2; i++)
            go();
        in.btns = 0;
        go();
        keys(1u << 8);        /* OP4: the selection's still, it was not closed */
        keys(0);
        CHECK("EDIT held and let go leaves the SELECTION as it was", step_is_empty(st(6)));
        press(PB_EDIT);       /* out */
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

    /* EDIT is lit only while the SELECTION is locked on, as a CAPS LOCK (30/10) */
    go();
    CHECK("EDIT is dark with no SELECTION", led[PB_EDIT] == PLAT_LED_OFF);
    press(PB_EDIT);
    go();
    CHECK("and lit once a tap locks it on", led[PB_EDIT] != PLAT_LED_OFF);
    press(PB_EDIT);
    go();
    CHECK("and dark again when the tap takes it off", led[PB_EDIT] == PLAT_LED_OFF);

    /* STOP leaves the cursor on the step it stopped on (30/10): the phrase
     * plays from its top while the cursor waits further down */
    enc(PE_K2, -10);
    enc(PE_K2, 1);            /* a note column, so a key writes where the cursor is */
    enc(PE_K1, -20);
    enc(PE_K1, 5);            /* the cursor on row 5 */
    player_stop();            /* at rest: the REC test's phrase is really stopped */
    pump();
    press(PB_PLAY);
    pump();
    press(PB_PLAY);           /* stop, the playhead still on row 0 */
    pump();
    keys(1u << 16);
    keys(0);
    CHECK("STOP puts the cursor on the step it stopped on", st(0)->notes[0] == 69);
    for (int i = 0; i < 2; i++)
        enc(PE_ALGO, -1);     /* that note taken back */

    /* A KIT -- the keyboard is its drums and nothing else, the knob steps
     * from one drum to the next, and the lit keys say which are which
     * (30/10). Instrument 00 is the one every row names. */
    {
        uint8_t drums[DRUM_MAX_DRUMS];
        int n = drum_drums(2, drums), lit = 0;
        song.instruments[0].engine = ENGINE_DRUM;
        song.instruments[0].timbre = 2; /* 80 */
        press(PB_SEQ);                  /* NOTES */
        enc(PE_K2, -20);                /* the left end: the step's number */
        enc(PE_K2, 1);                  /* and into the first note */
        enc(PE_K1, -40);
        enc(PE_K1, 5);                  /* a step of its own */
        CHECK("the kit 80 has eight drums", n == 8);
        keys(1u << 0); keys(0);
        CHECK("the first key is the kit's first drum", st(5)->notes[0] == drums[0]);
        keys(1u << 7); keys(0);
        CHECK("the eighth key is its last", st(5)->notes[0] == drums[7]);
        keys(1u << 8); keys(0);
        CHECK("the ninth plays nothing: the kit has no ninth drum", st(5)->notes[0] == drums[7]);
        press(PB_OCTUP);
        keys(1u << 0); keys(0);
        CHECK("the octave does not move a kit's keys", st(5)->notes[0] == drums[0]);
        press(PB_OCTDN);
        for (int k = 0; k < PLAT_KEYS; k++)
            lit += led[PLAT_BTNS + k] != PLAT_LED_OFF;
        CHECK("and the keys that are drums are the ones lit", lit == n);
        enc(PE_K3, 1);
        CHECK("KNOB 3 steps to the next drum, not up a semitone", st(5)->notes[0] == drums[1]);
        enc(PE_K3, 100);
        CHECK("the list ends on the last drum", st(5)->notes[0] == drums[n - 1]);
        enc(PE_K3, -n);  /* one notch a drum: the last one down to the rung under the first */
        CHECK("one notch below the first drum: OFF", st(5)->notes[0] == NOTE_OFF);
        enc(PE_K3, -1);
        CHECK("and one more: empty", st(5)->notes[0] == NOTE_NONE);
        /* OFF belongs to the first slot alone. Giving the others that rung
         * would wall them in: the knob writes OFF, the step puts --- back,
         * and --- is where the cursor stays however long one turns. */
        enc(PE_K2, 1);
        enc(PE_K3, 1);
        CHECK("another slot leaves --- on one notch", st(5)->notes[1] == drums[0]);
        enc(PE_K3, -1);
        CHECK("and goes back to it, without an OFF of its own", st(5)->notes[1] == NOTE_NONE);
        song.instruments[0].engine = ENGINE_PULSE;
    }

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
