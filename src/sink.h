/* Audio output sink: WAV or FLAC (libFLAC). */
#ifndef SONORA_SINK_H
#define SONORA_SINK_H
#include <stddef.h>
#include <stdint.h>

typedef enum { FMT_WAV, FMT_FLAC } out_format_t;
typedef struct audio_sink audio_sink_t;

const char *sink_ext(out_format_t f);               /* "wav" / "flac" */
int         sink_parse_format(const char *s, out_format_t *f); /* 0 ok */

/* tags: NULL-terminated array of "KEY=VALUE" strings (FLAC only; may be NULL).
 * total_frames: expected number of sample frames (0 if unknown).
 * flac_level: 0..8 (default 5 when <0). */
audio_sink_t *sink_open(const char *path, out_format_t fmt, uint32_t rate,
                        unsigned channels, unsigned bits, uint64_t total_frames,
                        int flac_level, const char *const *tags);
int sink_write_s16(audio_sink_t *s, const int16_t *pcm, size_t frames); /* interleaved */
int sink_close(audio_sink_t *s);                    /* 0 = success */
#endif
