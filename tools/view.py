#!/usr/bin/env python3
"""Build a self-contained HTML descent viewer for one sweep trial.

Re-flies the clean and the faulty landing with --trajectory and inlines both
trajectories, the trial's evidence summary, and the vendored Three.js into
viewer/template.html.

Usage:
    tools/view.py --binary build/moon/moon_instr --sweep results/<sweep_dir> \\
        --trial N --defense none|tmr-lockstep --out view_trialN.html
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
    parser.add_argument("--sweep", required=True)
    parser.add_argument("--trial", type=int, required=True)
    parser.add_argument("--defense", required=True, choices=("none", "tmr-lockstep"))
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    with open(os.path.join(args.sweep, f"trial_{args.trial:04d}.json")) as handle:
        evidence = json.load(handle)
    inject = evidence.get("inject", {})
    if not inject.get("enabled", True) or "op_id" not in inject:
        raise SystemExit("view: the evidence has no injection site")

    with tempfile.TemporaryDirectory() as tmp:
        stdout, clean = fly(args.binary, {}, ["--defense", args.defense], os.path.join(tmp, "clean.json"))
        match = re.search(r"spot=(\S+)", stdout)
        if not match:
            raise SystemExit(f"view: the clean landing printed no spot: {stdout.strip()}")
        fault_env = {"SDC_INJECT": f"op={inject['op_id']},occ={inject['occurrence']},bit={inject['bit']}"}
        if inject.get("persistent"):
            fault_env["SDC_INJECT_PERSIST"] = "1"
        _, faulty = fly(
            args.binary, fault_env,
            ["--defense", args.defense, "--reference", match.group(1)],
            os.path.join(tmp, "faulty.json"),
        )

    summary = summarize(evidence, args)
    warning = check_consistency(summary["classification"], faulty["touchdown"])
    if warning:
        print(f"view: warning: {warning}", file=sys.stderr)
    summary["warning"] = warning

    data = json.dumps({"clean": clean, "faulty": faulty, "evidence": summary}, separators=(",", ":"))
    # Keep inlined text from closing the <script> element early.
    data = data.replace("</", "<\\/")
    with open(THREE) as handle:
        three = handle.read()
    with open(TEMPLATE) as handle:
        page = handle.read()
    page = page.replace("/*@THREE@*/", three, 1).replace("/*@DATA@*/", data, 1)
    with open(args.out, "w") as handle:
        handle.write(page)
    print(f"view: {summary['classification']} trial {args.trial} -> {args.out}")


if __name__ == "__main__":
    main()
