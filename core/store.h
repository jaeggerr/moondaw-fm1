/* SPDX-License-Identifier: GPL-3.0-only */
/* Songs in flash. A file is a run of 4 KB sectors, each with its own
 * header and checksum; saving writes a whole new generation beside the
 * old one and commits it with a single two-byte write, so a battery that
 * dies mid-save leaves the song as it was or as it is now, never half.
 *
 * Nothing here knows what a song is: bytes in, bytes out, a piece at a
 * time, so a file is never whole in memory. */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/platform.h"

/* 0..30 the songs, 31 the app's .LAST record, 32 the sample bank
 * (core/bank.h). 32 until 29/10: a flash written then mounts the same,
 * and a firmware from before takes slot 32's sectors for free space. */
#define STORE_SLOTS 33
#define STORE_NAME  12 /* characters, not NUL-terminated when full */
/* A sector: STORE_HEAD bytes of header, then STORE_PAYLOAD of the file;
 * the first one opens with the name. */
#define STORE_HEAD    16u
#define STORE_PAYLOAD (PLAT_FLASH_SECTOR - STORE_HEAD)

enum {
    STORE_OK = 0,
    STORE_ERR_IO = -1,     /* the flash refused, or read back wrong */
    STORE_ERR_FULL = -2,   /* no free sector: the old file is untouched */
    STORE_ERR_NONE = -3,   /* nothing in that slot */
    STORE_ERR_CORRUPT = -4 /* a sector failed its checksum */
};

/* Reads every sector header. Call once, and again after a failed save. */
int store_mount(void);
bool store_has(int slot);
/* Name (STORE_NAME + 1 bytes, NUL-terminated here) and size on flash. */
int store_info(int slot, char *name, uint32_t *bytes);
uint32_t store_free(void); /* payload bytes a new file could take */

int store_save_begin(int slot, const char *name);
int store_save_put(const char *data, uint32_t n);
int store_save_commit(void); /* from here on the new file is the file */

int store_load_begin(int slot);
int32_t store_load_get(char *buf, uint32_t cap); /* bytes, 0 at the end, < 0 on error */

int store_delete(int slot);

/* Where a file lies: the flash sector holding each STORE_PAYLOAD bytes
 * of it, in order (the name counts: the file's byte k is at payload
 * position STORE_NAME + k). Returns the count, or a STORE_ERR_*. For
 * what is read in place (the sample bank, over XIP); good until the next
 * save, commit or delete. */
int store_map(int slot, uint8_t *sectors, int max);

/* --------------------------------------------------------- shrink --- */
/* Rewrites a file smaller, in the sectors it already holds: it asks for
 * no free sector, which is the whole point -- the moment a sample has to
 * go is the moment the flash is full. The file is NOT whole while this
 * runs: a power cut leaves the slot empty, never half a file. So it is
 * only for what the computer can send again (the sample bank,
 * core/bank.h); a song goes through store_save_*, which does not touch
 * the old file before its commit.
 *
 * The new file is given in order, piece by piece: store_shrink_put for
 * bytes from memory, store_shrink_copy for a range of the file as it is
 * now, store_shrink_read to look at such a range first. A piece must
 * never land above where it came from -- `at` at or over what the new
 * file already holds, which is what lets a sector be read before it is
 * written over, and is refused otherwise. The new file must fit the
 * sectors the old one has. Sectors are not read back against their
 * checksum on the way through: the result carries its own, and what
 * built it can be sent again.
 *
 * Nothing of the slot may be read in place while this runs: bank_hold. */
int store_shrink_begin(int slot);
int store_shrink_put(const char *data, uint32_t n);
int store_shrink_copy(uint32_t at, uint32_t len);
int store_shrink_read(uint32_t at, char *out, uint32_t n);
int store_shrink_commit(void);

#endif
