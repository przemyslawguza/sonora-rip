/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SONORA_WAV_H
#define SONORA_WAV_H
#include <stdio.h>
#include <stdint.h>
FILE *wav_open(const char *path, uint32_t rate, uint16_t channels, uint16_t bits);
int   wav_write(FILE *f, const void *data, size_t bytes);
int   wav_close(FILE *f);
#endif
