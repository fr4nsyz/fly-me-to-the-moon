import * as THREE from "three";
import { Line2 } from "three/examples/jsm/lines/Line2.js";
import { LineGeometry } from "three/examples/jsm/lines/LineGeometry.js";
import { LineMaterial } from "three/examples/jsm/lines/LineMaterial.js";
import { lastIndexAtOrBefore, toScene, type Positioned } from "./trajectory";

export interface Path {
  setTime(t: number): void;
  setResolution(width: number, height: number): void;
  dispose(): void;
}

export function createPath(scene: THREE.Scene, track: Positioned[], color: number): Path {
  const positions = track.flatMap((s) => toScene(s.position).toArray());
  const solid = new LineMaterial({ color, linewidth: 3 });
  const dashed = new LineMaterial({
    color,
    linewidth: 2,
    dashed: true,
    dashSize: 3,
    gapSize: 2,
    transparent: true,
    opacity: 0.85,
    depthWrite: false,
  });
  const history = new Line2(new LineGeometry().setPositions(positions), solid);
  const future = new Line2(new LineGeometry().setPositions(positions), dashed);
  future.computeLineDistances();
  history.renderOrder = 1;
  scene.add(future, history);
  return {
    setTime(t: number) {
      history.geometry.instanceCount = lastIndexAtOrBefore(track, t);
    },
    setResolution(width: number, height: number) {
      solid.resolution.set(width, height);
      dashed.resolution.set(width, height);
    },
    dispose() {
      scene.remove(future, history);
      history.geometry.dispose();
      future.geometry.dispose();
      solid.dispose();
      dashed.dispose();
    },
  };
}
