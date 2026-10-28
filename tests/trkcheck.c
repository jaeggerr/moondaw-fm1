/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of core/trk.c against real songs: load each file, say what
 * the FM-1 would not carry, write it back, load THAT, and require the
 * second writing to be byte for byte the first. Then every line the
 * FM-1 claims to carry is looked up in the original.
 *
 *   trkcheck FILE.trk...      exit 1 if any file fails */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/trk.h"

static Song a, b;
static char src[1 << 20], out1[1 << 18], out2[1 << 18];

/* Does `line` appear in the original, as a whole cleaned line? Tokens
 * of a step line the FM-1 drops (fSMP) are removed from the original
 * before comparing. Syllables are carried, so they must be there. */
static int in_original(const char *line, const char *text)
{
    char buf[600], want[600];
    snprintf(want, sizeof want, "%s", line);
    for (const char *p = text; *p;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof buf)
            n = sizeof buf - 1;
        memcpy(buf, p, n);
        buf[n] = 0;
        p = nl ? nl + 1 : p + n;
        char *h = strstr(buf, " #");
        if (h)
            *h = 0;
        char clean[600] = "";
        char *sv = NULL;
        for (char *tok = strtok_r(buf, " \t\r", &sv); tok; tok = strtok_r(NULL, " \t\r", &sv)) {
            if (!strncmp(tok, "fSMP", 4))
                continue;
            if (clean[0])
                strcat(clean, " ");
            strcat(clean, tok);
        }
        if (!strcmp(clean, want))
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int bad = 0;
    for (int k = 1; k < argc; k++) {
        FILE *f = fopen(argv[k], "rb");
        if (!f) {
            printf("%s: cannot open\n", argv[k]);
            bad++;
            continue;
        }
        size_t n = fread(src, 1, sizeof src - 1, f);
        fclose(f);
        src[n] = 0;

        TrkReport r, r2;
        int rc = trk_load(&a, src, (uint32_t)n, &r);
        const char *name = strrchr(argv[k], '/');
        name = name ? name + 1 : argv[k];
        if (rc != TRK_OK) {
            printf("%-22s load error %d\n", name, rc);
            bad++;
            continue;
        }
        int32_t n1 = trk_save(&a, out1, sizeof out1 - 1);
        out1[n1 > 0 ? n1 : 0] = 0;
        int rc2 = n1 < 0 ? -9 : trk_load(&b, out1, (uint32_t)n1, &r2);
        int32_t n2 = rc2 ? -1 : trk_save(&b, out2, sizeof out2 - 1);
        int stable = n1 > 0 && n1 == n2 && !memcmp(out1, out2, (size_t)n1);

        /* pattern and arrangement lines must all exist in the original */
        int checked = 0, missing = 0, body = 0;
        char *copy = strdup(out1);
        char *sv = NULL;
        for (char *ln = strtok_r(copy, "\n", &sv); ln; ln = strtok_r(NULL, "\n", &sv)) {
            if (!strncmp(ln, "pat ", 4) || !strncmp(ln, "arr ", 4))
                body = 1;
            if (!body || ln[0] == '#')
                continue;
            checked++;
            if (!in_original(ln, src)) {
                if (missing < 3)
                    printf("    not in original: %s\n", ln);
                missing++;
            }
        }
        free(copy);

        int ok = stable && !missing && !r.steps_lost;
        printf("%-22s %s  %5zu B -> %5d B, %3d/%d chunks, tracks %d"
               " | dropped: smp %d zone %d ref %d syl %d fSMP %d shaping %d"
               " preview %d cutsteps %d lost %d | silent instr %d | %d lines checked\n",
               name, ok ? "ok  " : "FAIL", n, n1, r.chunks_used, POOL_CHUNKS,
               r.tracks_in_file, r.samples, r.zones, r.ref_lines, r.syllables,
               r.smp_fx, r.shaping, r.previews, r.steps_cut, r.steps_lost,
               r.silent_instr, checked);
        bad += !ok;
    }
    printf("sizeof(Song) = %zu\n", sizeof(Song));
    return bad ? 1 : 0;
}
