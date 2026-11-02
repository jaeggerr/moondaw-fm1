/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's own voice, for the editor page: core/player.c and the engines
 * behind it, compiled to WebAssembly, so what the browser plays is what the
 * device plays and not a drawing of it.
 *
 * It is NOT the firmware: platform/web/ already runs the whole machine with
 * its panel, and that is the wrong instrument here -- auditioning one zone of
 * one instrument means playing a chosen note on a chosen instrument, which on
 * the panel would mean steering the screens. So this links the core alone and
 * exports the three things the page needs: a song, a bank, and notes.
 *
 * The bank: core/bank.c reads the index through the store's sector map, over
 * flash this has none of, so the small part of it the page needs is here
 * instead -- bank_find's rules (the file's last part, no .wav, any case) word
 * for word, since a name that resolves here and not on the device would make
 * the audition a lie.
 *
 * Freestanding: no libc. Built by build.sh with zig cc or emcc. */
#include "../../core/player.h"
#include "../../core/sampler.h"
#include "../../core/song.h"
#include "../../core/trk.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT __attribute__((visibility("default")))
/* The compiler turns loops into these whether or not there is a libc. */
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
#endif

/* core/bank.h, repeated rather than included: bank.h pulls in the store and
 * the store pulls in a platform. */
#define BANK_MAX      128
#define BANK_NAME_LEN 24
#define BANK_ENTRY    (BANK_NAME_LEN + 8)
#define BANK_HEAD     8
#define STORE_NAME    12
#define PAYLOAD       (SMP_BLOCK * 16u) /* 4080, as core/sampler.c's */

#define RENDER_MAX 1024

static Song song;
static char song_buf[1 << 18];
static char syl_buf[8];
static int16_t pcm[RENDER_MAX * 2];
static bool have_song;

/* The bank as the store holds it: STORE_NAME bytes of name, then the file.
 * 160 KB is what the FM-1's 73 sectors can carry; the page never sends more. */
static uint8_t stream[STORE_NAME + 73u * PAYLOAD];
static const uint8_t *payloads[SMP_MAX_SECTORS];
static uint32_t bank_len; /* the file's length, without the name */
static int bank_n;

static uint8_t *bank_at(uint32_t off) { return stream + STORE_NAME + off; }

static uint32_t rd32(uint32_t off)
{
    const uint8_t *p = bank_at(off);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t entry_frames(int i) { return rd32(BANK_HEAD + (uint32_t)i * BANK_ENTRY + BANK_NAME_LEN); }
static uint32_t entry_at(int i) { return rd32(BANK_HEAD + (uint32_t)i * BANK_ENTRY + BANK_NAME_LEN + 4); }

/* core/bank.c entry_ok, with `size` the file's length. */
static bool entry_ok(int i)
{
    uint32_t e = BANK_HEAD + (uint32_t)i * BANK_ENTRY;
    uint32_t frames = entry_frames(i), at = entry_at(i);
    uint32_t blocks = (frames + SMP_BLOCK_FRAMES - 1) / SMP_BLOCK_FRAMES;

    return *bank_at(e) && frames && frames <= 4 * SMP_RATE &&
           (at + STORE_NAME) % SMP_BLOCK == 0 && at <= bank_len &&
           blocks * SMP_BLOCK <= bank_len - at;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* core/bank.c bank_find: a song names a sample by its file, the bank answers
 * to the last part of it without .wav, whatever the case. */
static int bank_find(const char *nm)
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
    for (int i = 0; i < bank_n; i++) {
        const uint8_t *e = bank_at(BANK_HEAD + (uint32_t)i * BANK_ENTRY);
        int k = 0;
        while (k < len && lower((char)e[k]) == lower(key[k]))
            k++;
        if (k == len && e[k] == 0)
            return i;
    }
    return -1;
}

/* core/bank.c bank_resolve: which of the song's names the bank has. */
static void resolve(void)
{
    for (int i = 0; i < SONG_SMP_MAX; i++) {
        int b = song.smp[i].name[0] ? bank_find(song.smp[i].name) : -1;
        if (b < 0)
            smp_map(i, 0, 0);
        else
            smp_map(i, entry_at(b) + STORE_NAME, entry_frames(b));
    }
}

/* ------------------------------------------------------------- the bank --- */

EXPORT uint8_t *aud_bank_buf(void) { return stream + STORE_NAME; }
EXPORT uint32_t aud_bank_cap(void) { return sizeof(stream) - STORE_NAME; }

/* The page writes the bank file into aud_bank_buf() and calls this with its
 * length. -> the entries that read whole, as bank_mount counts them. */
EXPORT int aud_bank_mount(uint32_t len)
{
    smp_hold();
    bank_n = 0;
    bank_len = len > aud_bank_cap() ? 0 : len;
    if (bank_len < BANK_HEAD)
        return 0;
    const uint8_t *h = bank_at(0);
    if (h[0] != 'S' || h[1] != 'M' || h[2] != 'P' || h[3] != '1' || h[4] > BANK_MAX ||
        BANK_HEAD + (uint32_t)h[4] * BANK_ENTRY > bank_len)
        return 0;
    bank_n = h[4];
    for (int i = 0; i < bank_n; i++)
        if (!entry_ok(i)) {
            bank_n = i; /* what reads whole, up to the first that does not */
            break;
        }
    uint32_t n = (STORE_NAME + bank_len + PAYLOAD - 1) / PAYLOAD;
    if (n > SMP_MAX_SECTORS)
        n = SMP_MAX_SECTORS;
    for (uint32_t s = 0; s < n; s++)
        payloads[s] = stream + s * PAYLOAD;
    smp_attach(payloads, (int)n);
    if (have_song)
        resolve();
    return bank_n;
}

EXPORT int aud_bank_count(void) { return bank_n; }

/* Entry `i`'s name, NUL-terminated inside the bank's own bytes. */
EXPORT const char *aud_bank_name(int i)
{
    return i >= 0 && i < bank_n ? (const char *)bank_at(BANK_HEAD + (uint32_t)i * BANK_ENTRY) : "";
}

EXPORT uint32_t aud_bank_frames(int i) { return i >= 0 && i < bank_n ? entry_frames(i) : 0; }

/* The bank's entry a song name plays, -1 for none. bank.js does the same in
 * JavaScript, to grey out a zone without a round trip through here; the test
 * holds the two against each other over a real bank. */
static char name_buf[SONG_SMP_NAME];
EXPORT char *aud_name_buf(void) { return name_buf; }
EXPORT int aud_bank_find(void)
{
    name_buf[sizeof(name_buf) - 1] = '\0';
    return bank_find(name_buf);
}

/* ------------------------------------------------------------- the song --- */

EXPORT char *aud_song_buf(void) { return song_buf; }
EXPORT uint32_t aud_song_cap(void) { return sizeof(song_buf); }

/* The page writes the .trk into aud_song_buf() and calls this. 1 when it
 * loaded. The player is reset, so this is also how a changed song is heard. */
EXPORT int aud_song_load(uint32_t len)
{
    if (len > sizeof(song_buf) || trk_load(&song, song_buf, len, 0) != TRK_OK)
        return 0;
    have_song = true;
    smp_pool_init();
    player_init(&song);
    resolve();
    return 1;
}

/* ------------------------------------------------------------- the notes --- */

EXPORT void aud_note_on(int instr, int note) { player_note_on(instr, (uint8_t)note); }

/* A VOX instrument singing: the page writes up to 5 characters here first. */
EXPORT char *aud_syl_buf(void) { return syl_buf; }
EXPORT void aud_note_sing(int instr, int note)
{
    syl_buf[sizeof(syl_buf) - 1] = '\0';
    player_note_sing(instr, (uint8_t)note, syl_buf);
}

EXPORT void aud_note_off(void) { player_note_off(); }
EXPORT void aud_play_song(int entry) { player_play_song(entry, false); }
/* The arrangement once, then it stops: how web/export/ knows a render is over
 * and how long the file is. The sequencer decides that, never the page -- the
 * length of a phrase is the player's arithmetic (swing, ticks, auto cycles,
 * the references between phrases) and a second copy of it in JavaScript would
 * drift from this one. */
EXPORT void aud_play_once(int entry) { player_play_song(entry, true); }
EXPORT void aud_play_entry(int entry) { player_play_entry(entry); }
EXPORT void aud_stop(void) { player_stop(); }
EXPORT int aud_playing(void) { return player_playing(); }
EXPORT int aud_row(void) { return player_row(); }
EXPORT int aud_entry(void) { return player_entry(); }
EXPORT void aud_master(int pot) { player_set_master(pot); }
EXPORT void aud_mute(int track) { player_toggle_mute(track); }
EXPORT int aud_muted(int track) { return player_muted(track); }
EXPORT uint32_t aud_rate(void) { return 44100; }

/* Interleaved stereo, `frames` of them, at aud_rate(). */
EXPORT const int16_t *aud_render(int frames)
{
    if (frames > RENDER_MAX)
        frames = RENDER_MAX;
    if (!have_song) {
        for (int i = 0; i < frames * 2; i++)
            pcm[i] = 0;
        return pcm;
    }
    player_render(pcm, frames);
    return pcm;
}
