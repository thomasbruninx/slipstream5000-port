#!/bin/bash
# Multi-process multiplayer smoke test: one host and N-1 joiners race headless (autopilot) on this machine and must agree on laps, ranks and
# the race end. Usage: tools/net_race_test.sh [players=4] [track=2] [laps=1] [binary=build/bin/slipstream]
# Optional: SLIP_FIRE=1 lets every human fire its weapons (exercises projectile / hit replication), KILL=1 kills one joiner mid-race, KILLHOST=1 kills the host (host migration).
set -u
N=${1:-4}; TRACK=${2:-2}; LAPS=${3:-1}; BIN=${4:-build/bin/slipstream}
DIR=$(mktemp -d /tmp/slip_net.XXXXXX)
PORT=$((52000 + RANDOM % 2000))
"$BIN" --host --players "$N" --track "$TRACK" --laps "$LAPS" --port "$PORT" --name P0 --net-run 400 >"$DIR/0.log" 2>"$DIR/0.err" &
HOSTPID=$!
sleep 1
PIDS=()
for ((i = 1; i < N; i++)); do
  "$BIN" --join "127.0.0.1:$PORT" --name "P$i" --net-run 400 >"$DIR/$i.log" 2>"$DIR/$i.err" &
  PIDS+=($!)
done
if [ "${KILL:-0}" = 1 ] && [ "$N" -gt 2 ]; then sleep 15; kill "${PIDS[0]}" 2>/dev/null; fi
if [ "${KILLHOST:-0}" = 1 ]; then sleep 15; kill "$HOSTPID" 2>/dev/null; fi  # host migration: the lowest remaining player takes over
wait
fail=0
ref=""
for ((i = 0; i < N; i++)); do
  [ "${KILL:-0}" = 1 ] && [ "$i" = 1 ] && continue
  [ "${KILLHOST:-0}" = 1 ] && [ "$i" = 0 ] && continue
  norm=$(grep '^slot' "$DIR/$i.log" | awk '{print $2, $6, $8, $10, $12}')
  [ -z "$norm" ] && { echo "player $i: no result"; fail=1; continue; }
  if [ -z "$ref" ]; then ref="$norm"; elif [ "$norm" != "$ref" ]; then echo "player $i disagrees:"; diff <(echo "$ref") <(echo "$norm"); fail=1; fi
done
[ $fail = 0 ] && echo "net race test OK ($N players, track $TRACK): all peers agree" && grep '^slot' "$DIR/0.log"
echo "logs in $DIR"
exit $fail
