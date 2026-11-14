#include "meta.h"
#include <cddb/cddb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VERSION
#define VERSION "0.1.0"
#endif

static char *dup(const char *s) { return s && *s ? strdup(s) : NULL; }

int sr_meta_lookup(CdIo_t *cdio, const sr_meta_opts_t *o, sr_meta_t *m)
{
    int rc = -1;
    memset(m, 0, sizeof *m);

    track_t first = cdio_get_first_track_num(cdio);
    track_t n     = cdio_get_num_tracks(cdio);
    if (n == 0) return -1;

    cddb_conn_t *c = cddb_new();
    cddb_disc_t *d = cddb_disc_new();
    if (!c || !d) goto out;

    cddb_set_server_name(c, o->server ? o->server : "gnudb.gnudb.org");
    if (o->http) {
        cddb_http_enable(c);
        cddb_set_server_port(c, o->port ? o->port : 80);
        cddb_set_http_path_query(c, "/~cddb/cddb.cgi");
        cddb_set_http_path_submit(c, "/~cddb/submit.cgi");
    } else {
        cddb_set_server_port(c, o->port ? o->port : 8880);
    }
    cddb_set_timeout(c, 15);
    cddb_set_charset(c, "UTF-8");
    /* gnudb wants a valid-looking hello; identify the program honestly. */
    cddb_set_email_address(c, "sonora-rip@localhost");
    cddb_set_client(c, "sonora-rip", VERSION);
    /* local cache (~/.cddb) is on by default: repeated rips stay offline. */

    for (int i = 0; i < n; i++) {
        cddb_track_t *t = cddb_track_new();
        if (!t) goto out;
        /* CDDB frame offset = LBA incl. the 150-frame lead-in = libcdio's LBA */
        cddb_track_set_frame_offset(t, cdio_get_track_lba(cdio, first + i));
        cddb_disc_add_track(d, t);
    }
    lba_t leadout = cdio_get_track_lba(cdio, CDIO_CDROM_LEADOUT_TRACK);
    cddb_disc_set_length(d, leadout / CDIO_CD_FRAMES_PER_SEC);
    if (!cddb_disc_calc_discid(d)) goto out;
    m->discid = cddb_disc_get_discid(d);

    int found = cddb_query(c, d);
    if (found < 0) { fprintf(stderr, "CDDB query failed: %s\n", cddb_error_str(cddb_errno(c))); goto out; }
    if (found == 0) { rc = 1; goto out; }
    for (int skip = (o->match > 1 ? o->match - 1 : 0); skip > 0 && found > 1; skip--)
        if (!cddb_query_next(c, d)) break;

    if (!cddb_read(c, d)) { fprintf(stderr, "CDDB read failed: %s\n", cddb_error_str(cddb_errno(c))); goto out; }

    m->artist = dup(cddb_disc_get_artist(d));
    m->album  = dup(cddb_disc_get_title(d));
    m->genre  = dup(cddb_disc_get_genre(d));
    m->year   = cddb_disc_get_year(d);
    m->ntracks = n;
    m->title        = calloc(n, sizeof *m->title);
    m->track_artist = calloc(n, sizeof *m->track_artist);
    if (!m->title || !m->track_artist) goto out;
    for (int i = 0; i < n; i++) {
        cddb_track_t *t = cddb_disc_get_track(d, i);
        if (!t) continue;
        m->title[i]        = dup(cddb_track_get_title(t));
        m->track_artist[i] = dup(cddb_track_get_artist(t));
    }
    m->valid = 1;
    rc = 0;
out:
    if (rc != 0) sr_meta_free(m);
    if (d) cddb_disc_destroy(d);   /* also frees the tracks */
    if (c) cddb_destroy(c);
    return rc;
}

void sr_meta_free(sr_meta_t *m)
{
    for (int i = 0; i < m->ntracks; i++) {
        if (m->title) free(m->title[i]);
        if (m->track_artist) free(m->track_artist[i]);
    }
    free(m->title); free(m->track_artist);
    free(m->artist); free(m->album); free(m->genre);
    memset(m, 0, sizeof *m);
}
