#!/usr/bin/env bash
# Regenerates web/public/data from the native instrumented lander and the stored sweeps. The output is committed.
set -euo pipefail

WEB="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$WEB/.." && pwd)"
BINARY="$ROOT/build/moon/moon_instr"
PYTHON="${PYTHON:-python3}"

if [[ ! -x "$BINARY" ]]; then
  echo "prepare.sh: $BINARY not found; build it with tools/instrument.sh (see README)" >&2
  exit 1
fi

rm -rf "$WEB/public/data"
"$PYTHON" "$ROOT/tools/scene_data.py" --binary "$BINARY" \
  --feature "$ROOT/results/sweep_moon_persistent_none:35" \
  --feature "$ROOT/results/sweep_moon_persistent_tmr_lockstep:35" \
  --feature "$ROOT/results/sweep_moon_persistent_none:0" \
  --out "$WEB/public/data"
