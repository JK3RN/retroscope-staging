# RetroScope architecture (v0.1.0)

This describes the code as it is, not an idealized design. RetroScope is about 590 lines of
C11 in one translation unit.

## Files and responsibilities

| File | Lines* | Responsibility |
| --- | ---: | --- |
| `src/retroscope.c` | ~280 | Includes, constants (`CAPACITY` 2048, `SAMPLE_RATE` 48000, version), **audio capture** (`Audio`, libpulse callbacks, `audio_open`/`audio_close`), VECTOR projection `project()`, **CLI parsing**, **SDL lifecycle and main loop**, `demo_signal()` generator. |
| `src/scope.h` | ~100 | **Mode logic**, free of I/O: `ScopeState`, mode/timebase selection, WAVE history ring buffer (3840 mono samples), rising zero-crossing trigger, clip detection, point projection for all three modes. |
| `src/instrument.h` | ~200 | **Instrument panel**: display settings (gain, persistence), adaptive layout, built-in 5×7 bitmap font, faceplate/grid rendering, status lamps, phosphor trace rendering, help text. |
| `tests/test_*.c` | | Unit and headless render tests. They `#include "../src/retroscope.c"` with `main` renamed, so they exercise the real static functions. |
| `tests/audio/` | | Isolated PipeWire configuration, capture probe and `smoke.sh` for real capture tests. |
| `tools/showcase/` | | Maintainer tooling: synthetic showcase signals and real window captures for the README (`make showcase`). |

\* approximate, v0.1.0.

The two headers are **not self-contained**: they are included once, in a fixed order, from
`retroscope.c` and rely on symbols defined before them (`CAPACITY`, `SAMPLE_RATE`, `clamp`,
`project`). They are best read as sections of one file, not as reusable modules.

## Threads and data flow

```text
                 ┌─────────────── audio thread (pa_threaded_mainloop) ───────────────┐
pipewire-pulse ─►│ context_state → server_info → sink_info(default sink)              │
 (local socket)  │   → pa_stream_connect_record(<sink>.monitor, DONT_MOVE, 10 ms frags) │
                 │ capture(): pa_stream_peek → buffer_samples() → pa_stream_drop        │
                 │   NaN/Inf and holes → 0; ring buffer Audio.samples[2048][2]          │
                 └──────────────────────────────┬────────────────────────────────────┘
                                                │ pa_threaded_mainloop_lock (copy + reset count)
                 ┌────────────── main thread (SDL, ≈59 fps cap) ───────────────────────┐
                 │ events: keys → adjust_setting / select_mode / select_timebase       │
                 │ resize → instrument_layout → recreate face + trail textures          │
                 │ scope_update(): WAVE history, SIGNAL/CLIP hold timers                │
                 │ signal_trace(): exponential fade of trail texture, glow + core line  │
                 │ compose: face texture → trail texture (additive) → status lamps      │
                 └─────────────────────────────────────────────────────────────────────┘
```

- **Shared state** is only the `Audio` struct. The audio thread writes it inside libpulse
  callbacks (which run with the mainloop lock held); the main thread copies and resets it
  under `pa_threaded_mainloop_lock`. No other synchronization exists or is needed.
- **Backpressure:** the ring buffer holds 2048 frames (~43 ms). If the main thread stalls,
  older frames are overwritten; nothing grows.
- **Demo mode** skips `audio_open` entirely and synthesizes 800 frames per rendered frame
  with `demo_signal()`, driven by a running sample position (not wall-clock time) so that
  blocks join without phase jumps: L = 0.75 sin(2π·220·t),
  R = 0.75 sin(2π·220·t + 0.9 sin(0.8 t)).

## Capture lifecycle

1. `pa_context_connect(NULL, PA_CONTEXT_NOAUTOSPAWN)` — uses the session default server, never
   spawns a daemon.
2. On `READY`: query server info → default sink name → sink info → `monitor_source_name`.
3. Record stream: float32 native-endian, 48 kHz, explicit stereo channel map, `maxlength`
   0.8 s, `fragsize` 10 ms, flags `PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE`.
4. `audio_open` polls for up to 5 s (interruptible by SIGINT/SIGTERM) for ready or failure.
5. Any later stream/context failure sets `Audio.failed`; the main loop prints the message and
   exits with status 1. There is no reconnect.
6. `audio_close`: stop mainloop → disconnect/unref stream → disconnect/unref context → free
   mainloop. Called on every exit path.

## Rendering

- Two render-target textures: **face** (window size; panel, grid, labels — redrawn only on
  resize or setting change) and **trail** (CRT size; additive blend). Each frame the trail is
  darkened by `fade_alpha(elapsed)` = 255·(1 − e^(−elapsed/τ)), so persistence is
  frame-rate independent.
- The trace is drawn as a bright core polyline plus two offset, dim polylines along the local
  normal (glow). Coordinates are clamped to the measuring area; samples are pre-sanitized.
- Prefers SDL's accelerated renderer with render targets, falls back to the software
  renderer, and refuses to run without render-target support.
- Layout scales with HiDPI (`dpi` = output pixels / logical width), switches to a compact
  panel below 400×260 logical pixels, and keeps the CRT at least 1×1 px.

## Assessment of the single-file structure

The central file mixes capture, CLI, lifecycle and the demo generator. For the current size
this is readable and keeps the build trivial (one compiler call, no link order issues), and
the pure logic is already separated into `scope.h`. A refactor into `audio.c`, `scope.c`,
`panel.c` with self-contained headers would mainly pay off when adding features such as
reconnect, source selection or translations. It was **not** done for v0.1.0 to avoid
behavioral risk in a release-preparation step.
