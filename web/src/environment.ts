import * as THREE from "three";

const EARTH_RADIUS_KM = 6371;
const EARTH_DISTANCE_KM = 384400;
const STAR_COUNT = 6000;
const STAR_SHELL_KM = 900000;

function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function stars(): THREE.Points {
  const random = mulberry32(1969);
  const positions = new Float32Array(STAR_COUNT * 3);
  const colors = new Float32Array(STAR_COUNT * 3);
  for (let i = 0; i < STAR_COUNT; ++i) {
    const z = 2 * random() - 1;
    const phi = 2 * Math.PI * random();
    const r = Math.sqrt(1 - z * z);
    positions.set([STAR_SHELL_KM * r * Math.cos(phi), STAR_SHELL_KM * z, STAR_SHELL_KM * r * Math.sin(phi)], 3 * i);
    const brightness = 0.35 + 0.65 * random() ** 3;
    const warmth = random();
    colors.set([brightness, brightness * (0.92 + 0.08 * warmth), brightness * (1.0 - 0.15 * warmth)], 3 * i);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  const material = new THREE.PointsMaterial({ size: 1.6, sizeAttenuation: false, vertexColors: true });
  return new THREE.Points(geometry, material);
}

export function createEnvironment(scene: THREE.Scene): void {
  scene.background = new THREE.Color(0x000000);
  scene.add(stars());

  const sunDirection = new THREE.Vector3(1, 0.35, 0.6).normalize();
  const sun = new THREE.DirectionalLight(0xffffff, 3);
  sun.position.copy(sunDirection);
  scene.add(sun, new THREE.AmbientLight(0xffffff, 0.04));

  const texture = new THREE.TextureLoader().load("assets/earth_bmng_5400.jpg");
  texture.colorSpace = THREE.SRGBColorSpace;
  const earth = new THREE.Mesh(
    new THREE.SphereGeometry(EARTH_RADIUS_KM, 96, 48),
    new THREE.MeshStandardMaterial({ map: texture, roughness: 0.9 }),
  );
  earth.position.set(-0.55, 0.25, -0.8).normalize().multiplyScalar(EARTH_DISTANCE_KM);
  scene.add(earth);
}
