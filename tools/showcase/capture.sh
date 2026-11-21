#!/usr/bin/env bash
# Capture real window screenshots of the running RetroScope app (maintainers only).
#
# Runs the normal ./retroscope binary on the Hyprland desktop, but points its audio
# connection at a private, throwaway PipeWire instance (same setup as `make audio-smoke`)
# whose default sink is a silent null sink. tools/showcase/signal.c plays a synthetic
# stereo signal into that sink; RetroScope captures the sink monitor exactly as in live
# use. Nothing is audible and the desktop audio session is not touched.
# Needs: Hyprland 0.56+ (Lua dispatchers), grim >= 1.5 (-T toplevel capture), pipewire, wireplumber,
# pipewire-pulse, pacat, ImageMagick, ffmpeg.
# Output: docs/media/{vector,wave,xy}.png and docs/media/demo.gif (raw frames in build/showcase).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
AUDIO=$ROOT/tests/audio
OUT=$ROOT/build/showcase
SIZE=${SHOWCASE_SIZE:-760}          # logical window size
SETTLE=${SHOWCASE_SETTLE:-2.5}      # seconds of signal before each capture
CLIP_SIZE=480 CLIP_FPS=20 CLIP_SECONDS=4  # demo clip: logical size, frame rate, seconds per mode
for tool in hyprctl grim pipewire wireplumber pipewire-pulse pactl pacat magick ffmpeg python3; do
    command -v "$tool" >/dev/null || { echo "missing: $tool"; exit 2; }
done
[ -x "$ROOT/retroscope" ] && [ -x "$ROOT/build/showcase_signal" ] || { echo "Run: make showcase"; exit 2; }
mkdir -p "$OUT"

RT=$(mktemp -d /tmp/rs-show.XXXXXX) || exit 2
chmod 700 "$RT"
PIDS=()
cleanup() {
    for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null; done
    wait 2>/dev/null
    [ -n "$RT" ] && [ -d "$RT" ] && rm -rf -- "$RT"
}
trap cleanup EXIT
mkdir -p "$RT/config/wireplumber/wireplumber.conf.d" "$RT/state" "$RT/data"
cp "$AUDIO/wireplumber-isolated.conf" "$RT/config/wireplumber/wireplumber.conf.d/90-isolated.conf"
# Private audio stack only. Keep WAYLAND_DISPLAY so the window opens on the desktop.
AENV=(XDG_RUNTIME_DIR="$RT" PIPEWIRE_RUNTIME_DIR="$RT" PIPEWIRE_REMOTE=pipewire-0
      XDG_CONFIG_HOME="$RT/config" XDG_STATE_HOME="$RT/state" XDG_DATA_HOME="$RT/data"
      PULSE_SERVER="unix:$RT/pulse/native" PULSE_RUNTIME_PATH="$RT/pulse" PULSE_COOKIE="$RT/cookie"
      DBUS_SESSION_BUS_ADDRESS=disabled:)
WL_SOCKET="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/${WAYLAND_DISPLAY:-wayland-0}"
aenv() { env -u PULSE_SINK -u PULSE_SOURCE "${AENV[@]}" "$@"; }

aenv pipewire -c "$AUDIO/pipewire.conf" >"$RT/pipewire.log" 2>&1 & PIDS+=($!)
sleep 0.5
aenv wireplumber >"$RT/wireplumber.log" 2>&1 & PIDS+=($!)
sleep 0.5
aenv pipewire-pulse >"$RT/pipewire-pulse.log" 2>&1 & PIDS+=($!)
for _ in $(seq 50); do timeout 2 env "${AENV[@]}" pactl info >/dev/null 2>&1 && break; sleep 0.2; done
[ "$(aenv pactl get-default-sink 2>/dev/null)" = rs_sink_a ] || { echo "private audio server failed"; exit 1; }

client() { # print "<address> <stableId>" of the RetroScope window with pid $1
    hyprctl clients -j | python3 -I -c 'import json,sys
for c in json.load(sys.stdin):
    if c["pid"] == int(sys.argv[1]): print(c["address"], c["stableId"])' "$1"
}

dsp() { [ "$(hyprctl dispatch "$1")" = ok ] || { echo "hyprctl dispatch failed: $1"; return 1; }; }

shoot() { # mode key, signal name, output basename
    local key=$1 sig=$2 name=$3 app play win addr sid
    "$ROOT/build/showcase_signal" "$sig" 30 | aenv pacat --playback --device=rs_sink_a \
        --format=float32le --rate=48000 --channels=2 --raw & play=$!
    # The app connects to the private server; the Wayland socket is passed explicitly
    # because XDG_RUNTIME_DIR now points to the private directory.
    env -u PULSE_SINK -u PULSE_SOURCE "${AENV[@]}" WAYLAND_DISPLAY="$WL_SOCKET" "$ROOT/retroscope" --seconds 20 >"$RT/app-$name.log" 2>&1 & app=$!
    win=
    for _ in $(seq 50); do win=$(client "$app"); [ -n "$win" ] && break; sleep 0.1; done
    [ -n "$win" ] || { echo "no window for $name"; cat "$RT/app-$name.log"; kill $app $play; return 1; }
    read -r addr sid <<<"$win"
    local W="window = \"address:$addr\""
    dsp "hl.dsp.window.set_prop({ prop = \"opaque\", value = \"1\", $W })"
    dsp "hl.dsp.window.float({ action = \"on\", $W })"
    dsp "hl.dsp.window.resize({ x = $SIZE, y = $SIZE, relative = false, $W })"
    dsp "hl.dsp.window.center({ $W })"
    sleep 0.5
    [ "$key" = 1 ] || dsp "hl.dsp.send_shortcut({ mods = \"\", key = \"$key\", $W })"
    sleep "$SETTLE"
    grim -T "$sid" "$OUT/$name-raw.png" || echo "grim failed for $name"
    kill -INT $app; wait $app; kill $play 2>/dev/null; wait $play 2>/dev/null
    grep -h 'RetroScope:' "$RT/app-$name.log"
}

record_demo() { # real window recording of ./retroscope --demo, switching 1 -> 2 -> 3
    local app win addr sid n=0 next now
    rm -rf "$OUT/clip" && mkdir -p "$OUT/clip"
    env -u PULSE_SINK -u PULSE_SOURCE "${AENV[@]}" WAYLAND_DISPLAY="$WL_SOCKET" "$ROOT/retroscope" --demo --seconds 30 >"$RT/app-demo.log" 2>&1 & app=$!
    for _ in $(seq 50); do win=$(client "$app"); [ -n "$win" ] && break; sleep 0.1; done
    [ -n "$win" ] || { echo "no window for demo"; kill $app; return 1; }
    read -r addr sid <<<"$win"
    local W="window = \"address:$addr\""
    dsp "hl.dsp.window.set_prop({ prop = \"opaque\", value = \"1\", $W })"
    dsp "hl.dsp.window.float({ action = \"on\", $W })"
    dsp "hl.dsp.window.resize({ x = $CLIP_SIZE, y = $CLIP_SIZE, relative = false, $W })"
    dsp "hl.dsp.window.center({ $W })"
    sleep 1.5
    next=$(date +%s%N)
    for key in 1 2 3; do
        [ "$key" = 1 ] || dsp "hl.dsp.send_shortcut({ mods = \"\", key = \"$key\", $W })"
        for _ in $(seq $((CLIP_FPS * CLIP_SECONDS))); do
            grim -t ppm -T "$sid" "$OUT/clip/$(printf %04d $n).ppm" || return 1
            n=$((n+1)); next=$((next + 1000000000 / CLIP_FPS)); now=$(date +%s%N)
            [ "$now" -lt "$next" ] && sleep "0.$(printf %09d $((next - now)))"
        done
    done
    kill -INT $app; wait $app
    echo "recorded $n frames"
}

shoot 1 vector vector
shoot 2 wave wave
shoot 3 xy xy
record_demo

MEDIA=$ROOT/docs/media
for m in vector wave xy; do magick "$OUT/$m-raw.png" -strip "PNG24:$MEDIA/$m.png" || exit 1; done
ffmpeg -loglevel error -y -framerate "$CLIP_FPS" -i "$OUT/clip/%04d.ppm" -vf \
    "fps=15,scale=480:480:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    -loop 0 -bitexact "$MEDIA/demo.gif" || exit 1
ls -l "$MEDIA"
