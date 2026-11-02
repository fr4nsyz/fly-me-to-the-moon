#!/usr/bin/env python3
"""Render the README figures: defense outcomes and clean versus faulty descents.

Re-flies one trial from each committed lunar sweep (runs are deterministic) and
writes each figure as a light and a dark PNG.

Usage:
    .venv/bin/python tools/plot.py --binary build/moon/moon_instr --trial 35 --out docs/img
"""

import argparse
import json
import math
import os
import sys
from collections import Counter

import matplotlib

matplotlib.use("Agg")
import matplotlib.patheffects as patheffects
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import view  # noqa: E402

RESULTS = os.path.join(view.ROOT, "results")
SWEEPS = {
    "none": os.path.join(RESULTS, "sweep_moon_persistent_none"),
    "tmr-lockstep": os.path.join(RESULTS, "sweep_moon_persistent_tmr_lockstep"),
}
MOON_RADIUS = 1737400.0

# Validated categorical slots and surfaces for each mode (dataviz reference palette).
THEMES = {
    "light": {"SURFACE": "#fcfcfb", "INK": "#0b0b0b", "MUTED": "#52514e", "GRID": "#e6e5e1", "SILENT": "#c9c8c3",
              "CLEAN": "#2a78d6", "FAULTY": "#eb6834", "CORRECTED": "#1baf7a", "GLOW": 0.0},
    "dark": {"SURFACE": "#111316", "INK": "#f0efec", "MUTED": "#a9aab0", "GRID": "#262a31", "SILENT": "#3d4249",
             "CLEAN": "#3987e5", "FAULTY": "#d95926", "CORRECTED": "#199e70", "GLOW": 0.22},
}


def apply_theme(name):
    globals().update(THEMES[name])
    plt.rcParams.update({
        "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
        "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "axes.titlecolor": INK, "text.color": INK,
        "axes.titlesize": 12, "axes.titleweight": "bold", "axes.titlelocation": "left",
        "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
        "axes.spines.top": False, "axes.spines.right": False,
        "xtick.color": MUTED, "ytick.color": MUTED, "font.size": 10, "lines.linewidth": 2,
        "legend.frameon": False,
    })


def glow(color):
    if not GLOW:
        return []
    return [patheffects.Stroke(linewidth=7, foreground=color, alpha=GLOW), patheffects.Normal()]


def samples(run, key):
    return [(s["t"], s[key]) for s in run["samples"] if s[key] is not None]


def downrange_km(run):
    p0 = run["samples"][0]["position"]
    out = []
    for s in run["samples"]:
        p = s["position"]
        if None in p or s["altitude"] is None:
            continue
        cross = (p[1] * p0[2] - p[2] * p0[1], p[2] * p0[0] - p[0] * p0[2], p[0] * p0[1] - p[1] * p0[0])
        angle = math.atan2(math.hypot(*cross), sum(a * b for a, b in zip(p, p0)))
        out.append((MOON_RADIUS * angle / 1000, s["altitude"] / 1000))
    return out


def first_divergence(clean, faulty):
    for a, b in zip(clean["samples"], faulty["samples"]):
        if a["position"] != b["position"]:
            return b["t"]
    return None


def landing(run):
    td = run["touchdown"]
    if not td or td["score"] is None:
        return "no touchdown"
    return f"{td['descent']:.1f} m/s down, {td['horizontal']:.1f} m/s sideways, score {td['score']:.2f}"


def outcomes_figure(path):
    rows = [("tmr-lockstep\n3 computers x 2 cores", "tmr-lockstep"), ("none\n1 core", "none")]
    fig_height = 3.3
    segments = [("SDC", FAULTY), ("MASKED", CLEAN), ("CORRECTED", CORRECTED)]
    fig, ax = plt.subplots(figsize=(9, fig_height))
    for y, (label, mode) in enumerate(rows):
        with open(os.path.join(SWEEPS[mode], "summary.json")) as handle:
            counts = Counter(r["classification"] for r in json.load(handle)["records"])
        left = 0
        for cls, color in segments:
            n = counts.get(cls, 0)
            if n == 0:
                continue
            ax.barh(y, n, left=left, color=color, height=0.55, edgecolor=SURFACE, linewidth=2,
                    label=cls if y == 1 or cls == "CORRECTED" else None)
            if n >= 6:
                ax.text(left + n / 2, y, f"{cls} {n}", ha="center", va="center", color="white", fontsize=9, fontweight="bold")
            left += n
    ax.set_axisbelow(True)
    ax.set_yticks(range(len(rows)), [r[0] for r in rows])
    ax.set_xlim(0, 200)
    ax.set_xlabel("trials (200 persistent single-bit faults per mode, seed 1337)")
    ax.grid(axis="y", visible=False)
    ax.set_title("Silent data corruption: no defense vs TMR and lockstep hybrid")
    handles = dict(zip(*reversed(ax.get_legend_handles_labels())))
    order = [cls for cls, _ in segments if cls in handles]
    ax.legend([handles[c] for c in order], order, loc="lower center", bbox_to_anchor=(0.5, 1.12), ncol=3)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def profile_panel(ax, clean, faulty, faulty_name, title):
    for run, color, name, style in ((clean, CLEAN, "clean", "-"), (faulty, FAULTY, faulty_name, (0, (4, 3)))):
        pts = downrange_km(run)
        ax.plot([p[0] for p in pts], [p[1] for p in pts], color=color, linestyle=style, label=f"{name}: {landing(run)}",
                path_effects=glow(color))
        ax.plot(pts[-1][0], pts[-1][1], "o", color=color, markersize=8, markeredgecolor=SURFACE, markeredgewidth=2)
    t = first_divergence(clean, faulty)
    if t is not None:
        div = next(p for p, s in zip(downrange_km(faulty), faulty["samples"]) if s["t"] >= t)
        ax.annotate(f"fault visible at t = {t:.0f} s", div, xytext=(div[0] - 150, div[1] - 4),
                    color=MUTED, arrowprops={"arrowstyle": "-", "color": MUTED})
    ax.set_xlabel("downrange (km)")
    ax.set_ylim(bottom=0)
    ax.legend(loc="lower left", fontsize=8.5)
    ax.set_title(title)


def same_fault_figure(path, none_run, tmr_run):
    (ev, clean, faulty), (tev, tclean, tfaulty) = none_run, tmr_run
    fig, (left, right) = plt.subplots(1, 2, figsize=(13, 4.4), sharey=True)
    profile_panel(left, clean, faulty, "faulty", f"No defense: fault in core {ev['core']}")
    profile_panel(right, tclean, tfaulty, "faulty, defended", f"TMR and lockstep: same fault in core {tev['core']}")
    left.set_ylabel("altitude (km)")
    fig.suptitle(f"Same persistent bit-{ev['bit']} flip in {ev['function'].split('(')[0]}, without and with the defense",
                 x=0.01, ha="left", fontweight="bold", color=INK)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def speeds_figure(path, clean, faulty):
    fig, axes = plt.subplots(1, 2, figsize=(13, 4))
    for ax, key, label in ((axes[0], "vertical_speed", "Vertical speed (m/s)"), (axes[1], "horizontal_speed", "Horizontal speed (m/s)")):
        for run, color, name in ((clean, CLEAN, "clean"), (faulty, FAULTY, "faulty")):
            pts = samples(run, key)
            ax.plot([p[0] for p in pts], [p[1] for p in pts], color=color, label=name, path_effects=glow(color))
        ax.set_title(label)
        ax.set_xlabel("time (s)")
    axes[0].legend(loc="lower left")
    fig.suptitle("Speeds, no defense: the faulty lander hits the ground fast and sliding",
                 x=0.01, ha="left", fontweight="bold", color=INK)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def computers_figure(path, faulty, ev):
    fig, ax = plt.subplots(figsize=(13, 1.9))
    t = [s["t"] for s in faulty["samples"]]
    for c in range(3):
        trusted = [bool(s["computers"] & (1 << c)) for s in faulty["samples"]]
        for i in range(len(t) - 1):
            ax.broken_barh([(t[i], t[i + 1] - t[i])], (c - 0.35, 0.7), color=CORRECTED if trusted[i] else SILENT, linewidth=0)
    ax.set_yticks(range(3), [f"computer {c}" for c in range(3)])
    ax.set_ylim(-0.6, 2.6)
    ax.grid(axis="y", visible=False)
    ax.set_xlabel("time (s)   (green: trusted, grey: silenced)")
    ax.set_title(f"Defended run: core {ev['core']} is broken, so computer {ev['computer']} is silenced and the other two fly")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--trial", type=int, default=1)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    runs = {}
    for mode, sweep in SWEEPS.items():
        trial = argparse.Namespace(binary=args.binary, sweep=sweep, trial=args.trial, defense=mode)
        view.sweep_defense(trial)
        evidence, clean, faulty = view.refly(trial)
        runs[mode] = (view.summarize(evidence, trial), clean, faulty)
    for theme in THEMES:
        apply_theme(theme)
        out = lambda name: os.path.join(args.out, f"{name}_{theme}.png")
        outcomes_figure(out("defense_outcomes"))
        same_fault_figure(out("same_fault"), runs["none"], runs["tmr-lockstep"])
        speeds_figure(out("descent_speeds"), runs["none"][1], runs["none"][2])
        computers_figure(out("computers"), runs["tmr-lockstep"][2], runs["tmr-lockstep"][0])
    print(f"plot: wrote 4 figures x {len(THEMES)} themes to {args.out}")


if __name__ == "__main__":
    main()
