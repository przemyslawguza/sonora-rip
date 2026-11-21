/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sonora-rip - CD de-emphasis filter (Red Book 50/15 us) */
#ifndef SONORA_DEEMPH_H
#define SONORA_DEEMPH_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    double b0, b1, a1;      /* H(z) = (b0 + b1 z^-1) / (1 + a1 z^-1) */
    double x1[2], y1[2];    /* per-channel state (L,R) */
} deemph_t;

#define DEEMPH_T1 50e-6     /* pole */
#define DEEMPH_T2 15e-6     /* zero */

void deemph_init(deemph_t *d, double sample_rate);
void deemph_reset(deemph_t *d);
/* In-place on interleaved stereo S16. Rounds+clips; optional TPDF dither. */
void deemph_s16_stereo(deemph_t *d, int16_t *pcm, size_t frames, int dither);
double deemph_gain(const deemph_t *d, double freq, double sample_rate);
#endif
