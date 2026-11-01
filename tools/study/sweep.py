#!/usr/bin/env python3
"""Fault injection campaign driver.

Runs a campaign of single-bit-fault injections against an instrumented
workload and aggregates the verdicts.

The campaign is deterministic. The same seed produces the same sequence of
injection sites, because the runtime resolves each injection with std::rand
seeded from the campaign seed, and the resolved site is written into the trace
metadata of every run.

Usage:
    ./tools/study/sweep.py --binary build/sum_plain_instr \\
        --ops build/ops.json --trials 100 --seed 1337 --out results/sweep
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import sdctrace  # noqa: E402

ANALYZER = os.path.join("build", "src", "analyzer", "sdc-analyze")

CLASSES = ("CRASH", "HANG", "CORRECTED", "DETECTED", "SDC", "MASKED", "INCONCLUSIVE")


def run_workload(binary, env_extra, args, timeout=None):
    """Run the workload; returns the CompletedProcess, or None if it timed out and was killed."""
    env = dict(os.environ)
    env.update(env_extra)
    try:
        return subprocess.run(
            [binary] + list(args),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None


def build_plan(trace_path, out_path):
    """Write the injection plan: every op that executes, and how many times.

    Only operations that actually ran are eligible. An operation the compiler
    optimized away would never be reached, and injecting into it would produce
    a run that is identical to the clean one.
    """
    trace = sdctrace.read(trace_path)
    counts = trace.occurrence_counts()
    if not counts:
        raise SystemExit(f"{trace_path}: no events recorded, nothing to inject into")

    with open(out_path, "w") as handle:
        handle.write("# op_id max_occurrence\n")
        for op_id in sorted(counts):
            handle.write(f"{op_id} {counts[op_id]}\n")

    return counts


def classify(clean_trace, injected_trace, ops_path, evidence_path, extra_args, timed_out=False):
    command = [
        ANALYZER,
        "--clean", clean_trace,
        "--injected", injected_trace,
        "--ops", ops_path,
        "--out", evidence_path,
    ]
    command += shlex.split(extra_args)
    if timed_out:
        command.append("--timed-out")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(
            f"analyzer failed on {injected_trace}: {result.stderr.strip()}"
        )
    with open(evidence_path) as handle:
        return json.load(handle)


def run_campaign(args):
    os.makedirs(args.out, exist_ok=True)

    clean_trace = os.path.join(args.out, "clean.sdct")
    plan_path = os.path.join(args.out, "inject_plan.txt")

    print(f"[sweep] clean run -> {clean_trace}")
    clean_start = time.monotonic()
    clean_result = run_workload(
        args.binary,
        {
            "SDC_TRACE": clean_trace,
            "SDC_WORKLOAD": args.workload,
            "SDC_BUILD_ID": args.build_id,
            "SDC_COMPILER_FLAGS": args.compiler_flags,
        },
        args.workload_args,
    )
    if clean_result.returncode != 0:
        raise SystemExit(f"clean run failed: {clean_result.stderr.strip()}")
    clean_seconds = time.monotonic() - clean_start
    timeout = max(args.timeout_floor, args.timeout_factor * clean_seconds)
    print(f"[sweep] clean run took {clean_seconds:.2f} s; per-trial timeout {timeout:.1f} s")

    trial_args = list(args.workload_args)
    for spec in args.forward_from_clean:
        key, flag = spec.split(":", 1)
        match = re.search(rf"(?:^|\s){re.escape(key)}=(\S+)", clean_result.stdout)
        if not match:
            raise SystemExit(f"clean run did not print {key}=...")
        trial_args += [flag, match.group(1)]
    if trial_args:
        print(f"[sweep] trial args: {' '.join(trial_args)}")

    counts = build_plan(clean_trace, plan_path)
    total_sites = sum(counts.values())
    print(
        f"[sweep] plan: {len(counts)} operations, {total_sites} injection sites "
        f"({total_sites * 64} candidate bit flips)"
    )

    baseline = sdctrace.read(clean_trace)

    def run_trial(trial):
        # The seed is the campaign seed combined with the trial index, so each
        # trial gets its own deterministic site and the whole campaign is
        # reproducible from a single number.
        seed = (args.seed + trial * 7919) & 0x7FFFFFFF

        trial_trace = os.path.join(args.out, f"trial_{trial:04d}.sdct")
        evidence_path = os.path.join(args.out, f"trial_{trial:04d}.json")

        result = run_workload(
            args.binary,
            {
                "SDC_TRACE": trial_trace,
                "SDC_INJECT": "random",
                "SDC_SEED": str(seed),
                "SDC_INJECT_PLAN": plan_path,
                "SDC_INJECT_BIT_RANGE": args.bit_range,
                "SDC_INJECT_PERSIST": "1" if args.persistent else "",
                "SDC_WORKLOAD": args.workload,
                "SDC_BUILD_ID": args.build_id,
                "SDC_COMPILER_FLAGS": args.compiler_flags,
            },
            trial_args,
            timeout=timeout,
        )
        timed_out = result is None

        if timed_out and not os.path.exists(trial_trace):
            return {"trial": trial, "seed": seed, "classification": "HANG"}, f"[sweep] trial {trial:4d}  HANG (no trace)"

        if not os.path.exists(trial_trace):
            record = {
                "trial": trial,
                "seed": seed,
                "classification": "CRASH",
                "error": result.stderr.strip()[-400:],
            }
            return record, f"[sweep] trial {trial:4d}  CRASH (no trace)"

        evidence = classify(clean_trace, trial_trace, args.ops, evidence_path, args.analyzer_args, timed_out)

        site = evidence.get("inject", {})
        record = {
            "trial": trial,
            "seed": seed,
            "classification": evidence["classification"],
            "op_id": site.get("op_id"),
            "occurrence": site.get("occurrence"),
            "bit": site.get("bit"),
            "reasons": evidence["reasons"],
            "diverged": evidence["alignment"]["diverged"],
            "final_result_outside_tolerance": evidence["final_result_outside_tolerance"],
        }

        # The trace files are large. Keep the evidence, drop the trace unless
        # the caller asked for it.
        if not args.keep_traces:
            os.remove(trial_trace)

        detail = ""
        if site.get("op_id") is not None:
            detail = f"  op={site['op_id']} occ={site['occurrence']} bit={site['bit']}"
        return record, f"[sweep] trial {trial:4d}  {record['classification']:<12}{detail}"

    # Trials are independent processes, so running them concurrently changes
    # only wall time; map() returns them in trial order.
    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for record, line in pool.map(run_trial, range(args.trials)):
            records.append(record)
            print(line, flush=True)

    write_summary(args, records, baseline, clean_trace)
    return records


def write_summary(args, records, baseline, clean_trace):
    counts = Counter(r["classification"] for r in records)
    total = len(records)

    # Group by the bit that was flipped. Bit 63 is the sign, bits 62..52 the
    # exponent, bits 51..0 the mantissa. The split is the interesting result:
    # a fault in the exponent is far more destructive than one in the low
    # mantissa.
    by_region = defaultdict(Counter)
    for record in records:
        bit = record.get("bit")
        if bit is None:
            continue
        if bit == 63:
            region = "sign"
        elif bit >= 52:
            region = "exponent"
        elif bit >= 52 - 11:
            region = "mantissa-high"
        else:
            region = "mantissa-low"
        by_region[region][record["classification"]] += 1

    by_op = defaultdict(Counter)
    for record in records:
        op_id = record.get("op_id")
        if op_id is None:
            continue
        by_op[op_id][record["classification"]] += 1

    summary = {
        "workload": args.workload,
        "build_id": args.build_id,
        "compiler_flags": args.compiler_flags,
        "seed": args.seed,
        "persistent": args.persistent,
        "trials": total,
        "bit_range": args.bit_range,
        "classifications": {name: counts.get(name, 0) for name in CLASSES},
        "by_bit_region": {
            region: {name: counter.get(name, 0) for name in CLASSES}
            for region, counter in sorted(by_region.items())
        },
        "by_op_id": {
            str(op_id): {name: counter.get(name, 0) for name in CLASSES}
            for op_id, counter in sorted(by_op.items())
        },
        "records": records,
    }

    summary_path = os.path.join(args.out, "summary.json")
    with open(summary_path, "w") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")

    write_report(args, summary, counts, total, by_region, by_op, baseline, clean_trace)
    print(f"\n[sweep] summary -> {summary_path}")
    print(f"[sweep] report  -> {os.path.join(args.out, 'report.md')}")


def write_report(args, summary, counts, total, by_region, by_op, baseline, clean_trace):
    lines = []
    lines.append(f"# Fault injection sweep: {args.workload}")
    lines.append("")
    lines.append(f"- Build id: `{args.build_id}`")
    lines.append(f"- Compiler flags: `{args.compiler_flags}`")
    lines.append(f"- Campaign seed: `{args.seed}`")
    lines.append(f"- Trials: {total}")
    lines.append(f"- Bit range: {args.bit_range}")
    lines.append(f"- Clean trace: `{clean_trace}`")
    lines.append("")

    lines.append("## Outcome totals")
    lines.append("")
    lines.append("| Class | Count | Share |")
    lines.append("| --- | ---: | ---: |")
    for name in CLASSES:
        count = counts.get(name, 0)
        share = f"{100.0 * count / total:.1f}%" if total else "-"
        lines.append(f"| {name} | {count} | {share} |")
    lines.append("")

    lines.append("## Outcome by bit region")
    lines.append("")
    lines.append(
        "Bit 63 is the sign bit, bits 62..52 the exponent, and bits 51..0 the "
        "mantissa. A fault high in the mantissa is a large relative change; a "
        "fault in the low mantissa is a rounding-sized change."
    )
    lines.append("")
    header = "| Region | " + " | ".join(CLASSES) + " |"
    lines.append(header)
    lines.append("| --- |" + " ---: |" * len(CLASSES))
    for region, counter in sorted(by_region.items()):
        cells = " | ".join(str(counter.get(name, 0)) for name in CLASSES)
        lines.append(f"| {region} | {cells} |")
    lines.append("")

    lines.append("## Outcome by operation")
    lines.append("")
    header = "| Op id | " + " | ".join(CLASSES) + " |"
    lines.append(header)
    lines.append("| --- |" + " ---: |" * len(CLASSES))
    for op_id, counter in sorted(by_op.items()):
        cells = " | ".join(str(counter.get(name, 0)) for name in CLASSES)
        lines.append(f"| {op_id} | {cells} |")
    lines.append("")

    lines.append("## Trials")
    lines.append("")
    lines.append("| Trial | Seed | Op | Occ | Bit | Class |")
    lines.append("| ---: | ---: | ---: | ---: | ---: | --- |")
    for record in summary["records"]:
        lines.append(
            f"| {record['trial']} | {record['seed']} | "
            f"{record.get('op_id', '-')} | {record.get('occurrence', '-')} | "
            f"{record.get('bit', '-')} | {record['classification']} |"
        )
    lines.append("")

    report_path = os.path.join(args.out, "report.md")
    with open(report_path, "w") as handle:
        handle.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="instrumented binary")
    parser.add_argument("--ops", required=True, help="ops.json from the pass")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--trials", type=int, default=100)
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--workload", default="workload")
    parser.add_argument("--build-id", default="unknown")
    parser.add_argument("--compiler-flags", default="unknown")
    parser.add_argument("--bit-range", default="0-63")
    parser.add_argument(
        "--jobs",
        type=int,
        default=1,
        help="trials to run at the same time; results do not depend on it",
    )
    parser.add_argument(
        "--persistent",
        action="store_true",
        help="flip the chosen bit on every execution of the op from the chosen occurrence on",
    )
    parser.add_argument(
        "--analyzer-args",
        default="",
        help="extra flags passed to sdc-analyze, as one string",
    )
    parser.add_argument(
        "--forward-from-clean",
        action="append",
        default=[],
        metavar="KEY:FLAG",
        help="pass the clean run's printed KEY=value to each trial as FLAG value",
    )
    parser.add_argument(
        "--timeout-factor",
        type=float,
        default=100.0,
        help="per-trial timeout as a multiple of the clean run's wall time",
    )
    parser.add_argument(
        "--timeout-floor",
        type=float,
        default=10.0,
        help="minimum per-trial timeout in seconds",
    )
    parser.add_argument(
        "--keep-traces",
        action="store_true",
        help="keep every trial trace on disk (they are large)",
    )
    parser.add_argument(
        "workload_args",
        nargs="*",
        help="arguments passed to the instrumented binary",
    )
    args = parser.parse_args()

    run_campaign(args)


if __name__ == "__main__":
    main()
