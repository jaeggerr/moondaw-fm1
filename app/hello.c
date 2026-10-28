/* SPDX-License-Identifier: GPL-3.0-only */
/* The bring-up app (MOONDAW_APP=hello): the same thing on the three backends. Text on the
 * screen, every control read and shown, a square wave under the keys.
 * Not the tracker -- what proves the platform layer before it. */
#include "../platform/platform.h"
#include "term.h"

/* Phase increments of C5..B5 at PLAT_FS, 2^32 = one cycle. The octaves
 * below are shifts of these. */
static const uint32_t top_octave[12] = {
    50960238u, 53990491u, 57200933u, 60602278u, 64205876u, 68023757u,
    72068660u, 76354085u, 80894335u, 85704563u, 90800821u, 96200119u,
};
#define KEY0_MIDI 53 /* key 0 is F3 */

/* Frame -> audio. Single words, written whole: the audio side may read
 * between any two of them and both orders are fine. */
static volatile uint32_t voice_inc; /* 0 = silent */
static volatile int32_t voice_amp;  /* peak, Q15 */

static const char *const btn_name[PLAT_BTNS] = {
    "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
    "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+",
};
static const char *const enc_name[PLAT_ENCS] = {
    "SELECT", "ALGO", "PRESET", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4",
};

static int32_t enc_pos[PLAT_ENCS];
static uint32_t shown_keys;

static uint32_t inc_of_key(int key)
{
    int midi = KEY0_MIDI + key;
    int oct = midi / 12 - 1; /* C5 = 72 -> 5 */
    uint32_t inc = top_octave[midi % 12];
    return oct >= 5 ? inc << (oct - 5) : inc >> (5 - oct);
}

void app_init(void)
{
    term_clear();
    term_put(0, 0, " MOONDAW FM-1                 ", C_BLACK, C_CYAN);
    term_put(1, 2, "hello world", C_WHITE, C_BLACK);
    term_put(1, 4, "KEYS", C_DIM, C_BLACK);
    term_put(1, 8, "BUTTONS", C_DIM, C_BLACK);
    term_put(1, 15, "ENCODERS", C_DIM, C_BLACK);
    term_put(1, 25, "MASTER", C_DIM, C_BLACK);
    term_put(1, 27, "NOTE", C_DIM, C_BLACK);
    term_put(1, 28, "MS", C_DIM, C_BLACK);
}

int app_load(const char *trk, uint32_t len, const char *name)
{
    (void)trk; (void)len; (void)name;
    return 0;
}

void app_frame(const PlatInput *in)
{
    static const char names[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    int top = -1;

    for (int k = 0; k < PLAT_KEYS; k++) {
        int on = (in->keys >> k) & 1u;
        term_putc(1 + k, 5, on ? '#' : '.', on ? C_GREEN : C_DIM, C_BLACK);
        if (on)
            top = k;
        if (((shown_keys ^ in->keys) >> k) & 1u)
            plat_led_key(k, on);
    }
    shown_keys = in->keys;

    for (int b = 0; b < PLAT_BTNS; b++) {
        int on = (in->btns >> b) & 1u;
        term_put(1 + (b % 5) * 6, 9 + b / 5 * 2, btn_name[b],
                 on ? C_BLACK : C_TEXT, on ? C_AMBER : C_BLACK);
        plat_led_btn(b, on);
    }

    for (int e = 0; e < PLAT_ENCS; e++) {
        enc_pos[e] += in->enc[e];
        term_put(1, 16 + e, enc_name[e], C_TEXT, C_BLACK);
        term_num(10, 16 + e, enc_pos[e], 6, in->enc[e] ? C_AMBER : C_WHITE,
                 C_BLACK);
    }

    term_num(10, 25, in->master, 6, C_WHITE, C_BLACK);
    if (top >= 0) {
        int midi = KEY0_MIDI + top;
        term_putc(10, 27, names[midi % 12 * 2], C_GREEN, C_BLACK);
        term_putc(11, 27, names[midi % 12 * 2 + 1], C_GREEN, C_BLACK);
        term_putc(12, 27, (char)('0' + midi / 12 - 1), C_GREEN, C_BLACK);
    } else {
        term_put(10, 27, "---", C_DIM, C_BLACK);
    }
    term_num(10, 28, (int32_t)(in->ms / 1000u), 6, C_DIM, C_BLACK);
    {
        int worst, load = plat_audio_load(&worst);
        if (load >= 0) {
            term_put(18, 27, "CPU", C_DIM, C_BLACK);
            term_num(22, 27, load, 4, C_WHITE, C_BLACK);
            term_put(18, 28, "MAX", C_DIM, C_BLACK);
            term_num(22, 28, worst, 4, C_WHITE, C_BLACK);
        }
    }

    /* A quarter of full scale with MASTER at the top. */
    voice_amp = (int32_t)in->master * 8;
    voice_inc = top >= 0 ? inc_of_key(top) : 0u;

    term_flush();
}

void app_audio(int16_t *out, int frames)
{
    static uint32_t phase;
    uint32_t inc = voice_inc;
    int32_t amp = voice_amp;

    for (int i = 0; i < frames; i++) {
        int16_t s = 0;
        if (inc) {
            phase += inc;
            s = (int16_t)(phase & 0x80000000u ? -amp : amp);
        } else {
            phase = 0;
        }
        out[2 * i] = s;
        out[2 * i + 1] = s;
    }
}
