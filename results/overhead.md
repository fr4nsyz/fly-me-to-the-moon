# Instrumentation overhead

Measured cost of the LLVM pass and the trace writer, on the `sum_plain`
reduction workload. Regenerate with `tools/study/overhead.py`.

## Result

| Measurement | Plain | Instrumented | Ratio |
| --- | ---: | ---: | ---: |
| Median wall time | 3.18 ms | 12.39 ms | 3.9x |
| Minimum wall time | 2.68 ms | 11.64 ms | 4.3x |

- Trace size: 164021 bytes (160.2 KiB)
- Events recorded: 4096
- Bytes per event: 40.0

## How this was measured

`tools/instrument.sh` pins the flag set (`-O2 -gline-tables-only
-ffp-contract=off -fno-vectorize -fno-slp-vectorize`), so the plain build
uses the same ones:

```bash
./tools/instrument.sh workloads/reductions/sum_plain.cpp -o build/sum_plain_instr

SDC_TRACE=build/clean.sdct ./build/sum_plain_instr 1024

/opt/homebrew/bin/python3.14 tools/study/overhead.py \
  --plain build/sum_plain_plain --instrumented build/sum_plain_instr \
  --repeats 9 --trace build/clean.sdct 1024
```

Measured on arm64-apple-darwin, Homebrew LLVM 23.1.4, Python 3.14.

## Reading the numbers

The bytes-per-event figure is exact and stable: the trace format is fixed, so
every event is 40 bytes.

The wall-time ratio is a median over 9 repeats of a 3 ms workload, so it is
noisy. Repeated runs of the same command on the same machine gave ratios
between 3.9x and 4.2x. Treat the ratio as "roughly 4x", not as a precise
figure.

## What the overhead is

Each instrumented operation costs one call into the runtime, which records the
event and returns the delivered result. The trace write dominates: 4096 events
times 40 bytes is the bulk of the instrumented run's extra work.
