import * as THREE from "three";
import type { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { lastIndexAtOrBefore, toScene, type Positioned } from "./trajectory";

const LM_HEIGHT_KM = 0.00704;
const UP = new THREE.Vector3(0, 1, 0);

export async function loadLander(loader: GLTFLoader): Promise<THREE.Group> {
  const model = (await loader.loadAsync("assets/lunar_module.glb")).scene;
  const box = new THREE.Box3().setFromObject(model);
  const size = box.getSize(new THREE.Vector3());
  const center = box.getCenter(new THREE.Vector3());
  const k = LM_HEIGHT_KM / size.y;
  model.scale.setScalar(k);
  model.position.set(-center.x * k, -box.min.y * k, -center.z * k);
  const lander = new THREE.Group();
  lander.add(model);
  return lander;
}

// The plant has no attitude: the body axis is drawn along the commanded thrust,
// rebuilt from thrust_tilt (signed, in the plane of travel) and the local vertical.
export function thrustDirection(track: Positioned[], t: number): THREE.Vector3 {
  const i = lastIndexAtOrBefore(track, t);
  const here = toScene(track[i].position);
  const up = here.clone().normalize();
  const tilt = track[i].thrust_tilt;
  if (tilt === null || track.length < 2) return up;
  const travel =
    i + 1 < track.length ? toScene(track[i + 1].position).sub(here) : here.clone().sub(toScene(track[i - 1].position));
  travel.addScaledVector(up, -travel.dot(up));
  if (travel.lengthSq() === 0) return up;
  travel.normalize();
  const a = THREE.MathUtils.degToRad(tilt);
  return up.multiplyScalar(Math.cos(a)).addScaledVector(travel, Math.sin(a)).normalize();
}

export function orient(lander: THREE.Object3D, direction: THREE.Vector3): void {
  lander.quaternion.setFromUnitVectors(UP, direction);
}
