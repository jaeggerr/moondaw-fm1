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
 *   01 BEGIN   flags (bit 0: replace a song of the same name),
 *              size u32, crc32 u32, name (1..12 characters)
 *   02 DATA    seq u16, bytes
 *   03 END
 *   04 CANCEL
 *   05 PING
 *   7F answer  cmd, status, seq u16, extra u32 (PING: bytes free)
 *
 * What arrives is written straight to a slot of the store, a piece at a
 * time: it is never whole in memory and never parsed here. The host
 * sends the file as this machine would save it (tools/fm1_send.py runs
 * the same reader and writer). The new song becomes the file only at
 * END, when its length and checksum are right -- a cable pulled halfway
 * leaves the flash as it was. */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stdint.h>

enum { LINK_CMD_BEGIN = 1, LINK_CMD_DATA, LINK_CMD_END, LINK_CMD_CANCEL, LINK_CMD_PING,
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
};
#define LINK_VERSION  1
#define LINK_DATA_MAX 240 /* bytes of song in one DATA */

enum { LINK_IDLE, LINK_RECEIVING, LINK_DONE };

void link_reset(void);
/* Once a frame, from the main loop. `may` is false where a transfer is
 * not welcome (anywhere but the MENU). LINK_DONE is returned once, for
 * the frame a song was committed in; link_slot() says where. */
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
