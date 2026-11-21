#!/usr/bin/env bash
# RetroScope audio smoke test against a private, throwaway PipeWire stack.
#
# Starts pipewire + wireplumber + pipewire-pulse in a fresh runtime directory with
# hardware monitors disabled: two null sinks (rs_sink_a is the default sink) and a
# fake microphone (rs_fake_mic is the default *source*). Nothing is played on real
# hardware and the desktop audio session is never contacted (PULSE_SERVER and
# XDG_RUNTIME_DIR point to the private instance only).
#
# Usage: make audio-smoke   (or: RETROSCOPE_BIN=... PROBE_BIN=... tests/audio/smoke.sh)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${RETROSCOPE_BIN:-$ROOT/retroscope}
PROBE=${PROBE_BIN:-$ROOT/build/capture_probe}
for tool in pipewire wireplumber pipewire-pulse pactl pacat; do
    command -v "$tool" >/dev/null || { echo "SKIP: $tool not installed"; exit 77; }
done
[ -x "$BIN" ] && [ -x "$PROBE" ] || { echo "Build first: make retroscope build/capture_probe"; exit 2; }

# Unix socket paths are limited to 108 bytes, so keep the runtime dir short.
RT=$(mktemp -d "${RETROSCOPE_SMOKE_TMP:-/tmp}/rs-audio.XXXXXX") || exit 2
chmod 700 "$RT"
PIDS=()
cleanup() {
    for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null; done
    wait 2>/dev/null
    [ -n "$RT" ] && [ -d "$RT" ] && rm -rf -- "$RT"
}
trap cleanup EXIT
mkdir -p "$RT/config/wireplumber/wireplumber.conf.d" "$RT/state" "$RT/data"
cp "$HERE/wireplumber-isolated.conf" "$RT/config/wireplumber/wireplumber.conf.d/90-isolated.conf"
export XDG_RUNTIME_DIR="$RT" PIPEWIRE_RUNTIME_DIR="$RT" PIPEWIRE_REMOTE=pipewire-0
export XDG_CONFIG_HOME="$RT/config" XDG_STATE_HOME="$RT/state" XDG_DATA_HOME="$RT/data"
export PULSE_SERVER="unix:$RT/pulse/native" PULSE_RUNTIME_PATH="$RT/pulse" PULSE_COOKIE="$RT/cookie"
export DBUS_SESSION_BUS_ADDRESS=disabled: SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
unset WAYLAND_DISPLAY DISPLAY PULSE_SINK PULSE_SOURCE

start_server() {
    pipewire -c "$HERE/pipewire.conf" >"$RT/pipewire.log" 2>&1 & PIDS+=($!)
    sleep 0.5
    wireplumber >"$RT/wireplumber.log" 2>&1 & PIDS+=($!)
    sleep 0.5
    pipewire-pulse >"$RT/pipewire-pulse.log" 2>&1 & PULSE_PID=$!; PIDS+=($PULSE_PID)
    for _ in $(seq 50); do timeout 2 pactl info >/dev/null 2>&1 && return 0; sleep 0.2; done
    echo "FAIL: private audio server did not start"; tail -n 5 "$RT"/*.log; exit 1
}
FAILS=0
pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; FAILS=$((FAILS+1)); }
in_range() { awk -v v="$1" -v lo="$2" -v hi="$3" 'BEGIN{exit !(v>=lo && v<=hi)}'; }
field() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" <<<"$2"; }
source_output_target() { # name of the source RetroScope records from
    local idx; idx=$(pactl list short source-outputs | awk 'NR==1{print $2}')
    [ -n "$idx" ] && pactl list short sources | awk -v i="$idx" '$1==i{print $2}'
}

start_server
echo "Private server: $(pactl info | sed -n 's/^Server Name: //p'); default sink $(pactl get-default-sink); default source $(pactl get-default-source)"

# 1. Real capture of a known synthetic stereo tone through the default sink monitor.
"$PROBE" tone 4 | pacat --playback --device=rs_sink_a --format=float32le --rate=48000 --channels=2 --raw &
PLAY=$!; sleep 0.5
out=$("$PROBE" capture 2.5 2>"$RT/probe.err"); rc=$?
wait $PLAY
echo "      probe: $out"
if [ $rc -eq 0 ] && in_range "$(field frames "$out")" 96000 144000 \
   && in_range "$(field rms_l "$out")" 0.33 0.38 && in_range "$(field rms_r "$out")" 0.16 0.19 \
   && in_range "$(field corr "$out")" -1.01 -0.98 && [ "$(field nonfinite "$out")" = 0 ]; then
    pass "capture: L/R levels, channel order and polarity match the synthetic tone"
else fail "capture (rc=$rc): $(cat "$RT/probe.err")"; fi
grep -q 'Monitor rs_sink_a.monitor' "$RT/probe.err" && pass "capture source is the default sink monitor" \
    || fail "unexpected capture source: $(cat "$RT/probe.err")"

# 2. Passive monitor only: never the default source (fake mic), even while running.
"$BIN" --seconds 4 >"$RT/app.log" 2>&1 & APP=$!; sleep 1.5
target=$(source_output_target)
[ "$target" = rs_sink_a.monitor ] && pass "app records rs_sink_a.monitor, not default source rs_fake_mic" \
    || fail "app record target: '${target:-none}'"
[ "$(pactl list short sink-inputs | wc -l)" = 0 ] && pass "app creates no playback stream" || fail "playback stream found"

# 3. Default sink change while running: stream stays on the original sink (PA_STREAM_DONT_MOVE).
pactl set-default-sink rs_sink_b; sleep 1
target=$(source_output_target)
[ "$target" = rs_sink_a.monitor ] && kill -0 $APP 2>/dev/null && pass "default-sink change: keeps running on original monitor" \
    || fail "after default-sink change: target '${target:-none}'"
wait $APP; rc=$?
[ $rc -eq 0 ] && pass "silence/idle run ends normally (exit 0)" || fail "idle run exit $rc: $(cat "$RT/app.log")"
pactl set-default-sink rs_sink_a

# 4. Removal of the captured sink: clear error, no hang.
mod=$(pactl load-module module-null-sink sink_name=rs_tmp channels=2 rate=48000)
pactl set-default-sink rs_tmp; sleep 0.5
"$BIN" --seconds 10 >"$RT/app.log" 2>&1 & APP=$!; sleep 1.5
t0=$(date +%s.%N); pactl unload-module "$mod"; wait $APP; rc=$?; t1=$(date +%s.%N)
dt=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
if [ $rc -eq 1 ] && in_range "$dt" 0 3 && grep -q 'getrennt' "$RT/app.log"; then
    pass "sink removed: exit 1 after ${dt}s with '$(tail -n 1 "$RT/app.log")'"
else fail "sink removal: rc=$rc after ${dt}s: $(cat "$RT/app.log")"; fi
pactl set-default-sink rs_sink_a

# 5. SIGTERM during capture: clean exit.
"$BIN" --seconds 10 >"$RT/app.log" 2>&1 & APP=$!; sleep 1.5
kill -TERM $APP; wait $APP; rc=$?
[ $rc -eq 0 ] && pass "SIGTERM during capture: exit 0" || fail "SIGTERM exit $rc"

# 6. Repeated start/stop of the capture lifecycle.
ok=0
for _ in $(seq 15); do "$BIN" --seconds 1 >/dev/null 2>&1 && ok=$((ok+1)); done
[ $ok -eq 15 ] && pass "15 consecutive capture start/stop cycles: all exit 0" || fail "start/stop cycles: $ok/15"
[ "$(pactl list short source-outputs | wc -l)" = 0 ] && pass "no leftover record streams" || fail "leftover record streams"

# 7. Audio server disappears while capturing: clear error, no hang.
"$BIN" --seconds 10 >"$RT/app.log" 2>&1 & APP=$!; sleep 1.5
kill "$PULSE_PID"; t0=$(date +%s.%N); wait $APP; rc=$?; t1=$(date +%s.%N)
dt=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
if [ $rc -eq 1 ] && in_range "$dt" 0 3; then pass "server lost: exit 1 after ${dt}s with '$(tail -n 1 "$RT/app.log")'"
else fail "server loss: rc=$rc after ${dt}s: $(cat "$RT/app.log")"; fi

# 8. No server at all: clear error, no hang.
t0=$(date +%s.%N); "$BIN" --seconds 2 >"$RT/app.log" 2>&1; rc=$?; t1=$(date +%s.%N)
dt=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
[ $rc -eq 1 ] && in_range "$dt" 0 6 && pass "no server: exit 1 after ${dt}s with '$(tail -n 1 "$RT/app.log")'" \
    || fail "no server: rc=$rc after ${dt}s"

echo "audio-smoke: $FAILS failure(s)"
exit $((FAILS>0))
