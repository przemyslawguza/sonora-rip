# sonora-rip 1.0.0
CD ripper with Red Book pre-emphasis detection + 50/15 us de-emphasis, and an
SACD extraction front-end. License: GPL-2+.

## Build
    sudo apt install build-essential pkg-config libcdio-dev libcdio-paranoia-dev libflac-dev libcddb2-dev libgtk-3-dev
    make && make check && sudo make install

## Other architectures
Native on arm64/amd64. Cross: `make CC=aarch64-linux-gnu-gcc PKG_CONFIG=aarch64-linux-gnu-pkg-config`
(needs the :arm64 -dev packages / multiarch). Debian: `dpkg-buildpackage -us -uc -b -a arm64`.

## Debian 13 package
    sudo apt install devscripts debhelper
    dpkg-buildpackage -us -uc -b      # produces ../sonora-rip_1.0.0_<arch>.deb

## SACD
Requires `sacd_extract` (sacd-ripper) in PATH. A normal DVD drive generally
cannot read the SACD layer; use an ISO from a supported source, the PS3
sacd-ripper network daemon (host:2002), or a drive known to work.

## FLAC output
    sonora-rip cd -o out --format flac [--flac-level 0-8]
    sonora-rip sacd -i disc.iso -o out --pcm --rate 88200 --bits 24 --format flac
CD: libFLAC encoder with verification, tags TRACKNUMBER, ENCODER and the
pre-emphasis flag/de-emphasis status. SACD PCM: encoded via ffmpeg.

## CDDB / gnudb metadata
On by default for `cd` mode: artist, album, year, genre and track titles are
fetched and used for tags and names. Layout: `<outdir>/<Artist - Album>/NN-Title.flac`
(e.g. `out/AC_DC - Back In Black/01-Hells Bells.flac`); `/` and other characters
illegal in file names become `_`. Without a CDDB match: `<outdir>/NN-Track.ext`.

    sonora-rip cd -o out --format flac                    # gnudb.gnudb.org:8880 (CDDBP)
    sonora-rip cd -o out --cddb-http                      # HTTP, port 80
    sonora-rip cd -o out --cddb-server host[:port]        # other CDDB server
    sonora-rip cd -o out --cddb-match 2                   # pick 2nd candidate
    sonora-rip cd -o out --no-cddb                        # offline, generic names

freedb.org was shut down in 2020; gnudb.org is the compatible successor.
Lookups are cached by libcddb in ~/.cddb. The disc's table of contents
(not your files) is what is sent to the server.

## Graphical interface
`sonora-rip-gui` (GTK 3) is built automatically when `gtk+-3.0` is found
(`make GUI=no` to skip it). Start it from the menu ("Sonora Rip") or the shell.

* **Audio CD tab:** the disc is scanned and looked up on CDDB at start-up (and on
  "Refresh"). Album artist/album/year/genre and every track's artist and title
  can be edited; untick tracks you do not want. The "Pre-emphasis" column shows the
  Red Book flag per track. Choose WAV/FLAC (+FLAC level), de-emphasis mode, dither,
  secure mode and the output folder, then "Rip selected tracks". Cancel stops
  cleanly and deletes the unfinished file.
* **Refresh button:** after inserting or swapping a disc (or plugging in a drive)
  press *Refresh*. It clears the track list, re-detects the optical drives (the
  "Drive" box is a list you can also type into), re-reads the disc and repeats the
  CDDB lookup. Drive = "(auto-detect)" uses the first drive libcdio finds.
* **SACD tab:** ISO image / device / `host:2002`, stereo or multichannel area,
  optional PCM conversion (rate, bits, WAV/FLAC). SACD extraction cannot be
  cancelled yet; the output of `sacd_extract` itself appears in the terminal.
* Output layout is the same as the CLI: `<folder>/<Artist - Album>/NN-Title.ext`.
* Settings are not persisted between runs yet.

## Manual pre-emphasis
Some discs are pre-emphasised as a whole (or mastered with pre-emphasis but never
flagged) so the per-track flag is wrong or missing. De-emphasis can therefore be set by hand:

* **GUI:** each track has a *De-emphasis* checkbox, pre-ticked from the *Disc flag*
  column. Tick/untick tracks freely, or use *all tracks* / *no tracks* / *as flagged*.
  What is ticked is what gets applied.
* **CLI:** `--deemph-tracks 1,3,5-7` (or `all`) forces it on, `--no-deemph-tracks LIST`
  forces it off; manual choices beat both the disc flag and `--deemph auto|on|off`.
  Example, whole disc is pre-emphasised but unflagged: `sonora-rip cd --deemph-tracks all`.
* FLAC tags record the outcome: `SONORA_PREEMPHASIS_FLAG=0;DEEMPHASIS_APPLIED=1;MANUAL=1`.
* How to tell: pre-emphasised audio sounds bright/harsh when played without
  de-emphasis, and old (1980s-90s) digital recordings are the usual suspects.

## Automatic mode (default) and how to disable it
* **Automatic is on by default.** A track whose Red Book pre-emphasis flag is set is
  de-emphasised without any action from you.
* **Disable it for everything:** GUI - untick *Automatic: de-emphasise tracks the disc
  flags* (all ticks are cleared; only tracks you tick are processed). CLI - `--deemph off`.
* **Disable it for single tracks:** GUI - untick the track's *De-emphasis* box.
  CLI - `--no-deemph-tracks 3,5`.
* **Enable it by hand** where the disc does not flag it: tick the box / `--deemph-tracks`.
* Log and tags say `(manual)` / `MANUAL=1` only when you changed the outcome the
  flag would have given.
