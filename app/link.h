/* SPDX-License-Identifier: GPL-3.0-only */
/* Songs from a computer, over USB-MIDI SysEx: the pipe Felucca's updater
 * already uses, with another signature so the two never read each
 * other's messages.
 *
 * A message is F0 7D 4D 44 <cmd> <payload, 8 bits packed in 7> F7.
 * 7D is the manufacturer id set aside for non-commercial use; 4D 44 is
 * "MD". The host sends one message and waits for the answer: the
 * firmware holds a single SysEx at a time.
 *
 *   01 BEGIN   flags (bit 0: replace a song of the same name; bit 1:
 *              the sample bank, core/bank.h -- it replaces the bank, the
 *              name is not read),
 *              size u32, crc32 u32, name (1..12 characters)
 *   02 DATA    seq u16, bytes
 *   03 END
 *   04 CANCEL
 *   05 PING
 *   06 INFO    slot                 -> its size (extra) and name (tail);
 *                                      slot 32 (BANK_SLOT) is the bank
 *   07 READ    slot, offset u32     -> up to 120 bytes of it (tail);
 *                                      none = the end
 *   7F answer  cmd, status, seq u16, extra u32, tail
 *              (PING: version, bytes free.  INFO: slots, size.
 *               READ: bytes in this piece, offset)
 *
 * From version 2 the host can also drive the machine and write into the
 * song in memory, on any screen (app/remote.c, tools/fm1_ctl.py): so that
 * a computer -- or a model on it -- can play, measure and compose on the
 * device. Nothing of it runs until a message arrives.
 *
 *   08 STATUS  flags (bit 0: start the worst block over)   -> text (tail)
 *   09 INPUT   seq u16, pressed u32, keys u32, hold u8, enc[7] i8
 *              the buttons pressed once, the keys held `hold` frames,
 *              the knobs turned: as the hands would. A repeated seq is
 *              not done twice.
 *   0A OPEN    slot, flags (bit 0: over unsaved changes)
 *   0B NEW     flags (bit 0: over unsaved changes)
 *   0C STEP    track, block, row, notes[4], instrument, fx[3] {cmd, val}
 *   0D CELL    phrase, track, block (FF: none); a phrase past the last is made
 *   0E LENGTH  track, block, steps u16
 *   0F SAVE
 *   10 PLAY    mode (0 stop, 1 the song from `phrase`, 2 `phrase` round), phrase
 *   11 CANVAS  drawing orders, each op, length, bytes (app/canvas.c): the
 *              computer draws on a screen of its own, GLO leaves it
 *   12 EVENTS  -> the gestures on the CANVAS since the last EVENTS
 *              (tail, 27 bytes: canvas.h); extra 1 while it is in front
 *   13 SCREEN  band (0..14), offset u16  -> what the panel shows, so the
 *              computer can see it: the band's 16 lines drawn again and
 *              run-length coded -- runs of count u8 (1..255), colour
 *              RGB565 u16 -- and of that, LINK_READ_MAX bytes from
 *              `offset` (tail); extra: the coded band's whole length
 *   14 LED     pairs of led, level: the computer shows a button or a key
 *              (led: a button 0..13 in PB_* order, 14 + a key; FF: all of
 *              them), at a level 0 dark, 1 glow, 2 mid, 3 lit, 4 breath,
 *              5..8 50, 62.5, 75, 87.5 % of lit,
 *              held over what the app lights until FF gives it back
 *
 * What arrives is written straight to a slot of the store, a piece at a
 * time: it is never whole in memory and never parsed here. The host
 * sends the file as this machine would save it (tools/fm1_send.py runs
 * the same reader and writer). The new song becomes the file only at
 * END, when its length and checksum are right -- a cable pulled halfway
 * leaves the flash as it was.
 *
 * The other way round the host asks for a slot piece by piece, by
 * offset, so a piece whose answer was lost is simply asked again. */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stdint.h>

enum { LINK_CMD_BEGIN = 1, LINK_CMD_DATA, LINK_CMD_END, LINK_CMD_CANCEL, LINK_CMD_PING,
       LINK_CMD_INFO, LINK_CMD_READ,
       LINK_CMD_STATUS, LINK_CMD_INPUT, LINK_CMD_OPEN, LINK_CMD_NEW, LINK_CMD_STEP, LINK_CMD_CELL,
       LINK_CMD_LENGTH, LINK_CMD_SAVE, LINK_CMD_PLAY, LINK_CMD_CANVAS, LINK_CMD_EVENTS,
       LINK_CMD_SCREEN, LINK_CMD_LED,
       LINK_CMD_ANSWER = 0x7F };
enum {
    LINK_OK,
    LINK_BUSY,   /* the hand is not at the MENU, or nothing was begun */
    LINK_EXISTS, /* a song has that name and `replace` was not asked */
    LINK_FULL,
    LINK_BADSEQ,
    LINK_BADSUM, /* the length or the checksum is wrong: nothing was kept */
    LINK_IO,
    LINK_DIRTY,  /* it would replace the open song, which has unsaved changes */
    LINK_BADMSG,
    LINK_NONE,   /* nothing in that slot */
};
#define LINK_VERSION  5 /* 3: SCREEN, 4: LED, 5: the sample bank */
#define LINK_DATA_MAX 240 /* bytes of song in one DATA */
#define LINK_READ_MAX 120 /* in one READ: an answer never has to wait for the USB queue to drain */

enum { LINK_IDLE, LINK_RECEIVING, LINK_DONE, LINK_SENDING };

void link_reset(void);
/* The commands from STATUS on, answered by the app (app/remote.c): the
 * status, `*extra`, and a tail of up to LINK_READ_MAX bytes. Unset, they
 * are refused as LINK_BADMSG. */
extern int (*link_control)(int cmd, const uint8_t *p, uint32_t n, uint32_t *extra, uint8_t *tail,
                           uint32_t *tail_n);
/* Once a frame, from the main loop. `may` is false where a transfer is
 * not welcome (anywhere but the MENU). LINK_DONE is returned once, for
 * the frame a song was committed in; link_slot() says where. While it
 * is LINK_RECEIVING or LINK_SENDING the store is the link's: nothing
 * else may save or load. */
int link_frame(uint32_t ms, bool may);
const char *link_name(void);
uint32_t link_got(void);
uint32_t link_size(void);
int link_slot(void);

/* Shared with the host side of the tests. */
uint32_t link_pack7(const uint8_t *in, uint32_t n, uint8_t *out);
uint32_t link_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max);
uint32_t link_crc32(uint32_t crc, const uint8_t *p, uint32_t n); /* start from 0 */

#endif
