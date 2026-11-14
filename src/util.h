#ifndef SONORA_UTIL_H
#define SONORA_UTIL_H
#include <stddef.h>
/* Make a string safe as a file name component (UTF-8 aware truncation).
 * Replaces / \ : * ? " < > | and control chars, trims dots/spaces. */
void sr_sanitize_filename(char *dst, size_t dstsz, const char *src);

/* "<artist> - <album>" (either may be NULL) as a sanitized directory name.
 * Returns 0 if a name was produced, -1 if both are empty (caller keeps outdir). */
int  sr_album_dirname(char *dst, size_t n, const char *artist, const char *album);
/* "NN-<title>" ; NN is the zero-padded track number. title NULL/empty -> "NN-Track". */
void sr_track_basename(char *dst, size_t n, int track, const char *title);
/* mkdir -p for a path (mode 0755). Returns 0 on success. */
int  sr_mkdir_p(const char *path);
#endif
