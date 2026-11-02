/* SPDX-License-Identifier: GPL-3.0-only */
#include "bank.h"

#include "sampler.h"

typedef char payload_is_16_blocks[STORE_PAYLOAD == SMP_BLOCK * 16 ? 1 : -1];
typedef char bank_fits_the_map[PLAT_FLASH_SECTORS <= SMP_MAX_SECTORS ? 1 : -1];

static const uint8_t *payload[SMP_MAX_SECTORS]; /* each sector's payload, in place */
static int nsec, count;
static uint32_t size;
static const Song *song;

/* A byte of the bank file, by its offset: through the sector map. */
static uint8_t rd8(uint32_t off)
{
    uint32_t p = off + STORE_NAME, s = p / STORE_PAYLOAD;
    return s < (uint32_t)nsec ? payload[s][p - s * STORE_PAYLOAD] : 0;
}

static uint32_t rd32(uint32_t off)
{
    return (uint32_t)rd8(off) | (uint32_t)rd8(off + 1) << 8 | (uint32_t)rd8(off + 2) << 16 |
           (uint32_t)rd8(off + 3) << 24;
}

static bool entry_ok(int i)
{
    uint32_t e = BANK_HEAD + (uint32_t)i * BANK_ENTRY;
    uint32_t frames = rd32(e + BANK_NAME_LEN), at = rd32(e + BANK_NAME_LEN + 4);
    uint32_t blocks = (frames + SMP_BLOCK_FRAMES - 1) / SMP_BLOCK_FRAMES;

    return rd8(e) && frames && frames <= 4 * SMP_RATE && (at + STORE_NAME) % SMP_BLOCK == 0 &&
           at <= size && blocks * SMP_BLOCK <= size - at;
}

void bank_hold(void)
{
    smp_hold();
    nsec = count = 0;
}

int bank_mount(void)
{
    uint8_t map[SMP_MAX_SECTORS];
    const uint8_t *base = plat_flash_map();
    char name[STORE_NAME + 1];
    int n, rc = STORE_OK;

    bank_hold();
    size = 0;
    if (!store_has(BANK_SLOT))
        return STORE_ERR_NONE;
    if (!base || store_info(BANK_SLOT, name, &size) != STORE_OK ||
        (n = store_map(BANK_SLOT, map, SMP_MAX_SECTORS)) < 0)
        return STORE_ERR_IO;
    for (int q = 0; q < n; q++)
        payload[q] = base + (uint32_t)map[q] * PLAT_FLASH_SECTOR + STORE_HEAD;
    nsec = n;
    if (size < BANK_HEAD || rd8(0) != 'S' || rd8(1) != 'M' || rd8(2) != 'P' || rd8(3) != '1' ||
        rd8(4) > BANK_MAX || BANK_HEAD + (uint32_t)rd8(4) * BANK_ENTRY > size) {
        nsec = 0;
        return STORE_ERR_CORRUPT;
    }
    count = rd8(4);
    for (int i = 0; i < count; i++)
        if (!entry_ok(i)) {
            count = i; /* what reads whole, up to the first that does not */
            rc = STORE_ERR_CORRUPT;
            break;
        }
    smp_attach(payload, nsec);
    if (song)
        bank_resolve(song);
    return rc;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

int bank_find(const char *nm)
{
    const char *key = nm;
    int len = 0;

    for (const char *p = nm; *p; p++)
        if (*p == '/' || *p == '\\')
            key = p + 1;
    while (key[len])
        len++;
    if (len > 4 && key[len - 4] == '.' && lower(key[len - 3]) == 'w' && lower(key[len - 2]) == 'a' &&
        lower(key[len - 1]) == 'v')
        len -= 4;
    if (len == 0 || len >= BANK_NAME_LEN)
        return -1;
    for (int i = 0; i < count; i++) {
        uint32_t e = BANK_HEAD + (uint32_t)i * BANK_ENTRY;
        int k = 0;
        while (k < len && lower((char)rd8(e + (uint32_t)k)) == lower(key[k]))
            k++;
        if (k == len && rd8(e + (uint32_t)k) == 0)
            return i;
    }
    return -1;
}

void bank_resolve(const Song *s)
{
    song = s;
    for (int i = 0; i < SONG_SMP_MAX; i++) {
        int b = s->smp[i].name[0] ? bank_find(s->smp[i].name) : -1;
        uint32_t e = BANK_HEAD + (uint32_t)b * BANK_ENTRY;
        if (b < 0)
            smp_map(i, 0, 0);
        else
            smp_map(i, rd32(e + BANK_NAME_LEN + 4) + STORE_NAME, rd32(e + BANK_NAME_LEN));
    }
}

bool bank_place(int i, uint32_t *at, uint32_t *frames)
{
    uint32_t e = BANK_HEAD + (uint32_t)i * BANK_ENTRY;

    if (i < 0 || i >= count)
        return false;
    *at = rd32(e + BANK_NAME_LEN + 4) + STORE_NAME;
    *frames = rd32(e + BANK_NAME_LEN);
    return true;
}

int bank_count(void) { return count; }
uint32_t bank_bytes(void) { return size; }

bool bank_entry(int i, char name[BANK_NAME_LEN], uint32_t *frames)
{
    uint32_t e = BANK_HEAD + (uint32_t)i * BANK_ENTRY;
    if (i < 0 || i >= count)
        return false;
    for (int k = 0; k < BANK_NAME_LEN; k++)
        name[k] = (char)rd8(e + (uint32_t)k);
    name[BANK_NAME_LEN - 1] = '\0';
    *frames = rd32(e + BANK_NAME_LEN);
    return true;
}

static uint32_t ld32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void st32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* The bank without sample `i`, written over the bank itself. What makes it
 * a plain shift: a sample is a whole number of blocks, so the hole it
 * leaves is a multiple of SMP_BLOCK and every block that comes down lands
 * on a block boundary again -- none of them crosses a sector, nothing is
 * decoded, and the entries after the hole only have their place lowered.
 * The index keeps the size it had, padded as the packer pads it, so the
 * blocks begin where they began and everything before the hole does not
 * move at all. */
int bank_delete(int i)
{
    static const char zero[BANK_ENTRY];
    uint32_t e = BANK_HEAD + (uint32_t)i * BANK_ENTRY, hole, at, head_end, pad, sz = size;
    uint8_t h[BANK_HEAD], b[4];
    int rc, n = count;

    if (i < 0 || i >= n)
        return STORE_ERR_NONE;
    if (n == 1) { /* nothing left to close up: the file goes */
        bank_hold();
        rc = store_delete(BANK_SLOT);
        bank_mount();
        return rc;
    }
    hole = (rd32(e + BANK_NAME_LEN) + SMP_BLOCK_FRAMES - 1) / SMP_BLOCK_FRAMES * SMP_BLOCK;
    at = rd32(e + BANK_NAME_LEN + 4);
    head_end = rd32(BANK_HEAD + BANK_NAME_LEN + 4); /* entry 0's: where the blocks begin */
    bank_hold();                                    /* from here nothing of it is read in place */
    if ((rc = store_shrink_begin(BANK_SLOT)) != STORE_OK) {
        bank_mount();
        return rc;
    }
    h[0] = 'S', h[1] = 'M', h[2] = 'P', h[3] = '1';
    h[4] = (uint8_t)(n - 1);
    h[5] = h[6] = h[7] = 0;
    rc = store_shrink_put((const char *)h, BANK_HEAD);
    if (rc == STORE_OK && i)
        rc = store_shrink_copy(BANK_HEAD, (uint32_t)i * BANK_ENTRY);
    for (int j = i + 1; j < n && rc == STORE_OK; j++) {
        uint32_t f = BANK_HEAD + (uint32_t)j * BANK_ENTRY;
        rc = store_shrink_copy(f, BANK_NAME_LEN + 4); /* its name and its length */
        if (rc == STORE_OK)
            rc = store_shrink_read(f + BANK_NAME_LEN + 4, (char *)b, 4);
        if (rc == STORE_OK) {
            st32(b, ld32(b) - hole); /* its place, a hole lower */
            rc = store_shrink_put((const char *)b, 4);
        }
    }
    for (pad = head_end - (BANK_HEAD + (uint32_t)(n - 1) * BANK_ENTRY); pad && rc == STORE_OK;) {
        uint32_t k = pad < sizeof zero ? pad : sizeof zero;
        rc = store_shrink_put(zero, k);
        pad -= k;
    }
    if (rc == STORE_OK)
        rc = store_shrink_copy(head_end, at - head_end); /* the blocks before it */
    if (rc == STORE_OK && sz > at + hole)
        rc = store_shrink_copy(at + hole, sz - at - hole); /* and those after */
    if (rc == STORE_OK)
        rc = store_shrink_commit();
    bank_mount(); /* the bank as it is now, under the song's names */
    return rc;
}
