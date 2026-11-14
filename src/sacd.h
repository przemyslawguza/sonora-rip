#ifndef SONORA_SACD_H
#define SONORA_SACD_H
typedef struct {
    const char *input;      /* ISO image, device node, or host[:2002] (PS3 sacd-ripper) */
    const char *outdir;
    int         multichannel;
    int         to_pcm;
    int         pcm_rate;
    int         pcm_bits;
    int         flac;           /* 1 = PCM output as FLAC instead of WAV */
    int         flac_level;     /* 0..12 (ffmpeg), <0 = default */
    const char *extract_bin;    /* default "sacd_extract" */
    const char *ffmpeg_bin;     /* default "ffmpeg" */
    void      (*log)(void *user, const char *msg);   /* optional, NULL = stderr */
    void       *user;
} sacd_opts_t;
int sacd_run(const sacd_opts_t *o);
#endif
