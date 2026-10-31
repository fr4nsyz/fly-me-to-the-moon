#!/usr/bin/env bash
# Builds an instrumented binary from a C++ source file.
#
#   ./tools/instrument.sh workloads/reductions/sum.cpp -o build/sum_instr
#
# The workload is compiled to bitcode with the canonical flag set (ADR-0002),
# the LLVM pass rewrites every double-precision FP op into a runtime hook,
# and the result is linked against the SDC runtime.
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

SRC=""
OUT=""
WORKLOAD_NAME=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o) OUT="$2"; shift 2 ;;
    --name) WORKLOAD_NAME="$2"; shift 2 ;;
    -h|--help)
      echo "usage: $0 <source.cpp> -o <output> [--name <workload-name>]"
      exit 0 ;;
    *) SRC="$1"; shift ;;
  esac
done

if [[ -z "$SRC" || -z "$OUT" ]]; then
  echo "usage: $0 <source.cpp> -o <output> [--name <workload-name>]" >&2
  exit 2
fi

if [[ -z "$WORKLOAD_NAME" ]]; then
  WORKLOAD_NAME="$(basename "$SRC" .cpp)"
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

"$CLANG" "${CANONICAL_FLAGS[@]}" -I"$HERE/include" -I"$HERE/src/runtime" \
  -emit-llvm -c "$SRC" -o "$BC"

# The pass writes ops.json into the current directory; run it from the build dir.
(
  cd "$BUILD_DIR"
  "$OPT" -load-pass-plugin="$PLUGIN" \
         -passes=sdc-instrument "$BC" -o "$INST_BC"
)

CANONICAL_FLAG_STRING="${CANONICAL_FLAGS[*]}"

"$CLANG" "$INST_BC" \
  -L"$HERE/build/src/runtime" -L"$HERE/build/src/core" \
  -lsdc_runtime -lsdc_core \
  -o "$OUT_ABS"

cp "$BUILD_DIR/ops.json" "$OUT_DIR/ops.json"

# A build id derived from the instrumented bitcode, so the analyzer can reject a
# clean/injected pair that came from different builds (ADR-0002).
BUILD_ID="$(shasum -a 256 "$INST_BC" | cut -c1-16)"

echo "built $OUT_ABS"
echo "ops:  $OUT_DIR/ops.json"
echo "run:  SDC_WORKLOAD=$WORKLOAD_NAME SDC_BUILD_ID=$BUILD_ID SDC_COMPILER_FLAGS='$CANONICAL_FLAG_STRING' SDC_TRACE=clean.sdct $OUT_ABS"
