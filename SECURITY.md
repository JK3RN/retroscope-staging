# Security and privacy

RetroScope is a local desktop program for a single user. It reads audio from the user's own
audio session and draws it in a window. This page describes what it does with that audio and
how that was checked for version 0.1.0.

## Audio

| Topic | Behaviour | How it is known |
| --- | --- | --- |
| Source | The monitor of the **default output** at startup (`<output>.monitor`), i.e. a copy of what is being played. The default *input* (usually a microphone) is never consulted. | Code; `make audio-smoke` sets a fake microphone as default input and verifies that RetroScope records the output monitor |
| Microphone | Not used and cannot be selected: the source name is taken only from the output's `monitor_source_name`, and the stream is created with `PA_STREAM_DONT_MOVE`, so the server or a policy cannot move it to another source. | Code and test |
| Passive | One record stream on a monitor. No playback stream, no volume, routing or module changes. | Code; test checks that no playback stream exists |
| Output changes | The stream stays on the output chosen at startup. If that output or the audio server disappears, RetroScope prints a message and exits with status 1 (no hang, no automatic reconnect). | Tests for output switch, output removal, server loss and missing server |
| Storage | None. Samples live in a fixed 2048-frame ring buffer (about 43 ms) and an 80 ms display history in memory. RetroScope's code opens no files. | Code review of all file, process and socket calls |
| Network | No networking code. libpulse talks to the local audio server socket of the session. | Code review |
| Remote servers | libpulse honours `PULSE_SERVER` and its client configuration. If a user points these at a remote server, audio is exchanged with that server; RetroScope never does this by itself. | libpulse behaviour |

## Permissions

RetroScope runs as an ordinary user and needs access to that user's own PipeWire/PulseAudio
session. It needs no root rights, setuid bits, capabilities, device nodes or desktop portals,
and it spawns no processes.

## Input handling

Command-line options are parsed strictly (`--seconds` accepts 1 to 86400 and rejects anything
else). Keyboard input only changes bounded settings. Audio samples are treated as untrusted:
NaN and infinite values are replaced by silence and every drawn coordinate is clamped. The
test suites run under AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer.

## Build and test scripts

`make install` copies one file; `make uninstall` removes exactly that file. The audio smoke
test and the screenshot tooling start a private PipeWire instance in a fresh `mktemp`
directory with hardware access disabled, stop only the processes they started and remove only
their own directory. Nothing is downloaded during build or test.

## Reporting a vulnerability

Please report suspected vulnerabilities privately through the repository's private
vulnerability reporting ("Security" tab) once it is enabled, rather than in a public issue.
Include the RetroScope version, your distribution and audio stack, and the steps to
reproduce. Please do not attach audio recordings or other personal data.
