/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CD metadata lookup through CDDB (libcddb) - default server gnudb.org */
#ifndef SONORA_META_H
#define SONORA_META_H
#include <cdio/cdio.h>
#include <stdint.h>

typedef struct {
    const char *server;   /* NULL = gnudb.gnudb.org */
    int         port;     /* 0 = 8880 (cddbp) or 80 (http) */
    int         http;     /* 1 = HTTP transport (works through most firewalls) */
    int         match;    /* which candidate when several (1-based, 0/1 = first) */
} sr_meta_opts_t;

typedef struct {
    int       valid;
    uint32_t  discid;
    char     *artist, *album, *genre;
    unsigned  year;
    int       ntracks;
    char    **title;       /* [ntracks], index = track position (0-based) */
    char    **track_artist;/* [ntracks], may contain NULLs */
} sr_meta_t;

/* returns 0 = found, 1 = no match, -1 = error (m->valid is 0 unless found) */
int  sr_meta_lookup(CdIo_t *cdio, const sr_meta_opts_t *o, sr_meta_t *m);
void sr_meta_free(sr_meta_t *m);
#endif
