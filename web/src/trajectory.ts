import * as THREE from "three";

export type Vec3m = [number, number, number];

export interface Sample {
  t: number;
  position: Vec3m | [null, null, null];
  altitude: number | null;
  vertical_speed: number | null;
  horizontal_speed: number | null;
  mass: number | null;
  throttle: number | null;
  thrust_tilt: number | null;
  computers: number;
}

export interface Touchdown {
  t: number;
  descent: number | null;
  horizontal: number | null;
  fuel: number | null;
  score: number | null;
  spot: Vec3m;
}

export interface Trajectory {
  defense: "none" | "tmr-lockstep";
  samples: Sample[];
  touchdown: Touchdown | null;
}

export type Positioned = Sample & { position: Vec3m };

export interface Evidence {
  classification: string;
  op_id: number;
  occurrence: number;
  bit: number;
  persistent: boolean;
  kind: string;
  file: string;
  line: number;
  function: string;
}

export interface OpRun {
  t: number;
  occurrence: number;
  operand0: string;
  operand1: string;
  result: string;
  clean: string | null;
}

type RawOpRun = [number, number, string, string, string, string | null];

export interface Run {
  trajectory: Trajectory;
  evidence: Evidence | null;
  opRuns: OpRun[];
}

export interface TrialEntry {
  file: string;
  trial: number;
  defense: string;
  classification: string;
}

async function fetchJson<T>(path: string): Promise<T> {
  const response = await fetch(`data/${path}`);
  if (!response.ok || !response.headers.get("content-type")?.includes("json")) {
    throw new Error(`data/${path}: HTTP ${response.status}`);
  }
  return response.json();
}

export async function loadRun(file: string): Promise<Run> {
  const data = await fetchJson<Trajectory | { evidence: Evidence; faulty: Trajectory; op_runs: RawOpRun[] }>(file);
  if (!("faulty" in data)) return { trajectory: data, evidence: null, opRuns: [] };
  const opRuns = data.op_runs.map(([t, occurrence, operand0, operand1, result, clean]) => ({
    t,
    occurrence,
    operand0,
    operand1,
    result,
    clean,
  }));
  return { trajectory: data.faulty, evidence: data.evidence, opRuns };
}

export function bitsToDouble(hex: string): number {
  const view = new DataView(new ArrayBuffer(8));
  view.setBigUint64(0, BigInt(hex));
  return view.getFloat64(0);
}

export const loadTrialIndex = () => fetchJson<TrialEntry[]>("index.json");

export function hasPosition(s: Sample): s is Positioned {
  return s.position.every((v) => v !== null && Number.isFinite(v));
}

// Simulation frame (meters, z = spin axis) to scene frame (km, y up).
export function toScene(p: Vec3m): THREE.Vector3 {
  return new THREE.Vector3(p[0] / 1000, p[2] / 1000, -p[1] / 1000);
}

export function lastIndexAtOrBefore(samples: { t: number }[], t: number): number {
  let lo = 0;
  let hi = samples.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (samples[mid].t <= t) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

export function positionAt(track: Positioned[], t: number): THREE.Vector3 {
  const i = lastIndexAtOrBefore(track, t);
  const a = toScene(track[i].position);
  if (i + 1 >= track.length) return a;
  const b = toScene(track[i + 1].position);
  const f = THREE.MathUtils.clamp((t - track[i].t) / (track[i + 1].t - track[i].t), 0, 1);
  return a.lerp(b, f);
}
