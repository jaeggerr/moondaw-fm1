/* SPDX-License-Identifier: GPL-3.0-only */
/* The flash store against a NOR that behaves like one -- programming
 * only clears bits, so a write over a sector that was not erased shows
 * -- and that can lose power at any operation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/store.h"
#include "../platform/platform.h"

static uint8_t nor[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];
static long ops, cut_at = -1; /* the operation the power dies in, -1 = never */
static int fails;

static int dead(void) { return cut_at >= 0 && ops >= cut_at; }

int plat_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof nor)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}

int plat_flash_erase(uint32_t sector)
{
    if (sector >= PLAT_FLASH_SECTORS || dead())
        return -1;
    /* cut in the middle of an erase: half the sector is gone */
    if (ops++ == cut_at - 1 && cut_at > 0) {
        memset(nor + sector * PLAT_FLASH_SECTOR, 0xFF, PLAT_FLASH_SECTOR / 2);
        ops = cut_at;
        return -1;
    }
    memset(nor + sector * PLAT_FLASH_SECTOR, 0xFF, PLAT_FLASH_SECTOR);
    return 0;
}

int plat_flash_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *p = src;
    if (off + n > sizeof nor || dead())
        return -1;
    if (ops++ == cut_at - 1 && cut_at > 0) { /* cut mid-write: the first half lands */
        for (uint32_t i = 0; i < n / 2; i++)
            nor[off + i] &= p[i];
        ops = cut_at;
        return -1;
    }
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= p[i];
    return 0;
}

#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

static char *make(int seed, uint32_t n)
{
    char *p = malloc(n + 1);
    for (uint32_t i = 0; i < n; i++)
        p[i] = (char)('A' + (i * 7 + (uint32_t)seed * 13 + i / 97) % 26);
    p[n] = 0;
    return p;
}

static int save(int slot, const char *name, const char *data, uint32_t n)
{
    int rc = store_save_begin(slot, name);
    for (uint32_t at = 0; !rc && at < n; at += 1000) /* in pieces, as the writer gives them */
        rc = store_save_put(data + at, n - at < 1000 ? n - at : 1000);
    return rc ? rc : store_save_commit();
}

static long load(int slot, char *out, uint32_t cap)
{
    long n = 0, got;
    if (store_load_begin(slot))
        return -100;
    while ((got = store_load_get(out + n, 777)) > 0) {
        n += got;
        if ((uint32_t)n + 777 > cap)
            return -101;
    }
    return got < 0 ? got : n;
}

int main(void)
{
    static char out[400000];
    char name[STORE_NAME + 1];
    uint32_t bytes;
    char *a = make(1, 9000), *b = make(2, 23000), *c = make(3, 100);

    memset(nor, 0xFF, sizeof nor);
    CHECK("mounts an erased flash", store_mount() == 0 && !store_has(0));
    CHECK("an empty flash has room", store_free() > 290000);
    CHECK("saves", save(0, "KIDS", a, 9000) == 0);
    CHECK("loads the same bytes", load(0, out, sizeof out) == 9000 && !memcmp(out, a, 9000));
    CHECK("knows the name and the size",
          store_info(0, name, &bytes) == 0 && !strcmp(name, "KIDS") && bytes == 9000);
    CHECK("a second slot does not disturb the first",
          save(5, "TWELVECHARSX", b, 23000) == 0 && load(0, out, sizeof out) == 9000 &&
          load(5, out, sizeof out) == 23000 && !memcmp(out, b, 23000));
    store_info(5, name, &bytes);
    CHECK("a name of twelve characters comes back whole", !strcmp(name, "TWELVECHARSX"));
    CHECK("survives a remount", store_mount() == 0 && load(5, out, sizeof out) == 23000);
    CHECK("saving over a file replaces it",
          save(0, "KIDS", c, 100) == 0 && load(0, out, sizeof out) == 100 && !memcmp(out, c, 100));
    CHECK("an empty file is a file", save(7, "EMPTY", "", 0) == 0 && load(7, out, sizeof out) == 0);

    {   /* a hundred saves over the same slot: sectors are reclaimed */
        int ok = 1;
        for (int i = 0; i < 100 && ok; i++)
            ok = save(0, "KIDS", i & 1 ? a : b, i & 1 ? 9000 : 23000) == 0;
        CHECK("a hundred saves over one slot", ok && load(0, out, sizeof out) == 9000);
    }
    CHECK("delete", store_delete(5) == 0 && !store_has(5) && store_mount() == 0 && !store_has(5));
    CHECK("a deleted file does not come back after saves elsewhere",
          save(1, "X", a, 9000) == 0 && store_mount() == 0 && !store_has(5));

    {   /* corruption is seen, not played */
        uint8_t keep;
        store_mount();
        save(9, "CRC", b, 23000);
        for (uint32_t k = 0; k < sizeof nor; k += PLAT_FLASH_SECTOR)
            if (nor[k + 4] == 9 && nor[k + 5] == 2) {
                keep = nor[k + 100];
                nor[k + 100] = (uint8_t)(keep & 0xFE) == keep ? keep & 0xFD : keep & 0xFE;
            }
        CHECK("a flipped bit is refused", load(9, out, sizeof out) == STORE_ERR_CORRUPT);
        store_delete(9);
    }

    {   /* full: the old file stays */
        char *big = make(4, 280000);
        int rc;
        store_mount();
        save(2, "OLD", a, 9000);
        rc = save(2, "OLD", big, 280000);
        store_mount();
        CHECK("a save that does not fit says so", rc == STORE_ERR_FULL);
        CHECK("and leaves the old file", load(2, out, sizeof out) == 9000 && !memcmp(out, a, 9000));
        free(big);
    }

    {   /* The power dies at every operation of a save over an existing
         * file. Whatever is left must be the old file or the new one. */
        static uint8_t before[sizeof nor];
        long total, olds = 0, news = 0, bad = 0;

        memset(nor, 0xFF, sizeof nor);
        store_mount();
        save(3, "OTHER", c, 100);
        save(0, "SONG", a, 9000);
        memcpy(before, nor, sizeof nor);
        ops = 0;
        save(0, "SONG", b, 23000);
        total = ops;
        for (long cut = 1; cut <= total; cut++) {
            long n;
            memcpy(nor, before, sizeof nor);
            store_mount();
            ops = 0;
            cut_at = cut;
            save(0, "SONG", b, 23000);
            cut_at = -1; /* power back */
            store_mount();
            n = load(0, out, sizeof out);
            if (n == 9000 && !memcmp(out, a, 9000))
                olds++;
            else if (n == 23000 && !memcmp(out, b, 23000))
                news++;
            else
                bad++;
            if (load(3, out, sizeof out) != 100 || memcmp(out, c, 100))
                bad++;
            /* and the machine can still save afterwards */
            if (save(0, "SONG", c, 100) != 0 || load(0, out, sizeof out) != 100)
                bad++;
        }
        printf("power cut at each of %ld flash operations: %ld times the old file, %ld the new, %ld bad\n",
               total, olds, news, bad);
        CHECK("a cut save leaves the old file or the new one", bad == 0 && olds > 0);
    }
    free(a), free(b), free(c);
    printf("store test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
