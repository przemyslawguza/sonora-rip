/* Audio CD ripping with pre-emphasis detection (libcdio + libcdio-paranoia). */
#include "cdrip.h"
#include "deemph.h"
#include "sink.h"
#include "util.h"
#include <cdio/cdio.h>
#include <cdio/paranoia/cdda.h>
#include <cdio/paranoia/paranoia.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pre-emphasis is the Q-subchannel/TOC control flag of each track. */
static int track_has_preemph(CdIo_t *cdio, track_t t)
{
    return cdio_get_track_preemphasis(cdio, t) == CDIO_TRACK_FLAG_TRUE;
}

static void paranoia_cb(long inpos, paranoia_cb_mode_t fn) { (void)inpos; (void)fn; }

int cdrip_run(const cdrip_opts_t *o)
{
    int rc = 1;
    cdrom_drive_t *drv = NULL;
    cdrom_paranoia_t *par = NULL;
    sr_meta_t meta;
    memset(&meta, 0, sizeof meta);
    CdIo_t *cdio = cdio_open(o->device, DRIVER_UNKNOWN);
    if (!cdio) { fprintf(stderr, "cannot open drive\n"); return 1; }

    drv = cdio_cddap_identify_cdio(cdio, 1, NULL);
    if (!drv || cdio_cddap_open(drv) != 0) {
        fprintf(stderr, "cannot open CD-DA interface\n"); goto out;
    }
    par = cdio_paranoia_init(drv);
    cdio_paranoia_modeset(par, o->paranoia ? PARANOIA_MODE_FULL
                                           : PARANOIA_MODE_DISABLE);

    track_t first = cdio_get_first_track_num(cdio);
    track_t ntr   = cdio_get_num_tracks(cdio);
    int from = o->first ? o->first : first;
    int to   = o->last  ? o->last  : first + ntr - 1;

    if (o->use_cddb) {
        int r = sr_meta_lookup(cdio, &o->cddb, &meta);
        if (r == 0)
            fprintf(stderr, "CDDB: %s - %s (disc id %08x)\n",
                    meta.artist ? meta.artist : "?", meta.album ? meta.album : "?",
                    (unsigned)meta.discid);
        else if (r == 1) fprintf(stderr, "CDDB: no match, using generic names\n");
        else             fprintf(stderr, "CDDB: lookup failed, using generic names\n");
    }

    /* Output goes to <outdir>/<Artist - Album>/ when CDDB gave a name. */
    char outdir[4096];
    snprintf(outdir, sizeof outdir, "%s", o->outdir);
    if (meta.valid) {
        char album_dir[600];
        if (sr_album_dirname(album_dir, sizeof album_dir, meta.artist, meta.album) == 0)
            snprintf(outdir, sizeof outdir, "%s/%s", o->outdir, album_dir);
    }
    if (sr_mkdir_p(outdir)) { perror(outdir); goto out; }
    fprintf(stderr, "Output directory: %s\n", outdir);

    for (int t = from; t <= to; t++) {
        if (cdio_get_track_format(cdio, t) != TRACK_FORMAT_AUDIO) continue;
        int pre = track_has_preemph(cdio, t);
        int apply = (o->deemph == DEEMPH_ON) ||
                    (o->deemph == DEEMPH_AUTO && pre);
        fprintf(stderr, "Track %02d: pre-emphasis flag=%s -> de-emphasis %s\n",
                t, pre ? "SET" : "clear", apply ? "APPLIED" : "not applied");

        char path[4800], fname[400], pe[96], tn[40], tt[40], dt[40];
        char ar[1024], al[1024], ti[1024], gn[300], aa[1024], di[40];
        const char *tags[16]; int nt = 0;
        int idx = t - first;
        const char *title = NULL, *tartist = NULL;

        snprintf(tn, sizeof tn, "TRACKNUMBER=%d", t);
        snprintf(pe, sizeof pe, "SONORA_PREEMPHASIS_FLAG=%s;DEEMPHASIS_APPLIED=%s",
                 pre ? "1" : "0", apply ? "1" : "0");
        tags[nt++] = tn; tags[nt++] = "ENCODER=sonora-rip"; tags[nt++] = pe;
        if (meta.valid && idx >= 0 && idx < meta.ntracks) {
            title   = meta.title[idx];
            tartist = meta.track_artist[idx] ? meta.track_artist[idx] : meta.artist;
            snprintf(tt, sizeof tt, "TRACKTOTAL=%d", (int)ntr); tags[nt++] = tt;
            snprintf(di, sizeof di, "CDDB_DISCID=%08x", (unsigned)meta.discid); tags[nt++] = di;
            if (title)   { snprintf(ti, sizeof ti, "TITLE=%s", title);        tags[nt++] = ti; }
            if (tartist) { snprintf(ar, sizeof ar, "ARTIST=%s", tartist);     tags[nt++] = ar; }
            if (meta.artist) { snprintf(aa, sizeof aa, "ALBUMARTIST=%s", meta.artist); tags[nt++] = aa; }
            if (meta.album)  { snprintf(al, sizeof al, "ALBUM=%s", meta.album);        tags[nt++] = al; }
            if (meta.genre)  { snprintf(gn, sizeof gn, "GENRE=%s", meta.genre);        tags[nt++] = gn; }
            if (meta.year)   { snprintf(dt, sizeof dt, "DATE=%u", meta.year);          tags[nt++] = dt; }
        }
        tags[nt] = NULL;

        sr_track_basename(fname, sizeof fname, t, title);
        snprintf(path, sizeof path, "%s/%s.%s", outdir, fname, sink_ext(o->format));

        lsn_t s = cdio_cddap_track_firstsector(drv, t);
        lsn_t e = cdio_cddap_track_lastsector(drv, t);
        uint64_t frames_total = (uint64_t)(e - s + 1) * (CDIO_CD_FRAMESIZE_RAW / 4);
        audio_sink_t *out = sink_open(path, o->format, 44100, 2, 16,
                                      frames_total, o->flac_level, tags);
        if (!out) { fprintf(stderr, "cannot create %s\n", path); goto out; }

        deemph_t de; deemph_init(&de, 44100.0);
        cdio_paranoia_seek(par, s, SEEK_SET);
        for (lsn_t i = s; i <= e; i++) {
            int16_t *buf = cdio_paranoia_read(par, paranoia_cb);
            if (!buf) { fprintf(stderr, "read error at LSN %d\n", (int)i); sink_close(out); goto out; }
            if (apply) deemph_s16_stereo(&de, buf, CDIO_CD_FRAMESIZE_RAW / 4, o->dither);
            if (sink_write_s16(out, buf, CDIO_CD_FRAMESIZE_RAW / 4)) {
                fprintf(stderr, "write error: %s\n", path); sink_close(out); goto out;
            }
        }
        if (sink_close(out)) { fprintf(stderr, "finalizing %s failed\n", path); goto out; }
    }
    rc = 0;
out:
    sr_meta_free(&meta);
    if (par) cdio_paranoia_free(par);
    if (drv) cdio_cddap_close_no_free_cdio(drv);
    cdio_destroy(cdio);
    return rc;
}
