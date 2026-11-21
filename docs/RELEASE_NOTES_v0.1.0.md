# RetroScope 0.1.0

The first release of RetroScope, a passive stereo signal instrument for Linux. It shows what
your desktop is playing as a phosphor trace, as VECTOR (stereo image), WAVE (triggered
waveform) or XY (oscilloscope), in a dark CRT-style panel. C11, SDL2 and libpulse; one small
binary.

## Highlights

- **Three views of one signal**, switchable live with `1`, `2` and `3`.
- **Passive and private by design:** records only the monitor of the default output. It never
  uses a microphone, never plays anything, stores nothing and has no network code.
- **Try it without audio:** `./retroscope --demo` runs a built-in synthetic signal.
- **Instrument feel:** exponential phosphor persistence, display-only gain, SIGNAL and CLIP
  lamps, and an adaptive HiDPI panel that keeps working in small tiling-WM tiles.

## Getting started

Download `retroscope-0.1.0.tar.gz` from this release (or clone the repository), then:

```sh
tar xzf retroscope-0.1.0.tar.gz && cd retroscope-0.1.0
make && ./retroscope --demo     # no audio server needed
./retroscope                    # visualize the default output
```

Requirements: a C11 compiler, `make`, `pkg-config`, SDL2 2.0.18 or newer and libpulse;
PipeWire-Pulse (or PulseAudio) for live mode. Verify the download with `sha256sum -c SHA256SUMS`.

## Tested

On Omarchy (Arch-based) x86_64 with Hyprland on Wayland, GCC 16 and Clang 23, sdl2-compat
2.32, libpulse 17 and PipeWire 1.6: warning-free builds, all test suites, sanitizers, real
capture against a private PipeWire instance, a 5-minute capture soak and desktop runs with
real key input. Other distributions, X11 and classic SDL2 have not been tested yet. Details:
[docs/QA.md](QA.md).

## Known limitations

- Uses the default output chosen at startup; exits with a message, without reconnecting, if
  that output or the audio server disappears.
- Terminal messages and the in-app help are in German.
- A visual instrument, not a calibrated measuring device.

Full list of changes: [CHANGELOG.md](../CHANGELOG.md).
