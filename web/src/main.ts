import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { createPath, type Path } from "./paths";
import { renderStatus, renderTelemetry } from "./telemetry";
import { hasPosition, lastIndexAtOrBefore, loadTrajectory, positionAt, type Positioned, type Trajectory } from "./trajectory";
import { createWireMoon } from "./wire";
import "./style.css";

type CameraMode = "orbital" | "chase" | "overview";

const NOMINAL_COLOR = 0x5aa9ff;
const GRID_COLOR = 0x3fbf8f;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const container = $<HTMLElement>("scene");
const scrub = $<HTMLInputElement>("scrub");
const playButton = $<HTMLButtonElement>("play");
const clockLabel = $<HTMLSpanElement>("clock");
const speedSelect = $<HTMLSelectElement>("speed");
const defenseSelect = $<HTMLSelectElement>("defense");
const telemetryEl = $<HTMLElement>("telemetry");
const statusEl = $<HTMLElement>("status");

const renderer = new THREE.WebGLRenderer({ antialias: true, logarithmicDepthBuffer: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
container.appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1, 0.001, 2e6);
let controls = makeControls(new THREE.Vector3(0, 1, 0));

// OrbitControls reads camera.up only when constructed, so a new local vertical needs new controls.
function makeControls(up: THREE.Vector3): OrbitControls {
  camera.up.copy(up);
  const next = new OrbitControls(camera, renderer.domElement);
  next.enableDamping = true;
  return next;
}

const marker = new THREE.Points(
  new THREE.BufferGeometry().setAttribute("position", new THREE.Float32BufferAttribute([0, 0, 0], 3)),
  new THREE.PointsMaterial({ color: 0xffc861, size: 7, sizeAttenuation: false }),
);
scene.add(marker);

let trajectory: Trajectory;
let track: Positioned[] = [];
let path: Path | null = null;
let t = 0;
let playing = false;
let mode: CameraMode = "orbital";
let failure: string | null = null;

function endTime(): number {
  return track[track.length - 1].t;
}

function setPlaying(on: boolean): void {
  playing = on;
  playButton.textContent = on ? "Pause" : "Play";
}

function setCamera(next: CameraMode): void {
  mode = next;
  document.querySelectorAll<HTMLButtonElement>("[data-camera]").forEach((b) => {
    b.classList.toggle("on", b.dataset.camera === next);
  });
  const here = positionAt(track, t);
  const up = here.clone().normalize();
  controls.dispose();
  controls = makeControls(next === "chase" ? up : new THREE.Vector3(0, 1, 0));
  if (next === "orbital") {
    controls.target.set(0, 0, 0);
    const tilt = THREE.MathUtils.degToRad(35);
    camera.position.copy(up).multiplyScalar(Math.cos(tilt)).add(new THREE.Vector3(0, Math.sin(tilt), 0)).multiplyScalar(5200);
  } else if (next === "chase") {
    controls.target.copy(here);
    const back = positionAt(track, 0).sub(here);
    back.addScaledVector(up, -back.dot(up));
    if (back.lengthSq() === 0) back.set(0, 1, 0).addScaledVector(up, -up.y);
    back.normalize();
    camera.position.copy(here).addScaledVector(back, 0.03).addScaledVector(up, 0.012);
  } else {
    const first = positionAt(track, 0);
    const last = positionAt(track, endTime());
    const middle = first.clone().add(last).multiplyScalar(0.5);
    controls.target.copy(middle);
    camera.position.copy(middle).addScaledVector(middle.clone().normalize(), first.distanceTo(last) * 1.4);
  }
  controls.update();
}

function update(): void {
  const here = positionAt(track, t);
  if (mode === "chase") {
    camera.position.add(here.clone().sub(controls.target));
    controls.target.copy(here);
  }
  marker.position.copy(here);
  path?.setTime(t);

  const sample = trajectory.samples[lastIndexAtOrBefore(trajectory.samples, t)];
  renderTelemetry(telemetryEl, sample, track[0]);
  if (failure === null) renderStatus(statusEl, trajectory, sample, t);
  scrub.value = String(Math.round((1000 * t) / endTime()));
  clockLabel.textContent = `T+${t.toFixed(1).padStart(5, "0")} s`;
}

async function useTrajectory(defense: string): Promise<void> {
  trajectory = await loadTrajectory(defense);
  track = trajectory.samples.filter(hasPosition);
  path?.dispose();
  path = createPath(scene, track, NOMINAL_COLOR);
  path.setResolution(container.clientWidth, container.clientHeight);
  t = Math.min(t, endTime());
}

function resize(): void {
  const { clientWidth: w, clientHeight: h } = container;
  if (w === 0 || h === 0) return;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  path?.setResolution(w, h);
}

async function start(): Promise<void> {
  await useTrajectory("none");
  scene.add(createWireMoon(GRID_COLOR));

  resize();
  setCamera("orbital");
  new ResizeObserver(resize).observe(container);
  playButton.addEventListener("click", () => setPlaying(!playing && t < endTime()));
  scrub.addEventListener("input", () => {
    t = (Number(scrub.value) / 1000) * endTime();
  });
  defenseSelect.addEventListener("change", () => {
    useTrajectory(defenseSelect.value).catch(fail);
  });
  document.querySelectorAll<HTMLButtonElement>("[data-camera]").forEach((b) => {
    b.addEventListener("click", () => setCamera(b.dataset.camera as CameraMode));
  });

  const clock = new THREE.Clock();
  renderer.setAnimationLoop(() => {
    const dt = clock.getDelta();
    if (playing) {
      t = Math.min(t + dt * Number(speedSelect.value), endTime());
      if (t >= endTime()) setPlaying(false);
    }
    update();
    controls.update();
    renderer.render(scene, camera);
  });
}

function fail(error: unknown): void {
  setPlaying(false);
  failure = error instanceof Error ? error.message : String(error);
  const dt = document.createElement("dt");
  dt.textContent = "Error";
  const dd = document.createElement("dd");
  dd.textContent = failure;
  statusEl.replaceChildren(dt, dd);
  console.error(error);
}

start().catch(fail);
