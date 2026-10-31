#!/usr/bin/env python3
"""Measure the cost of instrumentation.

Three numbers matter:

- Wall time of the instrumented binary against the plain binary.
- The size of the trace the instrumented binary produces.
- The number of events recorded, which is what drives the trace size.

Usage:
    ./tools/study/overhead.py --plain build/sum --instrumented build/sum_instr \\
        --binary build/sum_plain_instr --repeats 5 --args 1024
"""

import argparse
import os
import statistics
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import sdctrace  # noqa: E402


def time_run(binary, args, repeats, env=None):
    environment = dict(os.environ)
    if env:
        environment.update(env)

    samples = []
    for _ in range(repeats):
        start = time.perf_counter()
        result = subprocess.run(
            [binary] + list(args),
            env=environment,
            capture_output=True,
        )
        elapsed = time.perf_counter() - start
        if result.returncode != 0:
            raise SystemExit(f"{binary} failed: {result.stderr.decode()[-400:]}")
        samples.append(elapsed)
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plain", required=True, help="uninstrumented binary")
    parser.add_argument("--instrumented", required=True, help="instrumented binary")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--trace", default="", help="trace path to measure")
    parser.add_argument("binary_args", nargs="*")
    args = parser.parse_args()

    plain = time_run(args.plain, args.binary_args, args.repeats)
    instrumented = time_run(args.instrumented, args.binary_args, args.repeats)

    plain_median = statistics.median(plain)
    instrumented_median = statistics.median(instrumented)
    slowdown = instrumented_median / plain_median if plain_median else float("inf")

    print("## Instrumentation overhead")
    print()
    print(f"- Plain binary:        {args.plain}")
    print(f"- Instrumented binary: {args.instrumented}")
    print(f"- Repeats:             {args.repeats}")
    print()
    print("| Measurement | Plain | Instrumented | Ratio |")
    print("| --- | ---: | ---: | ---: |")
    print(f"| Median wall time | {plain_median * 1000:.2f} ms | "
          f"{instrumented_median * 1000:.2f} ms | {slowdown:.1f}x |")
    print(f"| Minimum wall time | {min(plain) * 1000:.2f} ms | "
          f"{min(instrumented) * 1000:.2f} ms | "
          f"{min(instrumented) / min(plain):.1f}x |" if plain else "")
    print()

    if args.trace and os.path.exists(args.trace):
        size = os.path.getsize(args.trace)
        trace = sdctrace.read(args.trace)
        events = trace.total_events()
        print(f"- Trace file: `{args.trace}`")
        print(f"- Trace size: {size} bytes ({size / 1024:.1f} KiB)")
        print(f"- Events recorded: {events}")
        if events:
            print(f"- Bytes per event: {size / events:.1f}")
    else:
        print("Pass --trace <path> to report the trace size and event count.")


if __name__ == "__main__":
    main()
