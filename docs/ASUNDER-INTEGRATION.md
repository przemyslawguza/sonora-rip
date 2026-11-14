# Integrating with the Asunder GUI

Asunder (GPL-2.0-or-later, C/GTK, drives `cdparanoia` + encoders as child
processes) could not be downloaded in the environment that generated this
tree (no network), so its source is NOT included. Plan for the fork:

1. Fetch Asunder source, rename binary/package to `sonora-rip`, keep GPL headers
   and credits.
2. Replace the cdparanoia child process in Asunder's rip code with calls into
   `src/cdrip.c` (libcdio-paranoia), or keep cdparanoia and pipe the WAV
   through `deemph_s16_stereo()` before encoding.
3. Query `cdio_get_track_preemphasis()` per track when the TOC is read; show a
   "Pre-emphasis" column and a preference: Auto / Always / Never.
4. Add an "SACD" tab/source calling `sacd_run()` (src/sacd.c) in a worker.
5. Keep the CLI (`src/main.c`) as the `sonora-rip` frontend or split into
   libsonora + GUI + CLI.
