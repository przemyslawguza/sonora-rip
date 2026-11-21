# Changelog

## 1.0.0 - 2026-11-21
First stable release.

### Added
- Audio CD ripping (libcdio-paranoia) to WAV or FLAC (libFLAC, verified, tagged).
- Red Book pre-emphasis detection and 50/15 us de-emphasis filter.
  - Automatic for flagged tracks (default); can be disabled globally or per track.
  - Manual per-track enable for discs that are pre-emphasised but not flagged:
    GUI checkboxes, `--deemph-tracks` / `--no-deemph-tracks`.
- CDDB lookup through gnudb.org (CDDBP or HTTP): ARTIST, ALBUM, ALBUMARTIST, TITLE,
  DATE, GENRE, TRACKTOTAL tags; `<Artist - Album>/NN-Title.ext` layout.
- SACD extraction front-end (`sacd_extract`, optional ffmpeg PCM/FLAC conversion).
- GTK 3 interface: drive/disc Refresh, editable metadata, track selection,
  progress, cancel, log.
- Debian 13 packaging, portable Makefile (native or cross: arm64, amd64).

### Known limitations
- SACD extraction cannot be cancelled; the layer is only readable with ISO images,
  a PS3 network ripper or a supported drive.
- Only the first CDDB candidate is used unless `--cddb-match N` is given (no picker).
- GUI settings are not persisted between runs.
- De-emphasis is a first-order bilinear filter: within 0.5 dB of the analog curve up
  to 8 kHz, about 1 dB at 16-20 kHz.
