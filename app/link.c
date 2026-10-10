/* SPDX-License-Identifier: GPL-3.0-only */
#include "link.h"

#include "app.h"

enum { TIMEOUT_MS = 5000 };

static int state, slot;
static char name[STORE_NAME + 1];
static uint32_t size, got, crc, want_crc, last_ms;
static uint16_t next_seq;

/* Seven bytes travel as eight: their top bits first, then what is left. */
uint32_t link_pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 7) {
        uint32_t k = n - i < 7 ? n - i : 7;
        uint8_t top = 0;
        for (uint32_t j = 0; j < k; j++)
            top |= (uint8_t)((in[i + j] >> 7) << j);
        out[o++] = top;
        for (uint32_t j = 0; j < k; j++)
            out[o++] = in[i + j] & 0x7F;
    }
    return o;
}

uint32_t link_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 8) {
        uint32_t k = n - i - 1 < 7 ? n - i - 1 : 7;
        for (uint32_t j = 0; j < k && i + 1 + j < n; j++) {
            if (o >= max)
                return max + 1; /* too long: the caller refuses it */
            out[o++] = (uint8_t)(in[i + 1 + j] | ((in[i] >> j) & 1u) << 7);
        }
    }
    return o;
}

uint32_t link_crc32(uint32_t c, const uint8_t *p, uint32_t n)
{
    c = ~c;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void answer(int cmd, int status, uint32_t seq, uint32_t extra)
{
    uint8_t raw[8] = { (uint8_t)cmd, (uint8_t)status, (uint8_t)seq, (uint8_t)(seq >> 8),
                       (uint8_t)extra, (uint8_t)(extra >> 8), (uint8_t)(extra >> 16),
                       (uint8_t)(extra >> 24) };
    uint8_t msg[4 + 10] = { 0x7D, 0x4D, 0x44, LINK_CMD_ANSWER };
    plat_sysex_send(msg, 4 + link_pack7(raw, sizeof raw, msg + 4));
}

/* What a failed transfer wrote is free space again. */
static void drop(void)
{
    if (state == LINK_RECEIVING)
        store_mount();
    state = LINK_IDLE;
}

static bool same_name(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static int begin(const uint8_t *p, uint32_t n)
{
    char other[STORE_NAME + 1];
    uint32_t bytes;
    int free_slot = -1, i = 0;

    if (n < 10 || n > 9 + STORE_NAME)
        return LINK_BADMSG;
    for (; i < (int)n - 9; i++) {
        char c = (char)p[9 + i];
        if (c < ' ' || c > '~' || (i == 0 && c == '.'))
            return LINK_BADMSG; /* a leading dot is the store's own */
        name[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    name[i] = '\0';
    size = rd32(p + 1);
    want_crc = rd32(p + 5);
    slot = -1;
    for (int s = 0; s < APP_SONG_SLOTS; s++) {
        if (store_info(s, other, &bytes) != STORE_OK) {
            if (free_slot < 0)
                free_slot = s;
        } else if (same_name(other, name)) {
            slot = s;
        }
    }
    if (slot >= 0 && !(p[0] & 1u))
        return LINK_EXISTS;
    if (slot >= 0 && slot == app_slot && app_dirty)
        return LINK_DIRTY;
    if (slot < 0)
        slot = free_slot;
    if (slot < 0 || size == 0 || size > store_free())
        return LINK_FULL;
    if (store_save_begin(slot, name) != STORE_OK)
        return LINK_IO;
    got = 0;
    crc = 0;
    next_seq = 0;
    state = LINK_RECEIVING;
    return LINK_OK;
}

static int data(const uint8_t *p, uint32_t n, uint16_t *seq)
{
    int rc;
    if (n < 3 || n > 2 + LINK_DATA_MAX)
        return LINK_BADMSG;
    *seq = (uint16_t)(p[0] | p[1] << 8);
    if (state != LINK_RECEIVING)
        return LINK_BUSY;
    if ((uint16_t)(*seq + 1) == next_seq)
        return LINK_OK; /* the answer was lost and it came again: already written */
    if (*seq != next_seq)
        return LINK_BADSEQ;
    if (got + (n - 2) > size) {
        drop();
        return LINK_BADSUM;
    }
    rc = store_save_put((const char *)p + 2, n - 2);
    if (rc != STORE_OK) {
        drop();
        return rc == STORE_ERR_FULL ? LINK_FULL : LINK_IO;
    }
    crc = link_crc32(crc, p + 2, n - 2);
    got += n - 2;
    next_seq++;
    return LINK_OK;
}

void link_reset(void)
{
    state = LINK_IDLE;
}

int link_frame(uint32_t ms, bool may)
{
    static uint8_t raw[2 + LINK_DATA_MAX + 16];
    const uint8_t *p;
    uint32_t n, d;
    int out;

    if (state == LINK_DONE)
        state = LINK_IDLE;
    if (state == LINK_RECEIVING && ms - last_ms > TIMEOUT_MS)
        drop(); /* the cable was pulled, or the host gave up */
    while (plat_sysex_get(&p, &n)) {
        int cmd = n >= 4 ? p[3] : 0, status = LINK_BADMSG;
        uint16_t seq = 0;
        uint32_t extra = 0;

        d = n >= 4 ? link_unpack7(p + 4, n - 4, raw, sizeof raw) : sizeof raw + 1;
        plat_sysex_done();
        if (d > sizeof raw)
            continue; /* not one of ours, or mangled: the host will ask again */
        last_ms = ms;
        if (cmd == LINK_CMD_PING) {
            status = LINK_OK;
            seq = LINK_VERSION;
            extra = store_free();
        } else if (cmd == LINK_CMD_BEGIN) {
            drop();
            status = may ? begin(raw, d) : LINK_BUSY;
        } else if (cmd == LINK_CMD_DATA) {
            status = data(raw, d, &seq);
        } else if (cmd == LINK_CMD_END) {
            if (state != LINK_RECEIVING) {
                status = LINK_BUSY;
            } else if (got != size || crc != want_crc) {
                drop();
                status = LINK_BADSUM;
            } else if (store_save_commit() != STORE_OK) {
                drop();
                status = LINK_IO;
            } else {
                state = LINK_DONE;
                status = LINK_OK;
            }
            extra = got;
        } else if (cmd == LINK_CMD_CANCEL) {
            drop();
            status = LINK_OK;
        }
        answer(cmd, status, seq, extra);
        if (state == LINK_DONE)
            break; /* the caller sees it before anything else is taken */
    }
    out = state;
    return out;
}

const char *link_name(void) { return name; }
uint32_t link_got(void) { return got; }
uint32_t link_size(void) { return size; }
int link_slot(void) { return slot; }
