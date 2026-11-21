/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/util.h"
#include <stdio.h>
#include <string.h>
static int fails;
static void chk(const char *in, size_t n, const char *want)
{
    char out[256]; sr_sanitize_filename(out, n, in);
    if (strcmp(out, want)) { printf("FAIL: '%s' -> '%s' (want '%s')\n", in, out, want); fails++; }
}
int main(void)
{
    chk("AC/DC: Back In Black?", 256, "AC_DC_ Back In Black_");
    chk("  ..hidden. ", 256, "hidden");
    chk("", 256, "_");
    chk("\x01\x02", 256, "__");
    chk("Zażółć", 256, "Zażółć");
    chk("Zażółć", 5, "Za");            /* 'ż' is 2 bytes: cut must not split it */
    chk("Zażółć", 6, "Zaż");
    char b[256];
    sr_track_basename(b, sizeof b, 1, "Back In Black");
    if (strcmp(b, "01-Back In Black")) { printf("FAIL basename: %s\n", b); fails++; }
    sr_track_basename(b, sizeof b, 12, "AC/DC");
    if (strcmp(b, "12-AC_DC")) { printf("FAIL basename: %s\n", b); fails++; }
    sr_track_basename(b, sizeof b, 3, NULL);
    if (strcmp(b, "03-Track")) { printf("FAIL basename: %s\n", b); fails++; }
    if (sr_album_dirname(b, sizeof b, "AC/DC", "Back In Black") || strcmp(b, "AC_DC - Back In Black")) { printf("FAIL dir: %s\n", b); fails++; }
    if (sr_album_dirname(b, sizeof b, NULL, "Album") || strcmp(b, "Album")) { printf("FAIL dir2: %s\n", b); fails++; }
    if (sr_album_dirname(b, sizeof b, "", NULL) != -1) { puts("FAIL dir3"); fails++; }
    if (sr_mkdir_p("/tmp/sr_test/a/b/c") || sr_mkdir_p("/tmp/sr_test/a/b/c")) { puts("FAIL mkdir_p"); fails++; }
    signed char m[100];
    memset(m, -1, sizeof m);
    if (sr_parse_track_list("1,3,5-7", m, 100, 1) || m[1] != 1 || m[2] != -1 || m[3] != 1 || m[5] != 1 || m[7] != 1 || m[8] != -1) { puts("FAIL tracklist"); fails++; }
    if (sr_parse_track_list("all", m, 100, 0) || m[2] != 0 || m[99] != 0 || m[0] != -1) { puts("FAIL tracklist all"); fails++; }
    const char *bad[] = { "", "0", "x", "3-", "5-3", "1,,2", "1,", "100", "1;2", NULL };
    for (int i = 0; bad[i]; i++)
        if (sr_parse_track_list(bad[i], m, 100, 1) == 0) { printf("FAIL: accepted bad list '%s'\n", bad[i]); fails++; }
    puts(fails ? "RESULT: FAIL" : "RESULT: PASS");
    return fails;
}
