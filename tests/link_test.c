/* SPDX-License-Identifier: GPL-3.0-only */
/* The link's receiver, message by message, on a flash in memory: what it
 * accepts, what it refuses, and that a transfer that fails leaves the
 * flash as it was. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/link.h"
#include "../core/bank.h"
#include "ramflash.h"

Song app_song;
char app_name[STORE_NAME + 1] = "TEST";
int app_slot = -1;
bool app_dirty;
void player_stop(void) {} /* a bank coming in stops the song: there is none here */

/* host -> device: one message waiting; device -> host: the last answer */
static uint8_t to_dev[700], from_dev[200];
static uint32_t to_len, from_len;
int plat_sysex_get(const uint8_t **p, uint32_t *n) { *p = to_dev; *n = to_len; return to_len != 0; }
void plat_sysex_done(void) { to_len = 0; }
int plat_sysex_send(const uint8_t *p, uint32_t n) { memcpy(from_dev, p, n); from_len = n; return 0; }

static int fails, state;
static uint32_t ms;
static bool at_menu = true;
static struct { int cmd, status, seq; uint32_t extra, n; uint8_t tail[LINK_READ_MAX]; } ans;

static int say(int cmd, const uint8_t *raw, uint32_t n)
{
    uint8_t a[8 + LINK_READ_MAX];
    uint32_t d;
    to_dev[0] = 0x7D, to_dev[1] = 0x4D, to_dev[2] = 0x44, to_dev[3] = (uint8_t)cmd;
    to_len = 4 + link_pack7(raw, n, to_dev + 4);
    from_len = 0;
    state = link_frame(ms += 16, at_menu);
    if (from_len < 5 || (d = link_unpack7(from_dev + 4, from_len - 4, a, sizeof a)) < 8 || d > sizeof a)
        return -1;
    ans.n = d - 8;
    memcpy(ans.tail, a + 8, ans.n);
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

    /* the other way: the device's songs, read back piece by piece */
    {
        static uint8_t back[2000];
        uint8_t ask[5] = { 0 }, first[LINK_READ_MAX];
        uint32_t at = 0, bytes;
        int b_slot = -1;

        for (int s = 0; s < APP_SONG_SLOTS; s++) {
            uint8_t q = (uint8_t)s;
            if (say(LINK_CMD_INFO, &q, 1) == LINK_OK && ans.n == 1 && ans.tail[0] == 'B')
                b_slot = s, bytes = ans.extra;
        }
        CHECK("INFO names what a slot holds", b_slot >= 0 && ans.seq == APP_SONG_SLOTS);
        ask[0] = APP_SONG_SLOTS - 1;
        CHECK("and says when it holds nothing", say(LINK_CMD_INFO, ask, 1) == LINK_NONE);
        ask[0] = (uint8_t)b_slot;
        at_menu = false;
        CHECK("away from the MENU nothing is read out", say(LINK_CMD_READ, ask, 5) == LINK_BUSY);
        at_menu = true;
        CHECK("the first piece", say(LINK_CMD_READ, ask, 5) == LINK_OK && ans.n == LINK_READ_MAX &&
                                 state == LINK_SENDING);
        memcpy(first, ans.tail, ans.n);
        CHECK("asked again, it is the same piece", say(LINK_CMD_READ, ask, 5) == LINK_OK &&
                                                   !memcmp(first, ans.tail, LINK_READ_MAX));
        for (;;) {
            for (int i = 0; i < 4; i++)
                ask[1 + i] = (uint8_t)(at >> 8 * i);
            if (say(LINK_CMD_READ, ask, 5) != LINK_OK || ans.extra != at || !ans.n)
                break;
            memcpy(back + at, ans.tail, ans.n);
            at += ans.n;
        }
        CHECK("piece by piece the song comes back whole, and the end is said",
              ans.status == LINK_OK && at == sizeof one && !memcmp(back, one, sizeof one) && state == LINK_IDLE);
        ask[1] = 240, ask[2] = 0, ask[3] = 0, ask[4] = 0;
        CHECK("a piece from the middle can be asked for on its own",
              say(LINK_CMD_READ, ask, 5) == LINK_OK && !memcmp(ans.tail, one + 240, LINK_READ_MAX));
        (void)bytes;
    }

    /* the sample bank (version 5): BEGIN's bit 1, slot 32 */
    {
        static uint8_t big[300000];
        char other[STORE_NAME + 1];
        uint32_t bytes, room;
        uint8_t q = BANK_SLOT;

        for (uint32_t i = 0; i < sizeof big; i++)
            big[i] = (uint8_t)(i * 13);
        at_menu = false;
        CHECK("away from the MENU the bank is refused too", begin("x", one, sizeof one, 2) == LINK_BUSY);
        at_menu = true;
        CHECK("the bank goes through", send("whatever", one, sizeof one, 2) == LINK_OK &&
                                           link_slot() == BANK_SLOT);
        CHECK("into slot 32, under the bank's own name",
              store_info(BANK_SLOT, other, &bytes) == STORE_OK && !strcmp(other, BANK_FILE) && bytes == sizeof one);
        CHECK("INFO and READ reach it", say(LINK_CMD_INFO, &q, 1) == LINK_OK && ans.extra == sizeof one);
        CHECK("no song took the slot", !holds(BANK_FILE, one, sizeof one));
        CHECK("a bank replaces the bank", send("x", big, 100000, 2) == LINK_OK);
        room = store_free() + 40000; /* more than is free, less than with the old one's room */
        CHECK("a bank that does not fit beside the old one takes its place",
              room < sizeof big && send("x", big, room, 2) == LINK_OK &&
                  store_info(BANK_SLOT, other, &bytes) == STORE_OK && bytes == room);
        CHECK("more than the whole flash is still refused", begin_as("x", 400000, 0, 2) == LINK_FULL);
    }

    printf("link test: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
