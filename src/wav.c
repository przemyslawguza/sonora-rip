#include "wav.h"
#include <string.h>

static void le32(unsigned char *p, uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void le16(unsigned char *p, uint16_t v){p[0]=v;p[1]=v>>8;}

FILE *wav_open(const char *path, uint32_t rate, uint16_t ch, uint16_t bits)
{
    unsigned char h[44];
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    memcpy(h, "RIFF", 4); le32(h+4, 36);
    memcpy(h+8, "WAVEfmt ", 8); le32(h+16, 16); le16(h+20, 1); le16(h+22, ch);
    le32(h+24, rate); le32(h+28, rate * ch * bits / 8);
    le16(h+32, ch * bits / 8); le16(h+34, bits);
    memcpy(h+36, "data", 4); le32(h+40, 0);
    if (fwrite(h, 1, 44, f) != 44) { fclose(f); return NULL; }
    return f;
}

int wav_write(FILE *f, const void *d, size_t n) { return fwrite(d, 1, n, f) == n ? 0 : -1; }

int wav_close(FILE *f)
{
    unsigned char b[4];
    long end = ftell(f);
    if (end < 44) { fclose(f); return -1; }
    fseek(f, 4, SEEK_SET);  le32(b, (uint32_t)(end - 8));  fwrite(b, 1, 4, f);
    fseek(f, 40, SEEK_SET); le32(b, (uint32_t)(end - 44)); fwrite(b, 1, 4, f);
    return fclose(f);
}
