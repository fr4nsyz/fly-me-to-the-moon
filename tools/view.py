#!/usr/bin/env python3
"""Build a self-contained HTML descent viewer for the notable trials of one or more sweeps.

Re-flies the clean landing once per defense mode and every SDC trial plus a few
trials of each other outcome with --trajectory, and inlines the trajectories,
the evidence summaries, and the vendored Three.js into viewer/template.html.

Usage:
    tools/view.py --binary build/moon/moon_instr --sweep results/<sweep_dir> \\
        [--sweep results/<other_sweep_dir>] --out view.html
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, "viewer", "template.html")
THREE = os.path.join(ROOT, "viewer", "vendor", "three.min.js")
OPS_PER_CORE = 49
RUN_TIMEOUT = 600


def fly(binary, env_extra, args, trajectory_path):
    with tempfile.NamedTemporaryFile(suffix=".sdct", delete=False) as handle:
        trace = handle.name
    env = dict(os.environ)
    env.update({"SDC_TRACE": trace, "SDC_WORKLOAD": "moon"})
    env.update(env_extra)
    try:
        result = subprocess.run(
            [binary] + args + ["--trajectory", trajectory_path],
            env=env, capture_output=True, text=True, timeout=RUN_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        raise SystemExit(f"view: {binary} did not finish within {RUN_TIMEOUT} s")
    finally:
        if os.path.exists(trace):
            os.remove(trace)
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
        raise SystemExit(f"view: trial {args.trial} is not in {args.sweep}/summary.json")
    with open(os.path.join(os.path.dirname(os.path.abspath(args.binary)), "ops.json")) as handle:
        ops = json.load(handle)
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
        raise SystemExit(f"view: {args.sweep} was run with --defense {mode}, not {args.defense}")
    return mode


def injection(evidence):
    inject = evidence.get("inject", {})
    if not inject.get("enabled", True) or inject.get("op_id") is None:
        raise SystemExit("view: the evidence has no injection site")
    return inject


def fly_clean(binary, defense, tmp):
    """Fly the clean landing; returns (trajectory, spot) where spot is the faulty run's --reference."""
    stdout, clean = fly(binary, {}, ["--defense", defense], os.path.join(tmp, f"clean_{defense}.json"))
    match = re.search(r"spot=(\S+)", stdout)
    if not match:
        raise SystemExit(f"view: the clean landing printed no spot: {stdout.strip()}")
    return clean, match.group(1)


def fly_faulty(binary, defense, spot, inject, path):
    fault_env = {"SDC_INJECT": f"op={inject['op_id']},occ={inject['occurrence']},bit={inject['bit']}"}
    if inject.get("persistent"):
        fault_env["SDC_INJECT_PERSIST"] = "1"
    _, faulty = fly(binary, fault_env, ["--defense", defense, "--reference", spot], path)
    return faulty


def refly(args):
    """Re-fly the clean landing and the trial's faulty landing; returns (evidence, clean, faulty)."""
    evidence = load_trial(args)
    inject = injection(evidence)
    with tempfile.TemporaryDirectory() as tmp:
        clean, spot = fly_clean(args.binary, args.defense, tmp)
        faulty = fly_faulty(args.binary, args.defense, spot, inject, os.path.join(tmp, "faulty.json"))
    return evidence, clean, faulty


def pick_trials(sweep, per_class):
    """Every SDC trial plus the first per_class trials of each other outcome."""
    with open(os.path.join(sweep, "summary.json")) as handle:
        records = json.load(handle)["records"]
    taken = Counter()
    picked = []
    for record in records:
        cls = record["classification"]
        if cls == "SDC" or taken[cls] < per_class:
            taken[cls] += 1
            picked.append(record["trial"])
    return picked


def summarize(evidence, args):
    inject = evidence.get("inject", {})
    source = (evidence.get("injected_event") or {}).get("source", {})
    op_id = inject.get("op_id", 0)
    core = (op_id - 1) // OPS_PER_CORE if op_id > 0 else None
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--sweep", action="append", required=True, help="repeatable")
    parser.add_argument("--per-class", type=int, default=2, help="trials kept per non-SDC outcome")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    jobs = []
    for sweep in args.sweep:
        mode = sweep_defense(argparse.Namespace(sweep=sweep, defense=None))
        jobs += [argparse.Namespace(binary=args.binary, sweep=sweep, defense=mode, trial=t)
                 for t in pick_trials(sweep, args.per_class)]

    with tempfile.TemporaryDirectory() as tmp:
        cleans = {mode: fly_clean(args.binary, mode, tmp) for mode in {job.defense for job in jobs}}

        def one(index, job):
            evidence = load_trial(job)
            faulty = fly_faulty(args.binary, job.defense, cleans[job.defense][1], injection(evidence),
                                os.path.join(tmp, f"faulty_{index}.json"))
            summary = summarize(evidence, job)
            summary["warning"] = check_consistency(summary["classification"], faulty["touchdown"])
            if summary["warning"]:
                print(f"view: warning: trial {job.trial}: {summary['warning']}", file=sys.stderr)
            return {"evidence": summary, "faulty": faulty}

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            trials = list(pool.map(one, range(len(jobs)), jobs))

    data = {"clean": {mode: clean for mode, (clean, _) in cleans.items()}, "trials": trials}
    data = json.dumps(data, separators=(",", ":"))
    # Keep inlined text from closing the <script> element early.
    data = data.replace("</", "<\\/")
    with open(THREE) as handle:
        three = handle.read()
    with open(TEMPLATE) as handle:
        page = handle.read()
    page = page.replace("/*@THREE@*/", three, 1).replace("/*@DATA@*/", data, 1)
    with open(args.out, "w") as handle:
        handle.write(page)
    print(f"view: {len(trials)} trials -> {args.out}")


if __name__ == "__main__":
    main()
