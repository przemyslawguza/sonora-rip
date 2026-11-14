#include "../src/deemph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double analog(double f)
{
    double w = 2*M_PI*f;
    return sqrt((1+pow(w*DEEMPH_T2,2))/(1+pow(w*DEEMPH_T1,2)));
}
int main(void)
{
    deemph_t d; deemph_init(&d, 44100.0);
    int fail = 0;
    if (fabs(deemph_gain(&d, 1.0, 44100.0) - 1.0) > 1e-3) { puts("FAIL: DC gain"); fail = 1; }
    for (double f = 1000; f <= 16000; f *= 2) {
        double dg = 20*log10(deemph_gain(&d, f, 44100.0)), ag = 20*log10(analog(f));
        printf("%6.0f Hz  digital %7.2f dB  analog %7.2f dB  diff %5.2f\n", f, dg, ag, dg-ag);
        if ((f <= 8000 && fabs(dg-ag) > 0.5) || fabs(dg-ag) > 1.0) fail = 1;
    }
    static int16_t buf[2*44100];
    for (int i = 0; i < 44100; i++) buf[2*i] = buf[2*i+1] = (int16_t)(20000*sin(2*M_PI*10000.0*i/44100.0));
    deemph_reset(&d); deemph_s16_stereo(&d, buf, 44100, 0);
    double pk = 0; for (int i = 22050; i < 44100; i++) if (abs(buf[2*i]) > pk) pk = abs(buf[2*i]);
    double meas = pk/20000, pred = deemph_gain(&d, 10000, 44100);
    printf("10 kHz tone: measured %.4f predicted %.4f\n", meas, pred);
    if (fabs(meas-pred) > 0.01) { puts("FAIL: time domain"); fail = 1; }
    puts(fail ? "RESULT: FAIL" : "RESULT: PASS");
    return fail;
}
