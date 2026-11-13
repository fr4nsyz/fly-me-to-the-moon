#!/usr/bin/env python3
"""Write the web display's data: each defense's clean landing and the featured trials, re-flown from their sweeps.

Usage:
    tools/scene_data.py --binary build/moon/moon_instr \\
        --feature results/sweep_moon_persistent_none:35 [--feature <sweep_dir>:<trial> ...] --out web/public/data
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import tempfile

import sdctrace

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORES = 6
RUN_TIMEOUT = 600
# Must equal kDt in workloads/moon/sim/main.cpp.
DT = 0.1


def fly(binary, env_extra, args, trajectory_path, trace_path):
    env = dict(os.environ)
    env.update({"SDC_TRACE": trace_path, "SDC_WORKLOAD": "moon"})
    env.update(env_extra)
    try:
        result = subprocess.run(
            [binary] + args + ["--trajectory", trajectory_path],
            env=env, capture_output=True, text=True, timeout=RUN_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        raise SystemExit(f"scene_data: {binary} did not finish within {RUN_TIMEOUT} s")
    with open(trajectory_path) as handle:
        return result.stdout, json.load(handle)


def demangle(name):
    # Replicated cores carry a ".N" suffix that the demangler rejects.
    base = re.sub(r"\.\d+$", "", name)
    tools = [shutil.which("llvm-cxxfilt"), "/opt/homebrew/opt/llvm/bin/llvm-cxxfilt", shutil.which("c++filt")]
    for tool in filter(None, tools):
        if not os.access(tool, os.X_OK):
            continue
        # Apple's c++filt expects the Mach-O extra leading underscore.
        for symbol in (base, "_" + base):
            out = subprocess.run([tool, symbol], capture_output=True, text=True).stdout.strip()
            if out and out != symbol:
                return out
    return base


def load_ops(binary):
    with open(os.path.join(os.path.dirname(os.path.abspath(binary)), "ops.json")) as handle:
        return json.load(handle)


def load_trial(args):
    """The trial's evidence file, or an equivalent built from summary.json and ops.json."""
    path = os.path.join(args.sweep, f"trial_{args.trial:04d}.json")
    if os.path.exists(path):
        with open(path) as handle:
            return json.load(handle)
    with open(os.path.join(args.sweep, "summary.json")) as handle:
        summary = json.load(handle)
    record = next((r for r in summary["records"] if r["trial"] == args.trial), None)
    if record is None:
        raise SystemExit(f"scene_data: trial {args.trial} is not in {args.sweep}/summary.json")
    ops = load_ops(args.binary)
    return {
        "classification": record["classification"],
        "reasons": record.get("reasons", []),
        "inject": {
            "op_id": record["op_id"],
            "occurrence": record["occurrence"],
            "bit": record["bit"],
            "persistent": summary.get("persistent", False),
        },
        "injected_event": {"source": ops.get(str(record["op_id"]), {})},
    }


def sweep_defense(args):
    """The defense mode the sweep ran with; a mismatched --defense would re-fly a fault that never executes."""
    with open(os.path.join(args.sweep, "summary.json")) as handle:
        workload_args = json.load(handle).get("workload_args", [])
    mode = workload_args[workload_args.index("--defense") + 1] if "--defense" in workload_args else "none"
    if args.defense is not None and args.defense != mode:
        raise SystemExit(f"scene_data: {args.sweep} was run with --defense {mode}, not {args.defense}")
    return mode


def injection(evidence):
    inject = evidence.get("inject", {})
    if not inject.get("enabled", True) or inject.get("op_id") is None:
        raise SystemExit("scene_data: the evidence has no injection site")
    return inject


def fly_clean(binary, defense, tmp):
    """Fly the clean landing; returns (trajectory, spot, trace path) where spot is the faulty run's --reference."""
    trace = os.path.join(tmp, f"clean_{defense}.sdct")
    stdout, clean = fly(binary, {}, ["--defense", defense], os.path.join(tmp, f"clean_{defense}.json"), trace)
    match = re.search(r"spot=(\S+)", stdout)
    if not match:
        raise SystemExit(f"scene_data: the clean landing printed no spot: {stdout.strip()}")
    return clean, match.group(1), trace


def fly_faulty(binary, defense, spot, inject, path, trace_path):
    fault_env = {"SDC_INJECT": f"op={inject['op_id']},occ={inject['occurrence']},bit={inject['bit']}"}
    if inject.get("persistent"):
        fault_env["SDC_INJECT_PERSIST"] = "1"
    _, faulty = fly(binary, fault_env, ["--defense", defense, "--reference", spot], path, trace_path)
    return faulty


def refly(args):
    """Re-fly the clean landing and the trial's faulty landing; returns (evidence, clean, faulty)."""
    evidence = load_trial(args)
    inject = injection(evidence)
    with tempfile.TemporaryDirectory() as tmp:
        clean, spot, _ = fly_clean(args.binary, args.defense, tmp)
        faulty = fly_faulty(args.binary, args.defense, spot, inject, os.path.join(tmp, "faulty.json"),
                            os.path.join(tmp, "faulty.sdct"))
    return evidence, clean, faulty


def steps_flown(trajectory):
    """Number of flight software steps in a landed run; touchdown falls inside the last step."""
    if trajectory["touchdown"] is None:
        raise SystemExit("scene_data: the clean landing did not touch down")
    return math.ceil(trajectory["touchdown"]["t"] / DT)


def op_runs(clean_trace, faulty_trace, op_id, steps):
    """Each faulty-run execution of op_id as [t, occurrence, operand0, operand1, result, clean result], bits in hex."""
    clean = sdctrace.read(clean_trace).events
    # The tick's first instrumented op runs once per flight software step, so its count is the step number.
    clock = clean[0].op_id
    ticks = sum(1 for e in clean if e.op_id == clock)
    if ticks != steps:
        raise SystemExit(f"scene_data: clock op {clock} ran {ticks} times in the clean trace, not once per step ({steps})")
    reference = {e.occurrence: e.result for e in clean if e.op_id == op_id}
    runs, steps = [], 0
    for e in sdctrace.read(faulty_trace).events:
        if e.op_id == clock:
            steps += 1
        if e.op_id == op_id:
            expected = reference.get(e.occurrence)
            runs.append([round((steps - 1) * DT, 1), e.occurrence, f"0x{e.operand0:016x}", f"0x{e.operand1:016x}",
                         f"0x{e.result:016x}", None if expected is None else f"0x{expected:016x}"])
    return runs


def event_bits(event):
    if not event or "result" not in event:
        return None
    return {key: event[key] for key in ("operand0", "operand1", "result")}


def summarize(evidence, args):
    inject = evidence.get("inject", {})
    source = (evidence.get("injected_event") or {}).get("source", {})
    op_id = inject.get("op_id", 0)
    # The copies are the only instrumented code and are numbered in core order, so op ids form equal blocks.
    ops_per_core = len(load_ops(args.binary)) // CORES
    core = (op_id - 1) // ops_per_core if op_id > 0 else None
    return {
        "classification": evidence.get("classification", "UNKNOWN"),
        "reasons": evidence.get("reasons", []),
        "op_id": op_id,
        "occurrence": inject.get("occurrence"),
        "bit": inject.get("bit"),
        "persistent": bool(inject.get("persistent")),
        "kind": source.get("kind"),
        "file": source.get("file"),
        "line": source.get("line"),
        "column": source.get("column"),
        "function": demangle(source["function"]) if source.get("function") else None,
        "core": core,
        "computer": core // 2 if core is not None else None,
        "injected_bits": event_bits(evidence.get("injected_event")),
        "clean_bits": event_bits(evidence.get("clean_event")),
        "defense": args.defense,
        "sweep": os.path.basename(os.path.normpath(args.sweep)),
        "trial": args.trial,
    }


def check_consistency(classification, touchdown):
    score = touchdown.get("score") if touchdown else None
    if score is None:
        score = math.inf
    if classification == "SDC" and not score > 1:
        return f"evidence says SDC but the re-flown landing scored {score} (expected > 1)"
    if classification in ("MASKED", "CORRECTED") and not score <= 1:
        return f"evidence says {classification} but the re-flown landing scored {score} (expected <= 1)"
    return None


def write(path, data):
    with open(path, "w") as handle:
        json.dump(data, handle, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--feature", action="append", required=True, metavar="SWEEP:TRIAL", help="repeatable")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    jobs = []
    for feature in args.feature:
        sweep, _, trial = feature.rpartition(":")
        mode = sweep_defense(argparse.Namespace(sweep=sweep, defense=None))
        jobs.append(argparse.Namespace(binary=args.binary, sweep=sweep, defense=mode, trial=int(trial)))

    os.makedirs(args.out, exist_ok=True)
    index = []
    with tempfile.TemporaryDirectory() as tmp:
        cleans = {mode: fly_clean(args.binary, mode, tmp) for mode in {job.defense for job in jobs}}
        for mode, (clean, _, _) in cleans.items():
            write(os.path.join(args.out, f"clean_{mode}.json"), clean)
        for job in jobs:
            evidence = load_trial(job)
            inject = injection(evidence)
            faulty_trace = os.path.join(tmp, "faulty.sdct")
            faulty = fly_faulty(args.binary, job.defense, cleans[job.defense][1], inject,
                                os.path.join(tmp, "faulty.json"), faulty_trace)
            summary = summarize(evidence, job)
            warning = check_consistency(summary["classification"], faulty["touchdown"])
            if warning:
                raise SystemExit(f"scene_data: trial {job.trial}: {warning}")
            name = f"trial_{job.trial}_{job.defense}.json"
            clean, _, clean_trace = cleans[job.defense]
            runs = op_runs(clean_trace, faulty_trace, inject["op_id"], steps_flown(clean))
            write(os.path.join(args.out, name), {"evidence": summary, "faulty": faulty, "op_runs": runs})
            index.append({"file": name, "trial": job.trial, "defense": job.defense,
                          "classification": summary["classification"]})
    write(os.path.join(args.out, "index.json"), index)
    print(f"scene_data: {len(index)} trials -> {args.out}")


if __name__ == "__main__":
    main()
