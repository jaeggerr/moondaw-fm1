/* SPDX-License-Identifier: GPL-3.0-only */
/* The link's receiver, message by message, on a flash in memory: what it
 * accepts, what it refuses, and that a transfer that fails leaves the
 * flash as it was. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/link.h"
#include "ramflash.h"

Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;

/* host -> device: one message waiting; device -> host: the last answer */
static uint8_t to_dev[700], from_dev[64];
static uint32_t to_len, from_len;
int plat_sysex_get(const uint8_t **p, uint32_t *n) { *p = to_dev; *n = to_len; return to_len != 0; }
void plat_sysex_done(void) { to_len = 0; }
int plat_sysex_send(const uint8_t *p, uint32_t n) { memcpy(from_dev, p, n); from_len = n; return 0; }

static int fails, state;
static uint32_t ms;
static bool at_menu = true;
static struct { int cmd, status, seq; uint32_t extra; } ans;

static int say(int cmd, const uint8_t *raw, uint32_t n)
{
    uint8_t a[8];
    to_dev[0] = 0x7D, to_dev[1] = 0x4D, to_dev[2] = 0x44, to_dev[3] = (uint8_t)cmd;
    to_len = 4 + link_pack7(raw, n, to_dev + 4);
    from_len = 0;
    state = link_frame(ms += 16, at_menu);
    if (from_len < 5 || link_unpack7(from_dev + 4, from_len - 4, a, sizeof a) != 8)
        return -1;
    ans.cmd = a[0], ans.status = a[1], ans.seq = a[2] | a[3] << 8;
    ans.extra = (uint32_t)a[4] | (uint32_t)a[5] << 8 | (uint32_t)a[6] << 16 | (uint32_t)a[7] << 24;
    return ans.status;
}

static int begin_as(const char *name, uint32_t n, uint32_t c, int flags)
{
    uint8_t raw[9 + 32];
    uint32_t l = (uint32_t)strlen(name);
    raw[0] = (uint8_t)flags;
    for (int i = 0; i < 4; i++)
        raw[1 + i] = (uint8_t)(n >> 8 * i), raw[5 + i] = (uint8_t)(c >> 8 * i);
    memcpy(raw + 9, name, l);
    return say(LINK_CMD_BEGIN, raw, 9 + l);
}

static int begin(const char *name, const uint8_t *d, uint32_t n, int flags)
{
    return begin_as(name, n, link_crc32(0, d, n), flags);
}

static int data(int seq, const uint8_t *d, uint32_t n)
{
    uint8_t raw[2 + LINK_DATA_MAX];
    raw[0] = (uint8_t)seq, raw[1] = (uint8_t)(seq >> 8);
    memcpy(raw + 2, d, n);
    return say(LINK_CMD_DATA, raw, 2 + n);
}

/* The whole of it, as the host tool does. Returns the last status. */
static int send(const char *name, const uint8_t *d, uint32_t n, int flags)
{
    int rc = begin(name, d, n, flags), seq = 0;
    for (uint32_t at = 0; rc == LINK_OK && at < n; at += LINK_DATA_MAX)
        rc = data(seq++, d + at, n - at < LINK_DATA_MAX ? n - at : LINK_DATA_MAX);
    return rc == LINK_OK ? say(LINK_CMD_END, 0, 0) : rc;
}

static bool holds(const char *name, const uint8_t *d, uint32_t n)
{
    static char back[70000];
    char other[STORE_NAME + 1];
    uint32_t bytes, got = 0;
    int32_t k;
    for (int s = 0; s < APP_SONG_SLOTS; s++) {
        if (store_info(s, other, &bytes) != STORE_OK || strcmp(other, name))
            continue;
        if (store_load_begin(s) != STORE_OK)
            return false;
        while ((k = store_load_get(back + got, sizeof back - got)) > 0)
            got += (uint32_t)k;
        return k == 0 && got == n && !memcmp(back, d, n);
    }
    return false;
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)

int main(void)
{
    static uint8_t one[1000], two[60000];
    uint8_t dot[] = ".LAST";

    for (uint32_t i = 0; i < sizeof one; i++)
        one[i] = (uint8_t)(i * 7 + 3); /* every byte value, top bits included */
    for (uint32_t i = 0; i < sizeof two; i++)
        two[i] = (uint8_t)("pat t0 00 len 16\n"[i % 17] + (i >> 9));
    ramflash_blank();
    store_mount();
    link_reset();

    CHECK("PING answers with the version and the room left",
          say(LINK_CMD_PING, 0, 0) == LINK_OK && ans.seq == LINK_VERSION && ans.extra == store_free());

    at_menu = false;
    CHECK("away from the MENU a song is refused", begin("KIDS", one, sizeof one, 0) == LINK_BUSY);
    at_menu = true;
    CHECK("a name that starts with a dot is the store's own", begin((char *)dot, one, 10, 0) == LINK_BADMSG);
    CHECK("more than the flash holds is refused before anything is written",
          begin_as("HUGE", 400000, 0, 0) == LINK_FULL && state == LINK_IDLE);

    CHECK("a song goes through", send("kids", one, sizeof one, 0) == LINK_OK && state == LINK_DONE);
    CHECK("it is in the store under its name, in capitals, byte for byte", holds("KIDS", one, sizeof one));
    link_frame(ms += 16, true);
    CHECK("DONE is said once", link_frame(ms += 16, true) == LINK_IDLE);

    CHECK("the same name again is refused", send("KIDS", two, sizeof two, 0) == LINK_EXISTS);
    CHECK("and the first one is untouched", holds("KIDS", one, sizeof one));
    CHECK("unless the host asks to replace it", send("KIDS", two, sizeof two, 1) == LINK_OK);
    CHECK("then the store holds the new one: 60 KB, 250 messages", holds("KIDS", two, sizeof two));

    /* a lost answer, a skipped message */
    begin("B", one, sizeof one, 0);
    data(0, one, 240);
    CHECK("a message sent twice is acknowledged and written once", data(0, one, 240) == LINK_OK);
    CHECK("a message out of turn is refused", data(2, one + 480, 240) == LINK_BADSEQ);
    data(1, one + 240, 240);
    data(2, one + 480, 240);
    data(3, one + 720, 240);
    data(4, one + 960, 40);
    CHECK("and the transfer goes on to its end", say(LINK_CMD_END, 0, 0) == LINK_OK && holds("B", one, sizeof one));

    /* what fails leaves the flash as it was */
    begin("KIDS", one, sizeof one, 1);
    one[5] ^= 1;
    for (int i = 0; i < 5; i++)
        data(i, one + i * 240, i < 4 ? 240 : 40);
    one[5] ^= 1;
    CHECK("a byte changed on the way: END refuses", say(LINK_CMD_END, 0, 0) == LINK_BADSUM);
    CHECK("and the song that was there is still there", holds("KIDS", two, sizeof two));

    begin("KIDS", one, sizeof one, 1);
    data(0, one, 240);
    ms += 6000;
    link_frame(ms, true);
    CHECK("the cable pulled: after five seconds the transfer is dropped", data(1, one + 240, 240) == LINK_BUSY);
    CHECK("and the song that was there is still there", holds("KIDS", two, sizeof two));

    for (int s = 0; s < APP_SONG_SLOTS; s++) {
        char other[STORE_NAME + 1];
        uint32_t b;
        if (store_info(s, other, &b) == STORE_OK && !strcmp(other, "KIDS"))
            app_slot = s;
    }
    app_dirty = true;
    CHECK("the open song has unsaved changes: replacing it is refused",
          begin("KIDS", one, sizeof one, 1) == LINK_DIRTY);
    app_dirty = false;
    CHECK("saved, it may be replaced", send("KIDS", one, sizeof one, 1) == LINK_OK && link_slot() == app_slot);

    printf("link test: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
