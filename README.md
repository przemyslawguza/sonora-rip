# sonora-rip 0.1.0
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
    dpkg-buildpackage -us -uc -b      # produces ../sonora-rip_0.1.0_<arch>.deb

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
  "Scan disc"). Album artist/album/year/genre and every track's artist and title
  can be edited; untick tracks you do not want. The "Pre-emphasis" column shows the
  Red Book flag per track. Choose WAV/FLAC (+FLAC level), de-emphasis mode, dither,
  secure mode and the output folder, then "Rip selected tracks". Cancel stops
  cleanly and deletes the unfinished file.
* **SACD tab:** ISO image / device / `host:2002`, stereo or multichannel area,
  optional PCM conversion (rate, bits, WAV/FLAC). SACD extraction cannot be
  cancelled yet; the output of `sacd_extract` itself appears in the terminal.
* Output layout is the same as the CLI: `<folder>/<Artist - Album>/NN-Title.ext`.
* Settings are not persisted between runs yet.
