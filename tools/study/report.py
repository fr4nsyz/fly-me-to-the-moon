#!/usr/bin/env python3
"""Aggregate sweep summaries into a comparison table.

Each sweep writes a summary.json. This reads several of them and prints the
outcome totals side by side, which is how the Phase 6 defense table is built:
one column per defense mode, one row per outcome class.

Usage:
    ./tools/study/report.py results/sweep_none results/sweep_lockstep ...
    ./tools/study/report.py --markdown results/sweep_*
"""

import argparse
import json
import os
import sys

CLASSES = ("CRASH", "CORRECTED", "DETECTED", "SDC", "MASKED", "INCONCLUSIVE")


def load(summary_dir):
    path = os.path.join(summary_dir, "summary.json")
    if not os.path.exists(path):
        raise SystemExit(f"{summary_dir}: no summary.json")
    with open(path) as handle:
        return os.path.basename(summary_dir.rstrip("/")), json.load(handle)


def table(rows):
    """Build a markdown table: one row per class, one column per sweep."""
    header = "| Class | " + " | ".join(name for name, _ in rows) + " |"
    divider = "| --- |" + " ---: |" * len(rows)
    lines = [header, divider]
    for cls in CLASSES:
        cells = []
        for _, summary in rows:
            count = summary.get("classifications", {}).get(cls, 0)
            total = summary.get("trials", 0)
            if total:
                cells.append(f"{count} ({100.0 * count / total:.0f}%)")
            else:
                cells.append(str(count))
        lines.append(f"| {cls} | " + " | ".join(cells) + " |")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summaries", nargs="+", help="sweep output directories")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()

    rows = [load(path) for path in args.summaries]

    if args.markdown:
        for line in table(rows):
            print(line)
        return

    for name, summary in rows:
        total = summary.get("trials", 0)
        print(f"\n{name}  (workload={summary.get('workload')}, "
              f"seed={summary.get('seed')}, trials={total})")
        for line in table([(name, summary)]):
            print(line)

        regions = summary.get("by_bit_region", {})
        if regions:
            print("  by bit region:")
            for region, counts in regions.items():
                nonzero = {k: v for k, v in counts.items() if v}
                print(f"    {region:<14} {nonzero}")


if __name__ == "__main__":
    main()
