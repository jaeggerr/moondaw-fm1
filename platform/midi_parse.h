/* SPDX-License-Identifier: GPL-3.0-only */
/* A stream of MIDI bytes to the notes a keyboard plays. Three streams
 * carry the same thing and all three come through here: the event packets
 * of USB-MIDI over the cable (midi_parse_usb, firmware/src/usb.c's ring),
 * the 3.5 mm MIDI jack's 31250 baud, a byte at a time as the UART's DMA
 * leaves them, and CoreMIDI's packets in the simulator (midi_parse_bytes).
 * Running status is understood in one place, this one.
 *
 * Only notes come out, into PlatInput.midi (platform.h): note on, note
 * off -- a note on at velocity 0 is one -- and the two controllers that
 * let everything go, 120 (all sound off) and 123 (all notes off).
 * Everything else is stepped over: clock and SysEx, program changes, and
 * the controllers a keyboard sends from its own knobs and pads while it
 * plays, which the MiniLab 3 does.
 *
 * The channel is not read: what a note plays is the instrument of the
 * screen in front, not a channel's (app/ui.h, ui_midi_play).
 *
 * The parser holds no notes of its own. `q` is one frame's events and the
 * caller empties it (n = 0) once it has taken them; a frame that cannot
 * hold them all is a break in the stream, and then everything is let go
 * rather than left hanging. */
#ifndef MIDI_PARSE_H
#define MIDI_PARSE_H

#include <stdint.h>

#include "platform.h"

typedef struct {
    PlatMidi q[PLAT_MIDI_MAX];
    uint8_t n;    /* events in q */
    uint8_t lost; /* this frame overran: it says so and holds nothing more */
    uint8_t st;   /* the running status, 0 = none to speak of */
    uint8_t d1;   /* the first data byte, while the second is awaited */
    uint8_t have;
} MidiParse;

/* Everything let go, whatever was pressed: the only event the frame then
 * holds, since what came before it is void. */
static inline void midi_parse_release(MidiParse *p)
{
    p->n = 1;
    p->q[0].note = PLAT_MIDI_RELEASE;
    p->q[0].vel = 0;
}

static inline void midi_parse_push(MidiParse *p, uint8_t note, uint8_t vel)
{
    if (!p->n)
        p->lost = 0;           /* the frame before this one was taken */
    if (p->lost)
        return;                /* a broken frame: what follows the release is of no use either */
    if (p->n >= PLAT_MIDI_MAX) {
        midi_parse_release(p); /* more notes than a frame holds: the stream is broken */
        p->lost = 1;
        return;
    }
    p->q[p->n].note = (uint8_t)(note & 0x7Fu);
    p->q[p->n].vel = (uint8_t)(vel & 0x7Fu);
    p->n++;
}

/* The data bytes a status byte takes, 0 for the ones stepped over. */
static inline uint32_t midi_parse_len(uint8_t st)
{
    switch (st & 0xF0u) {
    case 0x80u:
    case 0x90u:
    case 0xA0u:
    case 0xB0u:
    case 0xE0u:
        return 2u;
    case 0xC0u:
    case 0xD0u:
        return 1u;
    default:
        return st == 0xF2u ? 2u : st == 0xF1u || st == 0xF3u ? 1u : 0u;
    }
}

/* One whole message, its status byte first. */
static inline void midi_parse_msg(MidiParse *p, uint8_t st, uint8_t d1, uint8_t d2)
{
    switch (st & 0xF0u) {
    case 0x90u:
        midi_parse_push(p, d1, d2); /* velocity 0 is the note let go */
        break;
    case 0x80u:
        midi_parse_push(p, d1, 0);
        break;
    case 0xB0u:
        if (d1 == 120u || d1 == 123u)
            midi_parse_release(p);
        break;
    default:
        break;
    }
}

static inline void midi_parse_bytes(MidiParse *p, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c >= 0xF8u)
            continue;          /* clock and the other real time: no status of their own */
        if (c >= 0x80u) {      /* a status byte, F0 and F7 among them: a half-read message is void */
            p->st = midi_parse_len(c) ? c : 0u;
            p->have = 0;
            continue;
        }
        if (!p->st)
            continue;          /* data with nothing to do: SysEx's bytes, or a stream joined mid-way */
        if (midi_parse_len(p->st) == 2u && !p->have) {
            p->d1 = c;
            p->have = 1;
            continue;
        }
        midi_parse_msg(p, p->st, midi_parse_len(p->st) == 2u ? p->d1 : c, c);
        p->have = 0;           /* running status: the next pair plays again */
    }
}

/* One USB-MIDI event packet: the code index number in the low nibble,
 * then the message itself, status byte first. usb.c's ring holds only
 * channel messages and the clock, so a packet is one message; its own
 * length is read from the status byte, as a stream's is. */
static inline void midi_parse_usb(MidiParse *p, uint32_t pkt)
{
    uint8_t b[3];
    uint32_t cin = pkt & 15u;
    if (cin < 8u || cin > 0xEu)
        return;
    b[0] = (uint8_t)(pkt >> 8);
    b[1] = (uint8_t)(pkt >> 16);
    b[2] = (uint8_t)(pkt >> 24);
    midi_parse_bytes(p, b, 1u + midi_parse_len(b[0]));
}

#endif
