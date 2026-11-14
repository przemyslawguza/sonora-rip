/* SACD extraction backend.
 *
 * Reading/decrypting the SACD layer is delegated to sacd-ripper's
 * `sacd_extract`; we do not reimplement drive authentication or DST.
 * Stage 1: sacd_extract -> DSF.  Stage 2 (optional): ffmpeg DSD -> PCM.
 * Command lines are kept in this file so they can be adapted to the
 * installed sacd_extract version. */
#define _GNU_SOURCE
#include "sacd.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int run(char *const argv[])
{
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) { execvp(argv[0], argv); perror(argv[0]); _exit(127); }
    int st; if (waitpid(p, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcasecmp(s + a - b, suf) == 0;
}

int sacd_run(const sacd_opts_t *o)
{
    const char *ex = o->extract_bin ? o->extract_bin : "sacd_extract";
    const char *ff = o->ffmpeg_bin  ? o->ffmpeg_bin  : "ffmpeg";
    char dsd_dir[4096], cwd[4096];
    snprintf(dsd_dir, sizeof dsd_dir, "%s/dsd", o->outdir);
    mkdir(o->outdir, 0755); mkdir(dsd_dir, 0755);

    if (!getcwd(cwd, sizeof cwd)) return 1;
    if (chdir(dsd_dir)) { perror(dsd_dir); return 1; }

    /* sacd_extract: -i input, -2 stereo / -m multichannel, -s one DSF per track,
     * -c convert DST to DSD.  VERIFY against `sacd_extract --help` on your build. */
    char *argv[] = { (char *)ex, "-i", (char *)o->input,
                     (char *)(o->multichannel ? "-m" : "-2"),
                     "-s", "-c", NULL };
    int rc = run(argv);
    if (chdir(cwd)) { /* ignore */ }
    if (rc) { fprintf(stderr, "%s failed (%d)\n", ex, rc); return 1; }
    if (!o->to_pcm) return 0;

    char rate[16], codec[16];
    snprintf(rate, sizeof rate, "%d", o->pcm_rate ? o->pcm_rate : 88200);
    if (o->flac) snprintf(codec, sizeof codec, "flac");
    else         snprintf(codec, sizeof codec, "pcm_s%dle", o->pcm_bits == 16 ? 16 : 24);
    char lvl[8]; snprintf(lvl, sizeof lvl, "%d", o->flac_level >= 0 ? o->flac_level : 5);
    const char *ext = o->flac ? "flac" : "wav";

    DIR *d = opendir(dsd_dir);
    if (!d) return 1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!has_suffix(de->d_name, ".dsf")) continue;
        char in[8192], out[8192];
        snprintf(in, sizeof in, "%s/%s", dsd_dir, de->d_name);
        snprintf(out, sizeof out, "%s/%.*s.%s", o->outdir,
                 (int)(strlen(de->d_name) - 4), de->d_name, ext);
        char *fa[] = { (char *)ff, "-y", "-loglevel", "error", "-i", in,
                       "-ar", rate, "-c:a", codec,
                       "-sample_fmt", (char *)(o->pcm_bits == 16 ? "s16" : "s32"), /* FLAC 24-bit = s32 container */
                       "-compression_level", lvl, out, NULL };
        if (run(fa)) fprintf(stderr, "ffmpeg failed for %s\n", in);
    }
    closedir(d);
    return 0;
}
