#include "deemph.h"
#include <math.h>

/* Analog prototype H(s) = (1 + s*T2)/(1 + s*T1): unity DC gain, T2/T1 = 0.3
 * (-10.46 dB) at HF.  Bilinear transform with K = 2*fs. */
void deemph_init(deemph_t *d, double fs)
{
    /* Plain bilinear: within 0.5 dB of the analog curve up to 8 kHz and
     * ~1 dB at 16-20 kHz (inherent frequency warping near Nyquist; prewarping
     * was evaluated and does not improve the worst case). */
    double K = 2.0 * fs;
    double den = 1.0 + K * DEEMPH_T1;
    d->b0 = (1.0 + K * DEEMPH_T2) / den;
    d->b1 = (1.0 - K * DEEMPH_T2) / den;
    d->a1 = (1.0 - K * DEEMPH_T1) / den;
    deemph_reset(d);
}

void deemph_reset(deemph_t *d)
{
    d->x1[0] = d->x1[1] = d->y1[0] = d->y1[1] = 0.0;
}

static uint32_t rng = 0x12345678u;
static double rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng / 4294967296.0;
}

static int16_t to_s16(double v, int dither)
{
    if (dither) v += rnd() - rnd();
    v = floor(v + 0.5);
    if (v > 32767.0) v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    return (int16_t)v;
}

void deemph_s16_stereo(deemph_t *d, int16_t *pcm, size_t frames, int dither)
{
    for (size_t i = 0; i < frames; i++) {
        for (int c = 0; c < 2; c++) {
            double x = pcm[2 * i + c];
            double y = d->b0 * x + d->b1 * d->x1[c] - d->a1 * d->y1[c];
            d->x1[c] = x;
            d->y1[c] = y;
            pcm[2 * i + c] = to_s16(y, dither);
        }
    }
}

double deemph_gain(const deemph_t *d, double f, double fs)
{
    double w = 2.0 * M_PI * f / fs;
    double nr = d->b0 + d->b1 * cos(w), ni = -d->b1 * sin(w);
    double dr = 1.0 + d->a1 * cos(w),   di = -d->a1 * sin(w);
    return sqrt((nr*nr + ni*ni) / (dr*dr + di*di));
}
