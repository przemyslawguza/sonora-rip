/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sink.h"
#include "wav.h"
#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct audio_sink {
    out_format_t fmt;
    unsigned channels;
    FILE *wav;
    FLAC__StreamEncoder *enc;
    FLAC__StreamMetadata *meta;
    FLAC__int32 *buf;
    size_t buf_cap;     /* in samples */
};

const char *sink_ext(out_format_t f) { return f == FMT_FLAC ? "flac" : "wav"; }

int sink_parse_format(const char *s, out_format_t *f)
{
    if (!strcmp(s, "wav"))  { *f = FMT_WAV;  return 0; }
    if (!strcmp(s, "flac")) { *f = FMT_FLAC; return 0; }
    return -1;
}

audio_sink_t *sink_open(const char *path, out_format_t fmt, uint32_t rate,
                        unsigned ch, unsigned bits, uint64_t total,
                        int level, const char *const *tags)
{
    audio_sink_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->fmt = fmt; s->channels = ch;

    if (fmt == FMT_WAV) {
        s->wav = wav_open(path, rate, ch, bits);
        if (!s->wav) { free(s); return NULL; }
        return s;
    }

    s->enc = FLAC__stream_encoder_new();
    if (!s->enc) { free(s); return NULL; }
    if (level < 0 || level > 8) level = 5;
    FLAC__bool ok = 1;
    ok &= FLAC__stream_encoder_set_verify(s->enc, true);
    ok &= FLAC__stream_encoder_set_compression_level(s->enc, level);
    ok &= FLAC__stream_encoder_set_channels(s->enc, ch);
    ok &= FLAC__stream_encoder_set_bits_per_sample(s->enc, bits);
    ok &= FLAC__stream_encoder_set_sample_rate(s->enc, rate);
    if (total) ok &= FLAC__stream_encoder_set_total_samples_estimate(s->enc, total);

    if (tags && tags[0]) {
        s->meta = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
        for (int i = 0; s->meta && tags[i]; i++) {
            const char *eq = strchr(tags[i], '=');
            if (!eq) continue;
            char key[64];
            size_t kl = (size_t)(eq - tags[i]);
            if (kl == 0 || kl >= sizeof key) continue;
            memcpy(key, tags[i], kl); key[kl] = 0;
            FLAC__StreamMetadata_VorbisComment_Entry e;
            if (FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&e, key, eq + 1))
                FLAC__metadata_object_vorbiscomment_append_comment(s->meta, e, false);
        }
        if (s->meta) ok &= FLAC__stream_encoder_set_metadata(s->enc, &s->meta, 1);
    }

    if (!ok || FLAC__stream_encoder_init_file(s->enc, path, NULL, NULL)
                != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        if (s->meta) FLAC__metadata_object_delete(s->meta);
        FLAC__stream_encoder_delete(s->enc);
        free(s);
        return NULL;
    }
    return s;
}

int sink_write_s16(audio_sink_t *s, const int16_t *pcm, size_t frames)
{
    if (s->fmt == FMT_WAV)
        return wav_write(s->wav, pcm, frames * s->channels * sizeof(int16_t));

    size_t n = frames * s->channels;
    if (n > s->buf_cap) {
        FLAC__int32 *nb = realloc(s->buf, n * sizeof *nb);
        if (!nb) return -1;
        s->buf = nb; s->buf_cap = n;
    }
    for (size_t i = 0; i < n; i++) s->buf[i] = pcm[i];
    return FLAC__stream_encoder_process_interleaved(s->enc, s->buf, (unsigned)frames) ? 0 : -1;
}

int sink_close(audio_sink_t *s)
{
    int rc = 0;
    if (!s) return -1;
    if (s->fmt == FMT_WAV) {
        rc = wav_close(s->wav);
    } else {
        if (!FLAC__stream_encoder_finish(s->enc)) rc = -1; /* also fails on verify mismatch */
        FLAC__stream_encoder_delete(s->enc);
        if (s->meta) FLAC__metadata_object_delete(s->meta);
    }
    free(s->buf);
    free(s);
    return rc;
}
