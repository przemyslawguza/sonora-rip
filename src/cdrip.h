#ifndef SONORA_CDRIP_H
#define SONORA_CDRIP_H
#include "sink.h"
#include "meta.h"
#include <stdint.h>

typedef enum { DEEMPH_AUTO, DEEMPH_ON, DEEMPH_OFF } deemph_mode_t;

/* Optional hooks, used by the GUI. They are called from the ripping thread. */
typedef void (*sr_log_fn)(void *user, const char *msg);                 /* msg ends with \n */
typedef void (*sr_progress_fn)(void *user, int track, double track_frac, double total_frac);
typedef int  (*sr_cancel_fn)(void *user);                               /* nonzero = stop */

typedef struct {
    const char   *device;      /* NULL or "" = autodetect */
    const char   *outdir;
    int           first, last; /* 0 = all */
    deemph_mode_t deemph;
    int           dither;
    int           paranoia;    /* 0 = fast, 1 = full */
    out_format_t  format;      /* FMT_WAV / FMT_FLAC */
    int           flac_level;  /* 0..8, <0 = default */
    int           use_cddb;    /* 1 = look up artist/album/titles */
    sr_meta_opts_t cddb;
    /* --- library / GUI use (all optional) --- */
    const sr_meta_t     *meta_in;    /* use this metadata instead of a CDDB lookup */
    const unsigned char *track_mask; /* [ntracks], 1 = rip; indexed by position; NULL = first..last */
    sr_log_fn      log;              /* NULL = stderr */
    sr_progress_fn progress;
    sr_cancel_fn   cancelled;
    void          *user;
} cdrip_opts_t;

typedef struct {
    int      number;      /* track number on the disc */
    int      is_audio;
    int      preemph;     /* Red Book pre-emphasis flag */
    uint32_t sectors;     /* length in frames (75 per second) */
} cd_track_info_t;

typedef struct {
    int              first;
    int              ntracks;
    cd_track_info_t *tracks;
    sr_meta_t        meta;   /* meta.valid == 0 when no lookup / no match */
} cd_disc_t;

/* Read the TOC (and optionally CDDB). 0 = ok, -1 = no disc / drive error. */
int  cdrip_scan(const char *device, int lookup, const sr_meta_opts_t *mo, cd_disc_t *d);
void cd_disc_free(cd_disc_t *d);

/* Optical drives found on this machine: NULL-terminated array of device paths
 * (may be empty, never NULL unless out of memory). Free with cdrip_free_list(). */
char **cdrip_list_drives(void);
void   cdrip_free_list(char **list);

/* 0 = ok, 1 = error, 2 = cancelled */
int  cdrip_run(const cdrip_opts_t *o);
#endif
