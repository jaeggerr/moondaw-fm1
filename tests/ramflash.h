/* SPDX-License-Identifier: GPL-3.0-only */
/* The 292 KB of NOR the store lives in, as an array: erased is 0xFF and
 * programming only clears bits. For the tests that need a flash and no
 * power cuts (tests/store_test.c has its own, with cuts). */
#ifndef RAMFLASH_H
#define RAMFLASH_H

#include <string.h>

#include "../platform/platform.h"

static uint8_t nor[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];

static void ramflash_blank(void) { memset(nor, 0xFF, sizeof nor); }

int plat_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof nor)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}

int plat_flash_erase(uint32_t sector)
{
    if (sector >= PLAT_FLASH_SECTORS)
        return -1;
    memset(nor + sector * PLAT_FLASH_SECTOR, 0xFF, PLAT_FLASH_SECTOR);
    return 0;
}

int plat_flash_prog(uint32_t off, const void *src, uint32_t n)
{
    if (off + n > sizeof nor)
        return -1;
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}

#endif
