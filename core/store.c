/* SPDX-License-Identifier: GPL-3.0-only */
#include "store.h"

/* A sector: 16 bytes of header, then payload.
 *
 *    0  'M' 'D' 'W' '1'
 *    4  slot            5  seq (0 = the file's first sector)
 *    6  generation, 16 bits, little-endian: a save writes old + 1
 *    8  payload length  10  CRC-16 of the payload
 *   12  count, and at 14 its complement -- on seq 0 only, and 0xFFFF
 *       until the commit: the number of sectors of the file. Flash can
 *       clear bits without an erase, so the commit is a write of these
 *       four bytes into a sector that is already there; the complement
 *       is what tells a commit from one the power cut in half.
 *
 * The first sector's payload opens with the name, STORE_NAME bytes. */
#define SECTORS PLAT_FLASH_SECTORS
#define SECTOR PLAT_FLASH_SECTOR
#define HEAD STORE_HEAD
#define PAYLOAD STORE_PAYLOAD
#define NO_SECTOR 0xFF

typedef struct {
    uint8_t slot, seq; /* slot NO_SECTOR: not one of ours */
    uint16_t gen, count;
} Info;

static Info info[SECTORS];
static uint8_t first[STORE_SLOTS]; /* the live file's seq 0, or NO_SECTOR */
static uint8_t buf[SECTOR];        /* one sector; the flash is programmed from RAM */

static struct {
    int slot;
    uint16_t gen;
    uint8_t seq, first, wrote[SECTORS];
    uint32_t fill;
    bool open;
} sv;
static struct {
    int slot;
    uint8_t seq, count;
    uint16_t gen;
    uint32_t at, len; /* within the sector held in buf */
    bool open;
} ldr;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++)
            c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

static bool newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

/* The sector holding (slot, gen, seq), or NO_SECTOR. */
static uint8_t find(int slot, uint16_t gen, int seq)
{
    for (uint32_t k = 0; k < SECTORS; k++)
        if (info[k].slot == slot && info[k].gen == gen && info[k].seq == seq)
            return (uint8_t)k;
    return NO_SECTOR;
}

static bool complete(uint32_t k0)
{
    if (info[k0].count == 0xFFFF || info[k0].count == 0 || info[k0].count > SECTORS)
        return false;
    for (int q = 1; q < info[k0].count; q++)
        if (find(info[k0].slot, info[k0].gen, q) == NO_SECTOR)
            return false;
    return true;
}

static bool live(uint32_t k)
{
    uint8_t f = info[k].slot < STORE_SLOTS ? first[info[k].slot] : NO_SECTOR;
    return f != NO_SECTOR && info[k].gen == info[f].gen && info[k].seq < info[f].count;
}

int store_mount(void)
{
    uint8_t h[HEAD];
    int rc = STORE_OK;

    for (uint32_t k = 0; k < SECTORS; k++) {
        info[k].slot = NO_SECTOR;
        if (plat_flash_read(k * SECTOR, h, HEAD) != 0) {
            rc = STORE_ERR_IO;
            continue;
        }
        if (h[0] != 'M' || h[1] != 'D' || h[2] != 'W' || h[3] != '1' || h[4] >= STORE_SLOTS)
            continue;
        info[k].slot = h[4];
        info[k].seq = h[5];
        info[k].gen = rd16(h + 6);
        info[k].count = (uint16_t)(rd16(h + 12) ^ rd16(h + 14)) == 0xFFFF ? rd16(h + 12) : 0xFFFF;
    }
    /* For each slot, the newest generation that was committed and has
     * all its sectors. Everything else is free space. */
    for (int s = 0; s < STORE_SLOTS; s++) {
        first[s] = NO_SECTOR;
        for (uint32_t k = 0; k < SECTORS; k++)
            if (info[k].slot == s && info[k].seq == 0 && complete(k) &&
                (first[s] == NO_SECTOR || newer(info[k].gen, info[first[s]].gen)))
                first[s] = (uint8_t)k;
    }
    sv.open = ldr.open = false;
    return rc;
}

bool store_has(int slot)
{
    return slot >= 0 && slot < STORE_SLOTS && first[slot] != NO_SECTOR;
}

uint32_t store_free(void)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < SECTORS; k++)
        n += !live(k);
    return n ? n * PAYLOAD - STORE_NAME : 0;
}

int store_info(int slot, char *name, uint32_t *bytes)
{
    uint8_t h[HEAD + STORE_NAME], last[HEAD], k;

    if (!store_has(slot))
        return STORE_ERR_NONE;
    k = first[slot];
    if (plat_flash_read(k * SECTOR, h, sizeof h) != 0)
        return STORE_ERR_IO;
    for (unsigned i = 0; i < STORE_NAME; i++)
        name[i] = (char)(h[HEAD + i] >= 32 && h[HEAD + i] < 127 ? h[HEAD + i] : 0);
    name[STORE_NAME] = '\0';
    k = find(slot, info[k].gen, info[first[slot]].count - 1);
    if (k == NO_SECTOR || plat_flash_read(k * SECTOR, last, HEAD) != 0)
        return STORE_ERR_IO;
    *bytes = (info[first[slot]].count - 1u) * PAYLOAD + rd16(last + 8) - STORE_NAME;
    return STORE_OK;
}

/* ------------------------------------------------------------- save --- */
static int flush_sector(void)
{
    uint32_t k;

    /* Any sector no live file holds and this save has not written. */
    for (k = 0; k < SECTORS; k++)
        if (!live(k) && !sv.wrote[k])
            break;
    if (k == SECTORS)
        return STORE_ERR_FULL;
    buf[0] = 'M', buf[1] = 'D', buf[2] = 'W', buf[3] = '1';
    buf[4] = (uint8_t)sv.slot;
    buf[5] = sv.seq;
    wr16(buf + 6, sv.gen);
    wr16(buf + 8, (uint16_t)sv.fill);
    wr16(buf + 10, crc16(buf + HEAD, sv.fill));
    wr16(buf + 12, 0xFFFF);
    wr16(buf + 14, 0xFFFF);
    for (uint32_t i = HEAD + sv.fill; i < SECTOR; i++)
        buf[i] = 0xFF;
    /* Always erased first: a sector that looks free may be the wreck of
     * a save the power cut. */
    info[k].slot = NO_SECTOR;
    if (plat_flash_erase(k) != 0 || plat_flash_prog(k * SECTOR, buf, HEAD + sv.fill) != 0)
        return STORE_ERR_IO;
    {   /* read back: what is on flash is what was meant */
        uint16_t want = rd16(buf + 10);
        uint32_t n = sv.fill;
        if (plat_flash_read(k * SECTOR, buf, HEAD + n) != 0 || rd16(buf + 8) != n ||
            crc16(buf + HEAD, n) != want)
            return STORE_ERR_IO;
    }
    sv.wrote[k] = 1;
    if (sv.seq == 0)
        sv.first = (uint8_t)k;
    sv.seq++;
    sv.fill = 0;
    return STORE_OK;
}

int store_save_begin(int slot, const char *name)
{
    unsigned i = 0;

    if (slot < 0 || slot >= STORE_SLOTS)
        return STORE_ERR_NONE;
    sv.slot = slot;
    /* A generation nothing on flash carries, not even the wreck of a
     * save that was cut: two sectors answering to the same (slot,
     * generation, seq) would be read back in either order. */
    sv.gen = (uint16_t)(store_has(slot) ? info[first[slot]].gen + 1 : 1);
    for (uint32_t k = 0; k < SECTORS; k++)
        if (info[k].slot == slot && info[k].gen == sv.gen) {
            sv.gen++;
            k = (uint32_t)-1;
        }
    sv.seq = 0;
    sv.first = NO_SECTOR;
    for (uint32_t k = 0; k < SECTORS; k++)
        sv.wrote[k] = 0;
    for (; name && name[i] && i < STORE_NAME; i++)
        buf[HEAD + i] = (uint8_t)name[i];
    for (; i < STORE_NAME; i++)
        buf[HEAD + i] = 0;
    sv.fill = STORE_NAME;
    sv.open = true;
    return STORE_OK;
}

int store_save_put(const char *data, uint32_t n)
{
    if (!sv.open)
        return STORE_ERR_NONE;
    while (n) {
        uint32_t room = PAYLOAD - sv.fill, take = n < room ? n : room;
        int rc;
        for (uint32_t i = 0; i < take; i++)
            buf[HEAD + sv.fill + i] = (uint8_t)data[i];
        sv.fill += take;
        data += take;
        n -= take;
        if (sv.fill == PAYLOAD && (rc = flush_sector()) != STORE_OK) {
            sv.open = false;
            return rc;
        }
    }
    return STORE_OK;
}

int store_save_commit(void)
{
    uint8_t c[4];
    int rc;

    if (!sv.open)
        return STORE_ERR_NONE;
    sv.open = false;
    if ((sv.fill || sv.seq == 0) && (rc = flush_sector()) != STORE_OK)
        return rc;
    /* The commit. Before these four bytes the old file is the file. */
    wr16(c, sv.seq);
    wr16(c + 2, (uint16_t)~sv.seq);
    if (plat_flash_prog(sv.first * SECTOR + 12, c, 4) != 0)
        return STORE_ERR_IO;
    return store_mount();
}

/* ------------------------------------------------------------- load --- */
static int load_sector(void)
{
    uint8_t k = find(ldr.slot, ldr.gen, ldr.seq);
    if (k == NO_SECTOR)
        return STORE_ERR_CORRUPT;
    if (plat_flash_read(k * SECTOR, buf, SECTOR) != 0)
        return STORE_ERR_IO;
    ldr.len = rd16(buf + 8);
    if (ldr.len > PAYLOAD || crc16(buf + HEAD, ldr.len) != rd16(buf + 10))
        return STORE_ERR_CORRUPT;
    ldr.at = ldr.seq == 0 ? STORE_NAME : 0;
    ldr.seq++;
    return STORE_OK;
}

int store_load_begin(int slot)
{
    if (!store_has(slot))
        return STORE_ERR_NONE;
    ldr.slot = slot;
    ldr.gen = info[first[slot]].gen;
    ldr.count = (uint8_t)info[first[slot]].count;
    ldr.seq = 0;
    ldr.at = ldr.len = 0;
    ldr.open = true;
    sv.open = false; /* the two share the sector buffer */
    return STORE_OK;
}

int32_t store_load_get(char *out, uint32_t cap)
{
    uint32_t n = 0;

    if (!ldr.open)
        return STORE_ERR_NONE;
    while (n < cap) {
        if (ldr.at >= ldr.len) {
            int rc;
            if (ldr.seq >= ldr.count)
                break;
            if ((rc = load_sector()) != STORE_OK) {
                ldr.open = false;
                return rc;
            }
            continue;
        }
        out[n++] = (char)buf[HEAD + ldr.at++];
    }
    return (int32_t)n;
}

int store_delete(int slot)
{
    uint8_t f;

    if (!store_has(slot))
        return STORE_ERR_NONE;
    /* The live file's first sector first: once it is gone the file is
     * gone, whatever happens next. Then EVERY sector of the slot -- an
     * older generation left whole on flash would come back as the file. */
    f = first[slot];
    if (plat_flash_erase(f) != 0)
        return STORE_ERR_IO;
    info[f].slot = NO_SECTOR;
    for (uint32_t k = 0; k < SECTORS; k++)
        if (info[k].slot == slot && plat_flash_erase(k) != 0)
            return STORE_ERR_IO;
    return store_mount();
}

int store_map(int slot, uint8_t *sectors, int max)
{
    int n;

    if (!store_has(slot))
        return STORE_ERR_NONE;
    n = info[first[slot]].count;
    if (n > max)
        return STORE_ERR_FULL;
    for (int q = 0; q < n; q++)
        if ((sectors[q] = find(slot, info[first[slot]].gen, q)) == NO_SECTOR)
            return STORE_ERR_CORRUPT;
    return n;
}

/* ----------------------------------------------------------- shrink --- */
/* Where every sector of the file lies, read before the first erase: the
 * new generation goes into those same sectors, seq for seq, and each one
 * is read before it is written over. What the new file takes from the old
 * never sits below where it lands, so by the time sector `seq` is erased
 * nothing still to be read is in it (store.h). */
static struct {
    int slot;
    uint16_t gen, old;
    uint8_t seq, homes, home[SECTORS];
    uint32_t fill, out; /* out: bytes of the new file given so far */
    bool open;
} sh;

/* The sector of the file as it is that holds its byte `at`, and where in
 * that sector's payload it is. NO_SECTOR past the file's end, or where
 * the new file has already caught up with it. */
static uint8_t source(uint32_t at, uint32_t *off)
{
    uint32_t p = at + STORE_NAME, s = p / PAYLOAD;

    if (at < sh.out || s >= sh.homes)
        return NO_SECTOR;
    *off = p - s * PAYLOAD;
    return sh.home[s];
}

static int shrink_flush(void)
{
    uint8_t k;

    if (sh.seq >= sh.homes)
        return STORE_ERR_FULL; /* not smaller after all */
    k = sh.home[sh.seq];
    buf[0] = 'M', buf[1] = 'D', buf[2] = 'W', buf[3] = '1';
    buf[4] = (uint8_t)sh.slot;
    buf[5] = sh.seq;
    wr16(buf + 6, sh.gen);
    wr16(buf + 8, (uint16_t)sh.fill);
    wr16(buf + 10, crc16(buf + HEAD, sh.fill));
    wr16(buf + 12, 0xFFFF);
    wr16(buf + 14, 0xFFFF);
    for (uint32_t i = HEAD + sh.fill; i < SECTOR; i++)
        buf[i] = 0xFF;
    info[k].slot = NO_SECTOR;
    if (plat_flash_erase(k) != 0 || plat_flash_prog(k * SECTOR, buf, HEAD + sh.fill) != 0)
        return STORE_ERR_IO;
    {   /* read back: what is on flash is what was meant, as a save does */
        uint16_t want = rd16(buf + 10);
        uint32_t n = sh.fill;
        if (plat_flash_read(k * SECTOR, buf, HEAD + n) != 0 || rd16(buf + 8) != n ||
            crc16(buf + HEAD, n) != want)
            return STORE_ERR_IO;
    }
    sh.seq++;
    sh.fill = 0;
    return STORE_OK;
}

int store_shrink_begin(int slot)
{
    uint8_t f;

    if (!store_has(slot))
        return STORE_ERR_NONE;
    f = first[slot];
    sh.slot = slot;
    sh.old = info[f].gen;
    sh.homes = (uint8_t)info[f].count;
    for (int q = 0; q < sh.homes; q++)
        if ((sh.home[q] = find(slot, sh.old, q)) == NO_SECTOR)
            return STORE_ERR_CORRUPT;
    /* A generation nothing on flash carries, as a save picks one. */
    sh.gen = (uint16_t)(sh.old + 1);
    for (uint32_t k = 0; k < SECTORS; k++)
        if (info[k].slot == slot && info[k].gen == sh.gen) {
            sh.gen++;
            k = (uint32_t)-1;
        }
    /* The name, before any erase: the file keeps the one it has. */
    if (plat_flash_read(sh.home[0] * SECTOR + HEAD, buf + HEAD, STORE_NAME) != 0)
        return STORE_ERR_IO;
    /* Any other generation of the slot goes first: left whole on flash,
     * it would come back as the file the moment this one's first sector
     * is erased -- and its blocks would be read where the new file has
     * written. store_delete clears a slot for the same reason. */
    for (uint32_t k = 0; k < SECTORS; k++)
        if (info[k].slot == slot && info[k].gen != sh.old) {
            if (plat_flash_erase(k) != 0)
                return STORE_ERR_IO;
            info[k].slot = NO_SECTOR;
        }
    sh.seq = 0;
    sh.fill = STORE_NAME;
    sh.out = 0;
    sh.open = true;
    sv.open = ldr.open = false; /* the three share the sector buffer */
    return STORE_OK;
}

int store_shrink_put(const char *data, uint32_t n)
{
    if (!sh.open)
        return STORE_ERR_NONE;
    while (n) {
        uint32_t room = PAYLOAD - sh.fill, take = n < room ? n : room;
        int rc;
        for (uint32_t i = 0; i < take; i++)
            buf[HEAD + sh.fill + i] = (uint8_t)data[i];
        sh.fill += take;
        sh.out += take;
        data += take;
        n -= take;
        if (sh.fill == PAYLOAD && (rc = shrink_flush()) != STORE_OK) {
            sh.open = false;
            return rc;
        }
    }
    return STORE_OK;
}

int store_shrink_read(uint32_t at, char *out, uint32_t n)
{
    if (!sh.open)
        return STORE_ERR_NONE;
    while (n) {
        uint32_t off, take;
        uint8_t k = source(at, &off);
        if (k == NO_SECTOR)
            return STORE_ERR_NONE;
        take = PAYLOAD - off < n ? PAYLOAD - off : n;
        if (plat_flash_read(k * SECTOR + HEAD + off, (uint8_t *)out, take) != 0)
            return STORE_ERR_IO;
        at += take, out += take, n -= take;
    }
    return STORE_OK;
}

int store_shrink_copy(uint32_t at, uint32_t len)
{
    if (!sh.open)
        return STORE_ERR_NONE;
    while (len) {
        uint32_t off, room = PAYLOAD - sh.fill, take;
        uint8_t k = source(at, &off);
        int rc;
        if (k == NO_SECTOR) { /* past the file, or over what is written */
            sh.open = false;
            return STORE_ERR_NONE;
        }
        take = len < room ? len : room;
        if (take > PAYLOAD - off)
            take = PAYLOAD - off;
        if (plat_flash_read(k * SECTOR + HEAD + off, buf + HEAD + sh.fill, take) != 0)
            return STORE_ERR_IO;
        sh.fill += take;
        sh.out += take;
        at += take;
        len -= take;
        if (sh.fill == PAYLOAD && (rc = shrink_flush()) != STORE_OK) {
            sh.open = false;
            return rc;
        }
    }
    return STORE_OK;
}

int store_shrink_commit(void)
{
    uint8_t c[4];
    int rc;

    if (!sh.open)
        return STORE_ERR_NONE;
    sh.open = false;
    if ((sh.fill || sh.seq == 0) && (rc = shrink_flush()) != STORE_OK)
        return rc;
    /* The commit, a save's four bytes: before them nothing of the slot
     * mounts, after them the smaller file is the file. The sectors the
     * old generation had past this one answer to a generation whose
     * first sector is gone -- mount counts them free. */
    wr16(c, sh.seq);
    wr16(c + 2, (uint16_t)~sh.seq);
    if (plat_flash_prog(sh.home[0] * SECTOR + 12, c, 4) != 0)
        return STORE_ERR_IO;
    return store_mount();
}
