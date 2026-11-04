#!/usr/bin/env bash
# Writes web/public/data from the native instrumented lander and copies the
# Draco decoder from the pinned three package into web/public/draco.
set -euo pipefail

WEB="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$WEB/.." && pwd)"
BINARY="$ROOT/build/moon/moon_instr"
DRACO="$WEB/node_modules/three/examples/jsm/libs/draco/gltf"

if [[ ! -x "$BINARY" ]]; then
  echo "prepare.sh: $BINARY not found; build it with tools/instrument.sh (see README)" >&2
  exit 1
fi

mkdir -p "$WEB/public/data" "$WEB/public/draco"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

for mode in none tmr-lockstep; do
  SDC_WORKLOAD=moon SDC_TRACE="$TMP/$mode.sdct" "$BINARY" --defense "$mode" \
    --trajectory "$WEB/public/data/trajectory_$mode.json"
done

cp "$DRACO/draco_decoder.wasm" "$DRACO/draco_wasm_wrapper.js" "$WEB/public/draco/"
