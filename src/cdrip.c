/* Audio CD ripping with pre-emphasis detection (libcdio + libcdio-paranoia). */
#include "cdrip.h"
#include "deemph.h"
#include "sink.h"
#include "util.h"
#include <cdio/cdio.h>
#include <cdio/paranoia/cdda.h>
#include <cdio/paranoia/paranoia.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void sr_log(const cdrip_opts_t *o, const char *fmt, ...)
{
    char buf[1100];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (o->log) o->log(o->user, buf); else fputs(buf, stderr);
}

/* Pre-emphasis is the Q-subchannel/TOC control flag of each track. */
static int track_has_preemph(CdIo_t *cdio, track_t t)
{
    return cdio_get_track_preemphasis(cdio, t) == CDIO_TRACK_FLAG_TRUE;
}

static void paranoia_cb(long inpos, paranoia_cb_mode_t fn) { (void)inpos; (void)fn; }

int cdrip_scan(const char *device, int lookup, const sr_meta_opts_t *mo, cd_disc_t *d)
{
    memset(d, 0, sizeof *d);
    CdIo_t *cdio = cdio_open(device && *device ? device : NULL, DRIVER_UNKNOWN);
    if (!cdio) return -1;
    track_t first = cdio_get_first_track_num(cdio);
    track_t n     = cdio_get_num_tracks(cdio);
    if (n == 0 || first == CDIO_INVALID_TRACK) { cdio_destroy(cdio); return -1; }

    d->tracks = calloc(n, sizeof *d->tracks);
    if (!d->tracks) { cdio_destroy(cdio); return -1; }
    lba_t leadout = cdio_get_track_lba(cdio, CDIO_CDROM_LEADOUT_TRACK);
    for (int i = 0; i < n; i++) {
        track_t t = (track_t)(first + i);
        lba_t start = cdio_get_track_lba(cdio, t);
        lba_t end   = (i + 1 < n) ? cdio_get_track_lba(cdio, (track_t)(t + 1)) : leadout;
        d->tracks[i].number   = t;
        d->tracks[i].is_audio = cdio_get_track_format(cdio, t) == TRACK_FORMAT_AUDIO;
        d->tracks[i].preemph  = track_has_preemph(cdio, t);
        d->tracks[i].sectors  = end > start ? (uint32_t)(end - start) : 0;
    }
    d->first = first;
    d->ntracks = n;
    if (lookup && mo) sr_meta_lookup(cdio, mo, &d->meta);   /* meta.valid tells the result */
    cdio_destroy(cdio);
    return 0;
}

void cd_disc_free(cd_disc_t *d)
{
    sr_meta_free(&d->meta);
    free(d->tracks);
    memset(d, 0, sizeof *d);
}

static int is_cancelled(const cdrip_opts_t *o) { return o->cancelled && o->cancelled(o->user); }

int cdrip_run(const cdrip_opts_t *o)
{
    int rc = 1;
    cdrom_drive_t *drv = NULL;
    cdrom_paranoia_t *par = NULL;
    sr_meta_t meta;
    memset(&meta, 0, sizeof meta);
    const sr_meta_t *mp = o->meta_in;

    CdIo_t *cdio = cdio_open(o->device && *o->device ? o->device : NULL, DRIVER_UNKNOWN);
    if (!cdio) { sr_log(o, "cannot open drive\n"); return 1; }

    drv = cdio_cddap_identify_cdio(cdio, 1, NULL);
    if (!drv || cdio_cddap_open(drv) != 0) {
        sr_log(o, "cannot open CD-DA interface\n"); goto out;
    }
    par = cdio_paranoia_init(drv);
    cdio_paranoia_modeset(par, o->paranoia ? PARANOIA_MODE_FULL
                                           : PARANOIA_MODE_DISABLE);

    track_t first = cdio_get_first_track_num(cdio);
    track_t ntr   = cdio_get_num_tracks(cdio);
    int from = o->first ? o->first : first;
    int to   = o->last  ? o->last  : first + ntr - 1;
    if (from < first) from = first;
    if (to > first + ntr - 1) to = first + ntr - 1;

    if (!mp && o->use_cddb) {
        int r = sr_meta_lookup(cdio, &o->cddb, &meta);
        if (r == 0) {
            mp = &meta;
            sr_log(o, "CDDB: %s - %s (disc id %08x)\n",
                   meta.artist ? meta.artist : "?", meta.album ? meta.album : "?",
                   (unsigned)meta.discid);
        } else if (r == 1) sr_log(o, "CDDB: no match, using generic names\n");
        else               sr_log(o, "CDDB: lookup failed, using generic names\n");
    }

    /* Output goes to <outdir>/<Artist - Album>/ when metadata gave a name. */
    char outdir[4096];
    snprintf(outdir, sizeof outdir, "%s", o->outdir);
    if (mp && mp->valid) {
        char album_dir[600];
        if (sr_album_dirname(album_dir, sizeof album_dir, mp->artist, mp->album) == 0)
            snprintf(outdir, sizeof outdir, "%s/%s", o->outdir, album_dir);
    }
    if (sr_mkdir_p(outdir)) { sr_log(o, "%s: %s\n", outdir, strerror(errno)); goto out; }
    sr_log(o, "Output directory: %s\n", outdir);

    /* tracks to rip */
    int nsel = 0;
    for (int t = from; t <= to; t++) {
        if (o->track_mask && !o->track_mask[t - first]) continue;
        if (cdio_get_track_format(cdio, t) == TRACK_FORMAT_AUDIO) nsel++;
    }
    if (nsel == 0) { sr_log(o, "no audio tracks selected\n"); goto out; }

    int done = 0;
    for (int t = from; t <= to; t++) {
        if (o->track_mask && !o->track_mask[t - first]) continue;
        if (cdio_get_track_format(cdio, t) != TRACK_FORMAT_AUDIO) continue;
        int pre = track_has_preemph(cdio, t);
        int apply = (o->deemph == DEEMPH_ON) ||
                    (o->deemph == DEEMPH_AUTO && pre);
        sr_log(o, "Track %02d: pre-emphasis flag=%s -> de-emphasis %s\n",
               t, pre ? "SET" : "clear", apply ? "APPLIED" : "not applied");

        char path[4800], fname[400], pe[96], tn[40], tt[40], dt[40];
        char ar[1024], al[1024], ti[1024], gn[300], aa[1024], di[40];
        const char *tags[16];
        int nt = 0;
        int idx = t - first;
        const char *title = NULL, *tartist = NULL;

        snprintf(tn, sizeof tn, "TRACKNUMBER=%d", t);
        snprintf(pe, sizeof pe, "SONORA_PREEMPHASIS_FLAG=%s;DEEMPHASIS_APPLIED=%s",
                 pre ? "1" : "0", apply ? "1" : "0");
        tags[nt++] = tn; tags[nt++] = "ENCODER=sonora-rip"; tags[nt++] = pe;
        if (mp && mp->valid && idx >= 0 && idx < mp->ntracks) {
            title   = mp->title[idx];
            tartist = mp->track_artist[idx] ? mp->track_artist[idx] : mp->artist;
            snprintf(tt, sizeof tt, "TRACKTOTAL=%d", (int)ntr); tags[nt++] = tt;
            if (mp->discid) { snprintf(di, sizeof di, "CDDB_DISCID=%08x", (unsigned)mp->discid); tags[nt++] = di; }
            if (title)   { snprintf(ti, sizeof ti, "TITLE=%s", title);        tags[nt++] = ti; }
            if (tartist) { snprintf(ar, sizeof ar, "ARTIST=%s", tartist);     tags[nt++] = ar; }
            if (mp->artist) { snprintf(aa, sizeof aa, "ALBUMARTIST=%s", mp->artist); tags[nt++] = aa; }
            if (mp->album)  { snprintf(al, sizeof al, "ALBUM=%s", mp->album);        tags[nt++] = al; }
            if (mp->genre)  { snprintf(gn, sizeof gn, "GENRE=%s", mp->genre);        tags[nt++] = gn; }
            if (mp->year)   { snprintf(dt, sizeof dt, "DATE=%u", mp->year);          tags[nt++] = dt; }
        }
        tags[nt] = NULL;

        sr_track_basename(fname, sizeof fname, t, title);
        snprintf(path, sizeof path, "%s/%s.%s", outdir, fname, sink_ext(o->format));

        lsn_t s = cdio_cddap_track_firstsector(drv, t);
        lsn_t e = cdio_cddap_track_lastsector(drv, t);
        uint64_t frames_total = (uint64_t)(e - s + 1) * (CDIO_CD_FRAMESIZE_RAW / 4);
        audio_sink_t *out = sink_open(path, o->format, 44100, 2, 16,
                                      frames_total, o->flac_level, tags);
        if (!out) { sr_log(o, "cannot create %s\n", path); goto out; }

        deemph_t de; deemph_init(&de, 44100.0);
        cdio_paranoia_seek(par, s, SEEK_SET);
        for (lsn_t i = s; i <= e; i++) {
            if (is_cancelled(o)) {
                sink_close(out); unlink(path);
                sr_log(o, "Cancelled.\n");
                rc = 2; goto out;
            }
            int16_t *buf = cdio_paranoia_read(par, paranoia_cb);
            if (!buf) { sr_log(o, "read error at LSN %d\n", (int)i); sink_close(out); unlink(path); goto out; }
            if (apply) deemph_s16_stereo(&de, buf, CDIO_CD_FRAMESIZE_RAW / 4, o->dither);
            if (sink_write_s16(out, buf, CDIO_CD_FRAMESIZE_RAW / 4)) {
                sr_log(o, "write error: %s\n", path); sink_close(out); goto out;
            }
            if (o->progress && ((i - s) % CDIO_CD_FRAMES_PER_SEC == 0 || i == e)) {
                double tf = (double)(i - s + 1) / (double)(e - s + 1);
                o->progress(o->user, t, tf, (done + tf) / nsel);
            }
        }
        if (sink_close(out)) { sr_log(o, "finalizing %s failed\n", path); goto out; }
        done++;
    }
    sr_log(o, "Done: %d track(s).\n", done);
    rc = 0;
out:
    sr_meta_free(&meta);
    if (par) cdio_paranoia_free(par);
    if (drv) cdio_cddap_close_no_free_cdio(drv);
    cdio_destroy(cdio);
    return rc;
}
