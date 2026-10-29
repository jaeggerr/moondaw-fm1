/* SPDX-License-Identifier: GPL-3.0-only */
/* The voice, the device's way: a song reduced as tools/fm1_send.py
 * reduces it, written to flash, opened by the app (the store read a
 * piece at a time), played with every track muted but the VOX ones,
 * then saved. Its syllables must all arrive, be sung, and be written
 * back out.
 *
 *   vox_test SONG.trk...      exit 1 if any fails */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../core/player.h"
#include "../core/trk.h"

static uint8_t nor[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];
int plat_flash_read(uint32_t o, void *d, uint32_t n) { memcpy(d, nor + o, n); return 0; }
int plat_flash_erase(uint32_t s) { memset(nor + s * PLAT_FLASH_SECTOR, 0xFF, PLAT_FLASH_SECTOR); return 0; }
int plat_flash_prog(uint32_t o, const void *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        nor[o + i] &= ((const uint8_t *)d)[i];
    return 0;
}
void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_sysex_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
void plat_sysex_done(void) {}
int plat_sysex_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }

static Song reduced;
static char src[1 << 20], text[1 << 18], back[1 << 18];

static int syllables(const char *t)
{
    int n = 0;
    for (const char *p = t; *p; p++)
        n += *p == '\'' && (p[-1] == ' ' || p[-1] == '\t');
    return n;
}

static int read_back(char *out, uint32_t cap)
{
    uint32_t n = 0;
    int32_t got;
    if (store_load_begin(app_slot) != STORE_OK)
        return -1;
    while ((got = store_load_get(out + n, cap - 1 - n)) > 0)
        n += (uint32_t)got;
    out[n] = '\0';
    return got < 0 ? -1 : (int)n;
}

int main(int argc, char **argv)
{
    int bad = 0;

    for (int k = 1; k < argc; k++) {
        FILE *f = fopen(argv[k], "rb");
        const char *name = strrchr(argv[k], '/');
        size_t n = f ? fread(src, 1, sizeof src - 1, f) : 0;
        int32_t len;
        uint8_t vox_tracks = 0;
        double sum = 0;
        long frames = 0, seconds = 90;
        int want, arrived, saved;

        name = name ? name + 1 : argv[k];
        if (f)
            fclose(f);
        src[n] = '\0';
        if (!n || trk_load(&reduced, src, (uint32_t)n, NULL) != TRK_OK ||
            (len = trk_save(&reduced, text, sizeof text - 1)) < 0) {
            printf("%-14s cannot read\n", name);
            bad++;
            continue;
        }
        text[len] = '\0';
        want = syllables(src);

        memset(nor, 0xFF, sizeof nor);
        store_mount();
        if (store_save_begin(0, "VOX") || store_save_put(text, (uint32_t)len) || store_save_commit()) {
            printf("%-14s flash refused it\n", name);
            bad++;
            continue;
        }
        app_init(); /* boots on what the flash holds */
        arrived = 0;
        for (int t = 0; t < SONG_MAX_TRACKS; t++)
            for (int p = 0; p < TRACK_MAX_PATTERNS; p++)
                for (int r = 0; r < app_song.patterns[t][p].length; r++) {
                    const Step *st = song_step(&app_song, t, p, r);
                    for (int i = 0; i < STEP_MAX_FX; i++)
                        arrived += st->fx[i].cmd == FX_SYL && song_syl(&app_song, st->fx[i].val)[0];
                    if (st->instrument != INSTR_NONE &&
                        app_song.instruments[st->instrument].engine == ENGINE_VOX)
                        vox_tracks |= (uint8_t)(1u << t);
                }

        for (int t = 0; t < SONG_MAX_TRACKS; t++)
            if (!((vox_tracks >> t) & 1u))
                player_toggle_mute(t);
        player_play_song(0, true);
        while (frames < seconds * PLAT_FS) {
            static int16_t pcm[256 * 2];
            app_audio(pcm, 256);
            for (int i = 0; i < 256; i++)
                sum += (double)pcm[2 * i] * pcm[2 * i] + (double)pcm[2 * i + 1] * pcm[2 * i + 1];
            frames += 256;
            if (!player_playing() && frames > PLAT_FS)
                break;
        }
        player_stop();
        for (int t = 0; t < SONG_MAX_TRACKS; t++)
            if (player_muted(t))
                player_toggle_mute(t);

        app_mark_dirty();
        saved = app_save() == STORE_OK && read_back(back, sizeof back) >= 0 ? syllables(back) : -1;

        {
            double db = 10 * log10(sum / (2.0 * frames) / (32768.0 * 32768.0) + 1e-20);
            bool ok = want > 0 && arrived == want && saved == want && vox_tracks && db > -50;
            printf("%-14s %s  %3d syllables, %d arrived, %d saved; voice tracks 0x%02X at %.1f dBFS over %.0f s\n",
                   name, ok ? "ok  " : "FAIL", want, arrived, saved, vox_tracks, db, (double)frames / PLAT_FS);
            bad += !ok;
        }
    }
    return bad ? 1 : 0;
}
