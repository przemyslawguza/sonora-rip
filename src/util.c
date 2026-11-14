#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

void sr_sanitize_filename(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    if (!n) return;
    for (; *src && o + 1 < n; src++) {
        unsigned char c = (unsigned char)*src;
        if (c < 0x20 || c == 0x7f || strchr("/\\:*?\"<>|", c)) c = '_';
        dst[o++] = (char)c;
    }
    /* do not cut a multi-byte UTF-8 sequence in half */
    if (*src && o) {
        size_t k = o;
        while (k > 0 && ((unsigned char)dst[k - 1] & 0xC0) == 0x80) k--;
        if (k > 0 && (unsigned char)dst[k - 1] >= 0xC0) o = k - 1;
    }
    while (o && (dst[o - 1] == ' ' || dst[o - 1] == '.')) o--;
    size_t s = 0;
    while (s < o && (dst[s] == ' ' || dst[s] == '.')) s++;
    memmove(dst, dst + s, o - s);
    dst[o - s] = 0;
    if (dst[0] == 0 && n > 1) { dst[0] = '_'; dst[1] = 0; }
}

int sr_album_dirname(char *dst, size_t n, const char *artist, const char *album)
{
    char a[256] = "", b[256] = "";
    if (artist && *artist) sr_sanitize_filename(a, sizeof a, artist);
    if (album  && *album)  sr_sanitize_filename(b, sizeof b, album);
    if (*a && *b)  snprintf(dst, n, "%s - %s", a, b);
    else if (*b)   snprintf(dst, n, "%s", b);
    else if (*a)   snprintf(dst, n, "%s", a);
    else { if (n) dst[0] = 0; return -1; }
    return 0;
}

void sr_track_basename(char *dst, size_t n, int track, const char *title)
{
    char t[300] = "";
    if (title && *title) sr_sanitize_filename(t, sizeof t, title);
    snprintf(dst, n, "%02d-%s", track, *t ? t : "Track");
}

int sr_mkdir_p(const char *path)
{
    char tmp[4096];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof tmp) return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
        *p = '/';
    }
    return (mkdir(tmp, 0755) && errno != EEXIST) ? -1 : 0;
}
