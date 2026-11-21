/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cdrip.h"
#include "sacd.h"
#include "sink.h"
#include "util.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VERSION
#define VERSION "1.0.0"
#endif

static void usage(void)
{
    puts("sonora-rip " VERSION "\n"
"Usage:\n"
"  sonora-rip cd   [-d DEV] [-o DIR] [-t N[-M]] [--deemph auto|on|off (default auto)] [--format wav|flac] [--flac-level 0-8]\n          [--deemph-tracks LIST] [--no-deemph-tracks LIST]\n          [--no-cddb] [--cddb-server HOST[:PORT]] [--cddb-http] [--cddb-match N] [--dither] [--fast]\n"
"  sonora-rip sacd -i ISO|DEV|HOST[:2002] [-o DIR] [--multichannel] [--pcm [--rate HZ] [--bits 16|24] [--format wav|flac]]\n"
"\nCDDB: artist/album/titles are fetched from gnudb.gnudb.org (port 8880; --cddb-http uses port 80)\n"
"and used for file names and tags. Results are cached in ~/.cddb. --no-cddb disables the lookup.\n"
"\nAutomatic (default): tracks carrying the pre-emphasis flag are de-emphasised; --deemph off disables it,\n"
"--no-deemph-tracks LIST disables it for single tracks.\n"
"\nManual pre-emphasis: --deemph-tracks 1,3,5-7 (or all) forces de-emphasis on those tracks even if\n"
"the disc does not flag them; --no-deemph-tracks forces it off. Manual choices beat the flag and --deemph.\n"
"\nCD: reads the Red Book pre-emphasis flag per track; with --deemph auto (default)\n"
"the 50/15us de-emphasis is applied only to flagged tracks.");
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 2; }
    const char *mode = argv[1];
    cdrip_opts_t c = { .device = NULL, .outdir = ".", .deemph = DEEMPH_AUTO,
                       .paranoia = 1, .format = FMT_WAV, .flac_level = -1,
                       .use_cddb = 1 };
    char srvbuf[256];
    static signed char ovr[SR_MAX_TRACKS];
    memset(ovr, -1, sizeof ovr);
    sacd_opts_t  s = { .input = NULL, .outdir = ".", .pcm_rate = 88200, .pcm_bits = 24, .flac_level = -1 };
    enum { O_DEEMPH = 1000, O_DITHER, O_FAST, O_MC, O_PCM, O_RATE, O_BITS, O_FORMAT, O_LEVEL, O_NOCDDB, O_CDDBSRV, O_CDDBHTTP, O_CDDBMATCH, O_DETRK, O_NODETRK };
    static struct option lo[] = {
        {"deemph", 1, 0, O_DEEMPH}, {"dither", 0, 0, O_DITHER}, {"fast", 0, 0, O_FAST},
        {"multichannel", 0, 0, O_MC}, {"pcm", 0, 0, O_PCM},
        {"deemph-tracks", 1, 0, O_DETRK}, {"no-deemph-tracks", 1, 0, O_NODETRK},
        {"no-cddb", 0, 0, O_NOCDDB}, {"cddb-server", 1, 0, O_CDDBSRV}, {"cddb-http", 0, 0, O_CDDBHTTP},
        {"cddb-match", 1, 0, O_CDDBMATCH}, {"format", 1, 0, O_FORMAT}, {"flac-level", 1, 0, O_LEVEL}, {"rate", 1, 0, O_RATE}, {"bits", 1, 0, O_BITS}, {"help", 0, 0, 'h'}, {0,0,0,0}
    };
    optind = 2;
    int ch;
    while ((ch = getopt_long(argc, argv, "d:o:t:i:h", lo, NULL)) != -1) {
        switch (ch) {
        case 'd': c.device = optarg; break;
        case 'o': c.outdir = s.outdir = optarg; break;
        case 't': if (sscanf(optarg, "%d-%d", &c.first, &c.last) == 1) c.last = c.first; break;
        case 'i': s.input = optarg; break;
        case O_DEEMPH:
            c.deemph = !strcmp(optarg, "on") ? DEEMPH_ON :
                       !strcmp(optarg, "off") ? DEEMPH_OFF : DEEMPH_AUTO; break;
        case O_DITHER: c.dither = 1; break;
        case O_FAST: c.paranoia = 0; break;
        case O_MC: s.multichannel = 1; break;
        case O_PCM: s.to_pcm = 1; break;
        case O_RATE: s.pcm_rate = atoi(optarg); break;
        case O_BITS: s.pcm_bits = atoi(optarg); break;
        case O_FORMAT: {
            out_format_t f;
            if (sink_parse_format(optarg, &f)) { fprintf(stderr, "unknown format '%s' (wav|flac)\n", optarg); return 2; }
            c.format = f; s.flac = (f == FMT_FLAC); break; }
        case O_DETRK:
        case O_NODETRK:
            if (sr_parse_track_list(optarg, ovr, SR_MAX_TRACKS, ch == O_DETRK ? 1 : 0)) {
                fprintf(stderr, "bad track list '%s' (use e.g. 1,3,5-7 or all)\n", optarg);
                return 2;
            }
            c.preemph_override = ovr; break;
        case O_NOCDDB: c.use_cddb = 0; break;
        case O_CDDBHTTP: c.cddb.http = 1; break;
        case O_CDDBMATCH: c.cddb.match = atoi(optarg); break;
        case O_CDDBSRV: {
            snprintf(srvbuf, sizeof srvbuf, "%s", optarg);
            char *colon = strrchr(srvbuf, ':');
            if (colon) { *colon = 0; c.cddb.port = atoi(colon + 1); }
            c.cddb.server = srvbuf; break; }
        case O_LEVEL: c.flac_level = s.flac_level = atoi(optarg); break;
        default: usage(); return 2;
        }
    }
    if (!strcmp(mode, "cd"))   return cdrip_run(&c);
    if (!strcmp(mode, "sacd")) { if (!s.input) { usage(); return 2; } return sacd_run(&s); }
    usage(); return 2;
}
