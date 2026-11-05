import * as THREE from "three";

// Display-only craters and grain: shading normals and albedo only, never geometry, so the
// fitted surface stays put. Octaves fade out below one pixel.
const PARS = /* glsl */ `
uniform vec3 uDetailOrigin;
varying vec3 vDetailPos;

uvec3 pcg3d(uvec3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  return v;
}

vec3 rand3(vec3 cell) {
  return vec3(pcg3d(uvec3(ivec3(cell)))) / 4294967295.0;
}

float valueNoise(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  vec3 u = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(mix(rand3(i).x, rand3(i + vec3(1, 0, 0)).x, u.x),
        mix(rand3(i + vec3(0, 1, 0)).x, rand3(i + vec3(1, 1, 0)).x, u.x), u.y),
    mix(mix(rand3(i + vec3(0, 0, 1)).x, rand3(i + vec3(1, 0, 1)).x, u.x),
        mix(rand3(i + vec3(0, 1, 1)).x, rand3(i + vec3(1, 1, 1)).x, u.x), u.y),
    u.z);
}

float craters(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  float h = 0.0;
  for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x) {
        vec3 c = vec3(x, y, z);
        vec3 r = rand3(i + c + 4096.0);
        float radius = 0.12 + 0.3 * r.z * r.z;
        float d = length(f - c - r) / radius;
        float bowl = d < 1.0 ? 0.6 * (d * d - 1.0) : 0.0;
        float rim = 0.3 * exp(-pow((d - 1.0) * 5.0, 2.0));
        h += (bowl + rim) * radius;
      }
  return h;
}

// x: height in km; y: dimensionless albedo shade.
vec2 detailHeight(vec3 p, float footprint) {
  vec2 sum = vec2(0.0);
  float frequency = 0.125;
  float shade = 1.0;
  for (int k = 0; k < 10; ++k) {
    float w = 1.0 - smoothstep(0.1, 0.45, footprint * frequency);
    if (w <= 0.0) break;
    float n = valueNoise(p * frequency + float(k) * 17.0) - 0.5;
    float c = (k % 2 == 1) ? craters(p * frequency * 0.5 + float(k) * 31.0) : 0.0;
    sum.x += w * (0.18 * n + 0.5 * c) / frequency;
    sum.y += w * shade * (n + 0.8 * c);
    frequency *= 3.0;
    shade *= 0.8;
  }
  return sum;
}
`;

const APPLY = /* glsl */ `
#include <normal_fragment_maps>
{
  vec2 detail = detailHeight(vDetailPos, length(fwidth(vDetailPos)));
  vec3 dpx = dFdx(-vViewPosition);
  vec3 dpy = dFdy(-vViewPosition);
  vec3 r1 = cross(dpy, normal);
  vec3 r2 = cross(normal, dpx);
  float det = dot(dpx, r1);
  if (abs(det) > 1e-30) {
    normal = normalize(normal - (dFdx(detail.x) * r1 + dFdy(detail.x) * r2) / det);
  }
  diffuseColor.rgb *= clamp(1.0 + 0.22 * detail.y, 0.7, 1.25);
}
`;

export function addSurfaceDetail(root: THREE.Object3D, origin: THREE.Vector3): void {
  root.traverse((node) => {
    const mesh = node as THREE.Mesh;
    if (!mesh.isMesh) return;
    const material = mesh.material as THREE.MeshStandardMaterial;
    material.onBeforeCompile = (shader) => {
      shader.uniforms.uDetailOrigin = { value: origin };
      shader.vertexShader = shader.vertexShader
        .replace("#include <common>", "#include <common>\nuniform vec3 uDetailOrigin;\nvarying vec3 vDetailPos;")
        .replace(
          "#include <project_vertex>",
          "#include <project_vertex>\nvDetailPos = (modelMatrix * vec4(transformed, 1.0)).xyz - uDetailOrigin;",
        );
      shader.fragmentShader = shader.fragmentShader
        .replace("#include <common>", `#include <common>\n${PARS}`)
        .replace("#include <normal_fragment_maps>", APPLY);
    };
    material.needsUpdate = true;
  });
}
