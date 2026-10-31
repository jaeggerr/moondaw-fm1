/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI IN: a keyboard's notes, from the bytes to the sound.
 *
 * The parser every stream goes through (platform/midi_parse.h):
 *   1. a note on and a note off, the note and the velocity as sent;
 *   2. a note on at velocity 0 is the note let go;
 *   3. running status: one status byte, then pair after pair;
 *   4. all notes off (123) and all sound off (120) let everything go;
 *      the controllers a keyboard sends from its knobs do nothing;
 *   5. clock, SysEx, program change and pitch bend are stepped over, and
 *      none of them swallows the note that follows;
 *   6. more notes in a frame than it holds: everything let go, once;
 *   7. USB-MIDI event packets say the same as the bytes do;
 *   8. and so does a stream arriving a byte at a time, which is how
 *      the 3.5 mm jack's UART leaves it (firmware.c, midi_drain).
 *
 * Then what a note does on a screen, through the player (app/ui.h):
 *   9. it sounds, and lets go when the keyboard does;
 *  10. its pitch is the keyboard's, whatever OCT- / OCT+ say;
 *  11. it plays the instrument the cursor is on, not a channel's;
 *  12. it writes nothing: the step under the cursor is left alone.
 *
 *   midi_test        exit 1 if any fails */
#include <stdio.h>
#include <string.h>

#include "../app/pattern.h"
#include "../app/ui.h"
#include "../core/player.h"
#include "../core/trk.h"
#include "../platform/midi_parse.h"

static int fails;
#define CHECK(what, ok)                                                     \
    do {                                                                    \
        if (!(ok)) {                                                        \
            printf("midi_test: FAIL %s\n", what);                           \
            fails++;                                                        \
        }                                                                   \
    } while (0)

/* ------------------------------------------------------- the parser --- */
static MidiParse p;

/* the bytes of one frame, from a parser that knows nothing yet */
static void feed(const uint8_t *b, uint32_t n)
{
    memset(&p, 0, sizeof p);
    midi_parse_bytes(&p, b, n);
}

static int ev(int i, int note, int vel)
{
    return i < p.n && p.q[i].note == note && p.q[i].vel == vel;
}

static void parser(void)
{
    {   /* 1 */
        static const uint8_t b[] = { 0x90, 60, 100, 0x80, 60, 64 };
        feed(b, sizeof b);
        CHECK("a note on and a note off", p.n == 2 && ev(0, 60, 100) && ev(1, 60, 0));
    }
    {   /* 2 */
        static const uint8_t b[] = { 0x90, 60, 100, 0x90, 60, 0 };
        feed(b, sizeof b);
        CHECK("velocity 0 is the note let go", p.n == 2 && ev(0, 60, 100) && ev(1, 60, 0));
    }
    {   /* 3 -- what a keyboard sends when it saves a byte */
        static const uint8_t b[] = { 0x90, 60, 100, 64, 90, 67, 80, 60, 0 };
        feed(b, sizeof b);
        CHECK("running status", p.n == 4 && ev(0, 60, 100) && ev(1, 64, 90) && ev(2, 67, 80) && ev(3, 60, 0));
    }
    {   /* 4 */
        static const uint8_t b123[] = { 0x90, 60, 100, 0xB0, 123, 0 };
        static const uint8_t b120[] = { 0x90, 60, 100, 0xB0, 120, 0 };
        static const uint8_t knob[] = { 0xB0, 74, 99, 0xB0, 1, 40 };
        feed(b123, sizeof b123);
        CHECK("all notes off lets everything go", p.n == 1 && ev(0, PLAT_MIDI_RELEASE, 0));
        feed(b120, sizeof b120);
        CHECK("all sound off lets everything go", p.n == 1 && ev(0, PLAT_MIDI_RELEASE, 0));
        feed(knob, sizeof knob);
        CHECK("a knob's controller does nothing", p.n == 0);
    }
    {   /* 5 -- a stream as it really arrives: the clock runs through the notes */
        static const uint8_t b[] = { 0xF8, 0xF0, 0x7D, 0x4D, 0x44, 0x01, 0xF7, 0xF8,
                                     0xC0, 7, 0xE0, 0, 64, 0xF8, 0x90, 60, 100 };
        feed(b, sizeof b);
        CHECK("clock, SysEx, program change and bend stepped over", p.n == 1 && ev(0, 60, 100));
    }
    {   /* 6 */
        uint8_t b[3 * (PLAT_MIDI_MAX + 4)];
        for (unsigned i = 0; i < sizeof b / 3; i++)
            b[3 * i] = 0x90, b[3 * i + 1] = (uint8_t)(40 + i), b[3 * i + 2] = 100;
        feed(b, sizeof b);
        CHECK("a frame that cannot hold them lets everything go", p.n == 1 && ev(0, PLAT_MIDI_RELEASE, 0));
    }
    {   /* 7 -- usb.c's ring: the code index in the low nibble, then the message */
        memset(&p, 0, sizeof p);
        midi_parse_usb(&p, 0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);
        midi_parse_usb(&p, 0x08u | 0x80u << 8 | 60u << 16 | 64u << 24);
        CHECK("a note on and off over USB", p.n == 2 && ev(0, 60, 100) && ev(1, 60, 0));
        memset(&p, 0, sizeof p);
        midi_parse_usb(&p, 0x0Fu | 0xF8u << 8);                /* the clock */
        midi_parse_usb(&p, 0x04u | 0xF0u << 8 | 0x7Du << 16);  /* a SysEx packet */
        midi_parse_usb(&p, 0x0Cu | 0xC0u << 8 | 7u << 16);     /* a program change */
        CHECK("neither clock nor SysEx nor program change is a note", p.n == 0);
        midi_parse_usb(&p, 0x0Bu | 0xB0u << 8 | 123u << 16);
        CHECK("all notes off over USB", p.n == 1 && ev(0, PLAT_MIDI_RELEASE, 0));
    }
    {   /* 8 -- the jack arrives a byte at a time, as the UART's DMA leaves them */
        static const uint8_t b[] = { 0x90, 60, 100, 64, 90, 0x80, 60, 0 };
        memset(&p, 0, sizeof p);
        for (unsigned i = 0; i < sizeof b; i++)
            midi_parse_bytes(&p, &b[i], 1u);
        CHECK("a byte at a time says the same thing",
              p.n == 3 && ev(0, 60, 100) && ev(1, 64, 90) && ev(2, 60, 0));
    }
}

/* ------------------------------------------- a note on a screen --- */
/* PATTERN without a screen, as tests/pattern_test.c drives it. The song
 * holds two instruments and a step that names the second one, so that
 * what the cursor is on can be heard: a PULSE at A-4 is a handful of
 * zero crossings a block, a NOISE a great many. */
void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
void plat_led_level(int l, int v) { (void)l; (void)v; }
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_battery(int *raw) { if (raw) *raw = -1; return -1; }
int plat_flash_read(uint32_t o, void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
const uint8_t *plat_flash_map(void) { return 0; }
int plat_flash_erase(uint32_t s) { (void)s; return -1; }
int plat_flash_prog(uint32_t o, const void *d, uint32_t n) { (void)o; (void)d; (void)n; return -1; }
int app_save(void) { return 0; }
void app_mark_dirty(void) { }

static Song song;
static PlatInput in;
static int16_t pcm[4096 * 2];

static const char trk[] =
    "version 3\nbpm 120\nticks 6\ntracks 1\n"
    "inst 00 engine PULSE timbre 2 adsr 0 0 255 10 name TONE\n"
    "inst 01 engine NOISE timbre 0 adsr 0 0 255 10 name HISS\n"
    "pat t0 00 len 16\n00 C-4 i00\n08 --- i01\n"
    "arr 00 steps auto\nt0 00\n";

static void go(void) { pattern_frame(&in); memset(in.enc, 0, sizeof in.enc); in.ms += 16; }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
static const Step *st(int row) { return song_step(&song, 0, song.arrangement[0].pattern[0], row); }

/* one frame carrying one MIDI event, as a backend fills PlatInput.midi */
static void midi(int note, int vel)
{
    in.midi[0].note = (uint8_t)note;
    in.midi[0].vel = (uint8_t)vel;
    in.midi_n = 1;
    go();
    in.midi_n = 0;
}

static long loudness(void) /* the average sample of the last render */
{
    long sum = 0;
    for (int i = 0; i < 4096; i++)
        sum += pcm[2 * i] < 0 ? -pcm[2 * i] : pcm[2 * i];
    return sum / 4096;
}

static int crossings(void) /* how often it crosses zero: the pitch, roughly */
{
    int n = 0;
    for (int i = 1; i < 4096; i++)
        if ((pcm[2 * i] >= 0) != (pcm[2 * (i - 1)] >= 0))
            n++;
    return n;
}

static void screen(void)
{
    uint8_t was[PATTERN_MAX_STEPS];
    long held, gone;
    int tone, after_oct, hiss, r, same = 1;

    trk_load(&song, trk, sizeof trk - 1, NULL);
    player_init(&song);
    CHECK("the block opens", pattern_open(&song, 0, 0));
    go(); /* the frame after the way in: nothing held counts as a press */
    for (r = 0; r < PATTERN_MAX_STEPS; r++)
        was[r] = st(r)->notes[0];

    /* 9 -- A-4 on the PULSE the cursor is on */
    midi(69, 100);
    player_render(pcm, 4096);
    held = loudness();
    tone = crossings();
    CHECK("a MIDI note sounds", held > 200);
    midi(69, 0);
    player_render(pcm, 4096);
    player_render(pcm, 4096);
    gone = loudness();
    CHECK("let go on the keyboard, it stops", gone < held / 4);

    /* 10 -- the octave buttons are the panel's keyboard, not this one */
    press(PB_OCTUP);
    press(PB_OCTUP);
    midi(69, 100);
    player_render(pcm, 4096);
    after_oct = crossings();
    CHECK("its pitch is the keyboard's, whatever OCT says",
          after_oct > tone - 4 && after_oct < tone + 4);
    midi(69, 0);

    /* 11 -- the step at row 8 says i01, a NOISE: the same note, another voice */
    CHECK("the song names the second instrument at row 8", st(8)->instrument == 1);
    enc(PE_K1, 9); /* the cursor past it: the instrument in force is still i01 */
    midi(69, 100);
    player_render(pcm, 4096);
    hiss = crossings();
    CHECK("the instrument is the one the cursor is on, not a channel's", hiss > 4 * tone);
    midi(69, 0);

    /* 12 */
    for (r = 0; r < PATTERN_MAX_STEPS; r++)
        same = same && st(r)->notes[0] == was[r];
    CHECK("a MIDI note writes nothing", same);
}

int main(void)
{
    parser();
    screen();
    printf("midi_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
