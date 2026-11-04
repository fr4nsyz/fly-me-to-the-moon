import * as THREE from "three";
import type { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { addSurfaceDetail } from "./detail";
import { toScene, type Positioned, type Vec3m } from "./trajectory";

// Must equal moon::kMoonRadius in workloads/moon/common/constants.hpp.
export const MOON_RADIUS_KM = 1737.4;

// Chosen offline so the clean descent stays above the displayed terrain.
const MESH_ROTATION_Y_DEG = 70;

export interface MoonFit {
  moon: THREE.Object3D;
  minClearanceM: number;
}

function surfaceDistance(moon: THREE.Object3D, direction: THREE.Vector3): number {
  const ray = new THREE.Raycaster(new THREE.Vector3(), direction.clone().normalize());
  const hit = ray.intersectObject(moon, true)[0];
  if (!hit) throw new Error("moon mesh: no surface along a track direction");
  return hit.distance;
}

export async function loadMoon(loader: GLTFLoader, spot: Vec3m, track: Positioned[]): Promise<MoonFit> {
  const moon = (await loader.loadAsync("assets/moon_small.glb")).scene;
  moon.rotation.y = THREE.MathUtils.degToRad(MESH_ROTATION_Y_DEG);
  moon.updateMatrixWorld(true);
  moon.scale.setScalar(MOON_RADIUS_KM / surfaceDistance(moon, toScene(spot)));
  moon.updateMatrixWorld(true);
  addSurfaceDetail(moon, toScene(spot));

  let minClearanceM = Infinity;
  for (let i = 0; i < track.length - 1; ++i) {
    const p = toScene(track[i].position);
    minClearanceM = Math.min(minClearanceM, (p.length() - surfaceDistance(moon, p)) * 1000);
  }
  return { moon, minClearanceM };
}
