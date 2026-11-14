#ifndef SONORA_CDRIP_H
#define SONORA_CDRIP_H
#include "sink.h"
#include "meta.h"
typedef enum { DEEMPH_AUTO, DEEMPH_ON, DEEMPH_OFF } deemph_mode_t;
typedef struct {
    const char   *device;      /* NULL = autodetect */
    const char   *outdir;
    int           first, last; /* 0 = all */
    deemph_mode_t deemph;
    int           dither;
    int           paranoia;    /* 0 = fast, 1 = full */
    out_format_t  format;      /* FMT_WAV / FMT_FLAC */
    int           flac_level;  /* 0..8, <0 = default */
    int           use_cddb;    /* 1 = look up artist/album/titles */
    sr_meta_opts_t cddb;
} cdrip_opts_t;
int cdrip_run(const cdrip_opts_t *o);
#endif
