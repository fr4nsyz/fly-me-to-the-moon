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

export async function loadTrajectory(defense: string): Promise<Trajectory> {
  const response = await fetch(`data/trajectory_${defense}.json`);
  if (!response.ok || !response.headers.get("content-type")?.includes("json")) {
    throw new Error(`data/trajectory_${defense}.json: HTTP ${response.status}; run npm run prepare-data`);
  }
  return response.json();
}

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
