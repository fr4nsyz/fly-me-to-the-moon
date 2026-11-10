import * as THREE from "three";

// Must equal moon::kMoonRadius in workloads/moon/common/constants.hpp.
export const MOON_RADIUS_KM = 1737.4;

const GRID_DEG = 15;
const RING_STEPS = 128;

export function createWireMoon(color: number): THREE.LineSegments {
  const r = MOON_RADIUS_KM;
  const points: number[] = [];
  const ring = (at: (a: number) => THREE.Vector3) => {
    for (let i = 0; i < RING_STEPS; ++i) {
      const a0 = (2 * Math.PI * i) / RING_STEPS;
      const a1 = (2 * Math.PI * (i + 1)) / RING_STEPS;
      points.push(...at(a0).toArray(), ...at(a1).toArray());
    }
  };
  for (let lat = -90 + GRID_DEG; lat < 90; lat += GRID_DEG) {
    const phi = THREE.MathUtils.degToRad(lat);
    ring((a) => new THREE.Vector3(r * Math.cos(phi) * Math.cos(a), r * Math.sin(phi), r * Math.cos(phi) * Math.sin(a)));
  }
  for (let lon = 0; lon < 180; lon += GRID_DEG) {
    const lambda = THREE.MathUtils.degToRad(lon);
    ring((a) => new THREE.Vector3(r * Math.cos(a) * Math.cos(lambda), r * Math.sin(a), r * Math.cos(a) * Math.sin(lambda)));
  }
  const geometry = new THREE.BufferGeometry().setAttribute("position", new THREE.Float32BufferAttribute(points, 3));
  return new THREE.LineSegments(geometry, new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0.4 }));
}
