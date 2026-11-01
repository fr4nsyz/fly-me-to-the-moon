#!/usr/bin/env bash
# Builds an instrumented binary.
#
#   ./tools/instrument.sh workloads/reductions/sum.cpp -o build/sum_instr
#
#   ./tools/instrument.sh --profile o0 -o build/moon_instr --keep fsw_tick \
#       -I workloads/moon/common --instrument workloads/moon/fsw/fsw.cpp \
#       --plain workloads/moon/sim/main.cpp --plain build/workloads/moon/plant/libmoon_plant.a
#
# Files given with --instrument (or as a bare argument) are compiled to bitcode
# with the profile's flag set, merged, and rewritten by the LLVM pass so every
# double-precision FP op becomes a runtime hook. Files given with --plain are
# compiled and linked normally. Everything is linked against the SDC runtime.
set -euo pipefail

LLVM_ROOT="${LLVM_ROOT:-/opt/homebrew/opt/llvm}"
CLANG="${LLVM_ROOT}/bin/clang++"
OPT="${LLVM_ROOT}/bin/opt"
LLVM_LINK="${LLVM_ROOT}/bin/llvm-link"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

CANONICAL_FLAGS=(
  -O2
  -gline-tables-only
  -ffp-contract=off
  -fno-vectorize
  -fno-slp-vectorize
)

# ADR-0017: replicas and voters must survive, so this profile does not optimize.
O0_FLAGS=(
  -O0
  -gline-tables-only
  -ffp-contract=off
)

USAGE="usage: $0 [--profile canonical|o0] [--instrument <file>]... [--plain <file>]...
          [--keep <symbol>]... [-I <dir>]... [--name <workload-name>] -o <output> [<file>]"

PROFILE="canonical"
OUT=""
WORKLOAD_NAME=""
INSTRUMENT=()
PLAIN=()
KEEP=()
INCLUDES=(-I"$HERE/include" -I"$HERE/src/runtime")

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o) OUT="$2"; shift 2 ;;
    --name) WORKLOAD_NAME="$2"; shift 2 ;;
    --profile) PROFILE="$2"; shift 2 ;;
    --instrument) INSTRUMENT+=("$2"); shift 2 ;;
    --plain) PLAIN+=("$2"); shift 2 ;;
    --keep) KEEP+=("$2"); shift 2 ;;
    -I) INCLUDES+=(-I"$2"); shift 2 ;;
    -h|--help) echo "$USAGE"; exit 0 ;;
    *) INSTRUMENT+=("$1"); shift ;;
  esac
done

if [[ ${#INSTRUMENT[@]} -eq 0 || -z "$OUT" ]]; then
  echo "$USAGE" >&2
  exit 2
fi

case "$PROFILE" in
  canonical) FLAGS=("${CANONICAL_FLAGS[@]}") ;;
  o0) FLAGS=("${O0_FLAGS[@]}") ;;
  *) echo "instrument.sh: unknown profile '$PROFILE'" >&2; exit 2 ;;
esac

if [[ -z "$WORKLOAD_NAME" ]]; then
  WORKLOAD_NAME="$(basename "${INSTRUMENT[0]}" .cpp)"
fi

OUT_DIR="$(cd "$(dirname "$OUT")" && pwd)"
OUT_ABS="$OUT_DIR/$(basename "$OUT")"
BUILD_DIR="$OUT_DIR/.sdc-instrument"
mkdir -p "$OUT_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

BC="$BUILD_DIR/workload.bc"
INST_BC="$BUILD_DIR/workload.instrumented.bc"
PLUGIN="$HERE/build/src/instrumentation/SDCInstrument.dylib"

BCS=()
for i in "${!INSTRUMENT[@]}"; do
  bc="$BUILD_DIR/part$i.bc"
  "$CLANG" -std=c++20 "${FLAGS[@]}" "${INCLUDES[@]}" -emit-llvm -c "${INSTRUMENT[$i]}" -o "$bc"
  BCS+=("$bc")
done

if [[ ${#BCS[@]} -eq 1 ]]; then
  mv "${BCS[0]}" "$BC"
else
  "$LLVM_LINK" "${BCS[@]}" -o "$BC"
fi

# The pass writes ops.json into the current directory; run it from the build dir.
(
  cd "$BUILD_DIR"
  "$OPT" -load-pass-plugin="$PLUGIN" \
         -passes=sdc-instrument "$BC" -o "$INST_BC"
)

# ADR-0018: shared headers compile to linkonce_odr functions; without this the
# linker keeps one copy for both the instrumented and the plain side.
if [[ ${#KEEP[@]} -gt 0 ]]; then
  KEEP_LIST="$(IFS=,; echo "${KEEP[*]}")"
  "$OPT" -passes=internalize -internalize-public-api-list="$KEEP_LIST" \
         "$INST_BC" -o "$INST_BC"
fi

FLAG_STRING="${FLAGS[*]}"

"$CLANG" "$INST_BC" "${PLAIN[@]+"${PLAIN[@]}"}" -std=c++20 "${INCLUDES[@]}" \
  -L"$HERE/build/src/runtime" -L"$HERE/build/src/core" \
  -lsdc_runtime -lsdc_core \
  -o "$OUT_ABS"

cp "$BUILD_DIR/ops.json" "$OUT_DIR/ops.json"

# A build id derived from the instrumented bitcode, so the analyzer can reject a
# clean/injected pair that came from different builds (ADR-0002).
BUILD_ID="$(shasum -a 256 "$INST_BC" | cut -c1-16)"

echo "built $OUT_ABS"
echo "ops:  $OUT_DIR/ops.json"
echo "run:  SDC_WORKLOAD=$WORKLOAD_NAME SDC_BUILD_ID=$BUILD_ID SDC_COMPILER_FLAGS='$FLAG_STRING' SDC_TRACE=clean.sdct $OUT_ABS"
