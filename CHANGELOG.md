# Changelog

All notable changes to RetroScope. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/).

## 0.1.0 — 2026-11-21

First public release.

### Features
- Passive capture of the default output's monitor through libpulse (PipeWire-Pulse),
  48 kHz float stereo. Never a microphone, no playback, no storage, no network code.
- Three display modes: VECTOR (stereo mid/side), WAVE (triggered mono mix, 20/5/40 ms
  timebase) and XY (X = left, Y = right).
- Display gain ×0.25 to ×16, four persistence levels, SIGNAL and CLIP status lamps.
- Adaptive, HiDPI-aware instrument panel with a built-in bitmap font; freely resizable.
- `--demo` synthetic signal without any audio connection; `--seconds N` for scripted runs;
  `--version`, `--help`.
- `make install` / `make uninstall` with `PREFIX`, `BINDIR` and `DESTDIR`.
- `make check`, `make sanitize` (ASan/UBSan/LSan) and `make audio-smoke` (real capture
  against a private, silent PipeWire instance).
- `make showcase`: reproducible README screenshots captured from the running app.

### Fixed during release preparation
- `--demo` produced phase jumps between frames, which showed as doubled traces in WAVE.
- Window tiles 30 px high or less (possible under tiling window managers) terminated the
  program with an SDL error; the display area is now kept at least 1×1 px.
- Overriding `CFLAGS` on the `make` command line dropped `-std=c11` and the warnings.
- `--seconds` without a value now names the missing duration; Ctrl+C while connecting to the
  audio server exits quietly.
- Tests write their files to `build/` instead of fixed paths in `/tmp`.
