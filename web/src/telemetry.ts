import { MOON_RADIUS_KM } from "./wire";
import { bitsToDouble, lastIndexAtOrBefore } from "./trajectory";
import type { Evidence, OpRun, Positioned, Sample, Trajectory } from "./trajectory";

// Must equal moon::kLanderDryMass in workloads/moon/common/lander.hpp.
const DRY_MASS_KG = 7000;
const SYMBOL: Record<string, string> = { fadd: "+", fsub: "-", fmul: "*", fdiv: "/" };

function fmt(v: number | null, digits: number, unit: string): string {
  return v === null || !Number.isFinite(v) ? "n/a" : `${v.toFixed(digits)} ${unit}`;
}

function rows(entries: [string, string][]): string {
  return entries.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join("");
}

function downrangeKm(start: Positioned, s: Sample): number | null {
  if (s.position[0] === null) return null;
  const a = start.position;
  const b = s.position as [number, number, number];
  const cos = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (Math.hypot(...a) * Math.hypot(...b));
  return Math.acos(Math.min(1, Math.max(-1, cos))) * MOON_RADIUS_KM;
}

export function renderTelemetry(el: HTMLElement, s: Sample, start: Positioned): void {
  const speed =
    s.vertical_speed === null || s.horizontal_speed === null ? null : Math.hypot(s.vertical_speed, s.horizontal_speed);
  el.innerHTML = rows([
    ["Mission elapsed", fmt(s.t, 1, "s")],
    ["Altitude", fmt(s.altitude, 1, "m")],
    ["Vertical speed", fmt(s.vertical_speed, 2, "m/s")],
    ["Horizontal speed", fmt(s.horizontal_speed, 2, "m/s")],
    ["Speed", fmt(speed, 2, "m/s")],
    ["Downrange", fmt(downrangeKm(start, s), 2, "km")],
    ["Propellant", fmt(s.mass === null ? null : s.mass - DRY_MASS_KG, 1, "kg")],
    ["Throttle", fmt(s.throttle === null ? null : 100 * s.throttle, 1, "%")],
    ["Thrust tilt", fmt(s.thrust_tilt, 2, "deg")],
  ]);
}

export function renderStatus(el: HTMLElement, trajectory: Trajectory, s: Sample, t: number): void {
  const entries: [string, string][] = [];
  if (trajectory.defense === "none") {
    entries.push(["Flight computer", "single, unprotected"]);
  } else {
    for (let i = 0; i < 3; ++i) {
      entries.push([`Computer ${i + 1}`, s.computers & (1 << i) ? "trusted" : "silenced"]);
    }
  }
  const td = trajectory.touchdown;
  if (td && t >= td.t) {
    const safe = td.score !== null && td.score <= 1;
    entries.push(
      ["Touchdown", fmt(td.t, 1, "s")],
      ["Descent rate", fmt(td.descent, 2, "m/s")],
      ["Sideways", fmt(td.horizontal, 2, "m/s")],
      ["Score", `${td.score === null ? "n/a" : td.score.toFixed(3)} (${safe ? "safe" : "outside envelope"})`],
    );
  } else if (!td && t >= trajectory.samples[trajectory.samples.length - 1].t) {
    entries.push(["Outcome", "no touchdown within the time limit"]);
  }
  el.innerHTML = rows(entries);
}

function double(v: number): string {
  return v === 0 ? "0" : v.toPrecision(10);
}

function num(hex: string | null): string {
  return hex === null ? "n/a" : double(bitsToDouble(hex));
}

function differingBits(a: string, b: string | null): string {
  if (b === null) return "n/a";
  const bits: number[] = [];
  let x = BigInt(a) ^ BigInt(b);
  for (let i = 0; x !== 0n; ++i, x >>= 1n) if (x & 1n) bits.push(i);
  if (bits.length === 0) return "none";
  return bits.length <= 4 ? bits.join(", ") : `${bits.length} bits`;
}

export function renderOperation(el: HTMLElement, e: Evidence | null, runs: OpRun[], t: number): void {
  if (e === null) {
    el.innerHTML = rows([["Operation", "no fault injected"]]);
    return;
  }
  const unary = e.kind === "sqrt";
  const entries: [string, string][] = [
    ["Operation", unary ? "sqrt(a)" : `a ${SYMBOL[e.kind] ?? e.kind} b`],
    ["Source", `${e.file.split("/").pop()}:${e.line}`],
  ];
  const i = lastIndexAtOrBefore(runs, t);
  const run = runs.length > 0 && runs[i].t <= t ? runs[i] : null;
  if (run === null) {
    entries.push(["Run", "not yet executed"]);
  } else {
    entries.push(
      ["Run", `#${run.occurrence}${i === runs.length - 1 ? " (last)" : ""}`],
      ["Executed at", fmt(run.t, 1, "s")],
      ["a", num(run.operand0)],
      ...(unary ? [] : ([["b", num(run.operand1)]] as [string, string][])),
      ["Result", num(run.result)],
      ["Clean result", num(run.clean)],
      ["Difference", run.clean === null ? "n/a" : double(bitsToDouble(run.result) - bitsToDouble(run.clean))],
      ["Bits differ", differingBits(run.result, run.clean)],
    );
  }
  el.innerHTML = rows(entries);
}
