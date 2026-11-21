# Release checklist

Before tagging a stable version, on real hardware (amd64 and, if possible, arm64):

1. `make clean && make && make check` - no warnings, tests pass.
2. `dpkg-buildpackage -us -uc -b` on Debian 13, then `lintian ../sonora-rip_*.changes`.
3. Install the .deb in a clean Debian 13 system; `sonora-rip --help`, start `sonora-rip-gui`.
4. Rip a normal CD to FLAC: files verify with `flac -t`, tags and folder name correct.
5. Rip a disc whose tracks carry the pre-emphasis flag: log says "de-emphasis APPLIED";
   compare spectrum with `--deemph off`. Check `--deemph-tracks all` on an unflagged disc.
6. CDDB: hit, miss, `--cddb-http`, `--no-cddb`.
7. GUI: Refresh with and without a disc, edit titles, untick tracks, Cancel mid-track
   (partial file must be deleted).
8. SACD: ISO input with `sacd_extract` installed; check the flags against `sacd_extract --help`.
9. `git tag -a v1.0.0 -m "sonora-rip 1.0.0" && git push origin v1.0.0`; `make dist`.
