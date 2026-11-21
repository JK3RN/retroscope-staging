# QA — RetroScope 0.1.0

What was tested for this release, how, and what was not. All results were produced on
2026-11-21 with the code of this release; the build and test commands were re-run on a fresh
checkout of the published tree.

## Environment

| Item | Value |
| --- | --- |
| Machine | x86_64 laptop, AMD Ryzen 5 (6 cores), AMD Radeon integrated graphics (Mesa radeonsi) |
| System | Omarchy (Arch-based), Linux 7.2, Hyprland 0.56 on Wayland |
| Toolchain | GCC 16.2.1, Clang 23.1.3, GNU make 4.4.1, pkgconf 3.0.10 |
| Libraries | sdl2-compat 2.32.74 (SDL2 API on SDL3), libpulse 17.0 |
| Audio stack | PipeWire 1.6.9, pipewire-pulse 1.6.9, WirePlumber 0.5.18 |

## Results

| Area | Command or method | Result |
| --- | --- | --- |
| Build, GCC | `make` (`-std=c11 -Wall -Wextra -Wpedantic`) | PASS, 0 warnings |
| Build, Clang, extra warnings | `make CC=clang CFLAGS="-O2 -g -Wshadow -Wformat=2 -Wcast-qual -Wnull-dereference" check` | PASS, 0 warnings |
| Hardened build | `make CFLAGS="-O2 -D_FORTIFY_SOURCE=3 -fstack-protector-strong"` | PASS, 0 warnings |
| Missing dependency | `make PKGS='sdl2 >= 9.0, libpulse'` | PASS: stops with a message naming the dependency |
| CLI | `make check`: `--version`, `--help`, unknown option, `--seconds` without or with invalid value | PASS |
| Headless demo | `make check`: `--demo --seconds 2` on SDL's dummy video driver | PASS |
| No audio server | `make check`: `PULSE_SERVER` pointing to a missing socket | PASS: readable message, exit status 1 |
| Unit and render tests | `make check`: `test_scope`, `test_instrument`, `test_modes` (projection for all three modes, trigger and timebases, buffer overflow, NaN/Inf input, tiny and large windows, render matrix, continuous demo signal, live SDL event loop) | PASS |
| Sanitizers | `make sanitize` with GCC and with Clang (AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer) over all suites and a demo run | PASS, no reports |
| Real audio capture | `make audio-smoke` (12 checks, below) with normal builds and with ASan/LSan builds | PASS, 12 of 12 |
| Install | `make install DESTDIR=… PREFIX=/usr`, run `--version`, `make uninstall` | PASS: one file installed and removed |
| Release archive | `make check` from the extracted `retroscope-0.1.0.tar.gz` | PASS |
| Desktop demo | `./retroscope --demo` on Hyprland/Wayland, OpenGL renderer on the Radeon iGPU, keys sent through the compositor | PASS (details below) |
| Capture soak | 300 s real capture, see below | PASS |
| Static analysis | clang `scan-build`, `clang-tidy` (analyzer, bugprone, cert), GCC `-fanalyzer` | No defects. Reviewed and left as is: advisory findings (bounded `snprintf`, unchecked `fprintf` results, int/float narrowing, combined flag enums) and one `-fanalyzer` false positive (`points[]` is always written before use) |

### Audio capture checks (`make audio-smoke`)

A private PipeWire, WirePlumber and pipewire-pulse run in a temporary directory with hardware
access disabled. Two silent null outputs exist, and a fake microphone is set as the default
input. A synthetic tone (left 0.5 · sin 440 Hz, right −0.25 · sin 440 Hz) is played into the
default output.

1. Captured levels match the tone: RMS left 0.353 and right 0.177 (expected 0.354 and 0.177), left/right correlation −1.000, no invalid samples.
2. The capture source is the default output's monitor.
3. RetroScope records from the monitor, not from the default input (the fake microphone).
4. No playback stream is created.
5. When the default output is switched while running, RetroScope keeps its original monitor and keeps running.
6. An idle run with no sound ends normally.
7. Removing the captured output gives a message and exit status 1 within 0.1 s.
8. SIGTERM exits with status 0.
9. 15 consecutive start and stop cycles all exit with status 0.
10. No record stream is left over after those runs.
11. Stopping the audio server while capturing gives a message and exit status 1 within 0.1 s.
12. With no audio server at all, RetroScope exits immediately with status 1.

### Desktop checks

The real binary ran in a window on the Wayland desktop. Keys were sent to that window
through Hyprland and each result was checked against a window capture:

- `1`, `2`, `3` switch between VECTOR, WAVE and XY.
- `B` steps the WAVE timebase.
- `↑` raises the gain; at ×2 the CLIP lamp lights. `0` and `↓` give ×0.84.
- `→` and `←` step persistence to GHOST and back to FAST.
- `H` prints the help text.
- The window can be shrunk to the compositor's minimum (compact panel) and widened to 1100×640.
- `Q` exits with status 0.

The README screenshots and clip come from the same kind of run (`make showcase`).

### Soak

RetroScope captured a synthetic stereo signal from the private PipeWire instance for 300 s,
using the SDL software renderer without a window. Measurements are from `/proc`, every 10 s:

| Measure | Result |
| --- | --- |
| Exit status | 0, after exactly 300 s (`--seconds 300`) |
| Resident memory | 10 944 KiB in all 30 samples, from 5 s to 295 s |
| Threads, open file descriptors | 2 and 7, constant |
| CPU time | 14.5 % of one core on average (software rendering of every frame) |

This is one run on one machine. It shows no growth over five minutes; it does not prove the
absence of very slow leaks over hours. LeakSanitizer found no leak on any tested exit path.

## Not tested

- Other distributions, X11 sessions, classic SDL 2.x builds and the PulseAudio daemon.
- Bluetooth or multichannel default outputs.
- Runs of several hours with a GPU renderer.
- Real music and oscilloscope music. Only synthetic signals were used, so no recordings were needed.
- Valgrind and cppcheck (not installed) and fuzzing.
