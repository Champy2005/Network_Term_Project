/**
 * Reproducibly author the original Socket Chat hardware cutaways as GLB.
 * Usage: npm run assets:build
 */
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { GLTFExporter } from 'three/addons/exporters/GLTFExporter.js';
import { mergeGeometries, mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = path.join(root, 'public/assets/models');
const manifestPath = path.join(root, 'public/assets/manifest.json');
const REQUIRED_COMMON = ['shell', 'lid', 'core', 'port-in', 'port-out'];

// GLTFExporter is browser-oriented; this shim supplies its only required browser API.
globalThis.FileReader ??= class FileReader {
  async readAsArrayBuffer(blob) {
    this.result = await blob.arrayBuffer();
    this.onloadend?.({ target: this });
  }
  readAsDataURL(blob) {
    blob.arrayBuffer().then((buffer) => {
      this.result = `data:${blob.type || 'application/octet-stream'};base64,${Buffer.from(buffer).toString('base64')}`;
      this.onloadend?.({ target: this });
    }).catch((error) => this.onerror?.(error));
  }
};

const mat = (name, color, metalness, roughness, emissive = 0x000000) =>
  new THREE.MeshStandardMaterial({ name, color, metalness, roughness, emissive, emissiveIntensity: 0.22 });
const M = {
  graphite: mat('Graphite ceramic', 0x252b31, 0.72, 0.31),
  dark: mat('Shadow graphite', 0x11161a, 0.68, 0.34),
  edge: mat('Machined titanium', 0x67727b, 0.82, 0.26),
  board: mat('Circuit substrate', 0x17332f, 0.38, 0.49),
  copper: mat('Copper contact', 0xc77e43, 0.84, 0.27),
  teal: mat('Signal teal', 0x4ac7bf, 0.4, 0.3, 0x16867f),
  ivory: mat('Silkscreen ceramic', 0xb8c0c4, 0.16, 0.55),
  glass: mat('Smoked glass', 0x26373a, 0.5, 0.18),
};
const boxGeometryCache = new Map();

function group(parent, name, position = [0, 0, 0]) {
  const node = new THREE.Group(); node.name = name; node.position.set(...position); parent.add(node); return node;
}
function box(parent, name, size, position, material, bevel = 0.04) {
  const r = Math.min(bevel, size[0] / 3, size[1] / 3, size[2] / 3);
  const key = `${size.join(',')}:${r}`;
  let geometry = boxGeometryCache.get(key);
  if (!geometry) {
    // One bevel segment retains the machined silhouette at a fraction of the mesh cost.
    geometry = new RoundedBoxGeometry(...size, 1, r);
    boxGeometryCache.set(key, geometry);
  }
  geometry.computeVertexNormals();
  const mesh = new THREE.Mesh(geometry, material); mesh.name = name; mesh.position.set(...position); mesh.castShadow = true; mesh.receiveShadow = true; parent.add(mesh); return mesh;
}
function cylinder(parent, name, radius, height, position, material, radialSegments = 24) {
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, radialSegments), material);
  mesh.name = name; mesh.position.set(...position); mesh.castShadow = true; mesh.receiveShadow = true; parent.add(mesh); return mesh;
}
function line(parent, name, start, end, radius, material) {
  const a = new THREE.Vector3(...start), b = new THREE.Vector3(...end), d = new THREE.Vector3().subVectors(b, a);
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, d.length(), 10), material);
  mesh.name = name; mesh.position.copy(a).add(b).multiplyScalar(0.5); mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize()); parent.add(mesh); return mesh;
}
function createRoot(name) {
  const scene = new THREE.Scene(); scene.name = `${name} assembly`;
  scene.add(group(scene, 'assembly'));
  return scene.getObjectByName('assembly');
}
function addBaseAndShell(root, opts = {}) {
  const shell = group(root, 'shell');
  box(shell, 'lower-chassis', [5.8, 0.36, 3.8], [0, 0.18, 0], M.graphite, 0.14);
  box(shell, 'service-floor', [5.2, 0.08, 3.25], [0, 0.4, 0], M.dark, 0.025);
  for (const x of [-2.77, 2.77]) box(shell, `side-rail-${x < 0 ? 'left' : 'right'}`, [0.18, opts.wallHeight ?? 0.95, 3.55], [x, (opts.wallHeight ?? 0.95) / 2 + 0.33, 0], M.edge, 0.075);
  for (const z of [-1.78, 1.78]) box(shell, `end-rim-${z < 0 ? 'front' : 'rear'}`, [5.48, 0.11, 0.13], [0, 0.48, z], M.edge, 0.04);
  // Flush fasteners and a single continuous seam make the enclosure read as serviceable.
  for (const x of [-2.45, -1.64, 1.64, 2.45]) for (const z of [-1.57, 1.57]) {
    cylinder(shell, `captive-fastener-${x}-${z}`, 0.055, 0.035, [x, 0.405, z], M.copper, 16);
  }
  box(shell, 'seam-front', [4.9, 0.025, 0.018], [0, 0.68, -1.89], M.dark, 0.008);
  return shell;
}
function addLid(root, y, label = 'service-lid') {
  const lid = group(root, 'lid', [0, y, 0]);
  box(lid, label, [5.56, 0.19, 3.54], [0, 0, 0], M.graphite, 0.12);
  box(lid, 'lid-recess', [4.8, 0.018, 2.7], [0, -0.103, 0], M.dark, 0.06);
  box(lid, 'lid-inlay', [4.55, 0.018, 2.45], [0, -0.086, 0], M.edge, 0.055);
  box(lid, 'status-window', [0.56, 0.025, 0.15], [-2.1, 0.101, 0], M.glass, 0.04);
  for (let i = 0; i < 3; i++) cylinder(lid, `status-lamp-${i}`, 0.037, 0.025, [-2.22 + i * 0.12, 0.118, 0], i === 0 ? M.teal : M.copper, 16);
  return lid;
}
function addPorts(root, kind) {
  for (const [name, x, material, label] of [
    ['port-in', -2.95, M.teal, kind === 'client' ? 'uplink' : 'ingress'],
    ['port-out', 2.95, M.copper, kind === 'server' ? 'egress' : 'peer'],
  ]) {
    const port = group(root, name, [x, 0.67, -0.24]);
    box(port, `${label}-bezel`, [0.13, 0.48, 0.78], [0, 0, 0], M.edge, 0.065);
    box(port, `${label}-recess`, [0.035, 0.29, 0.54], [Math.sign(x) * 0.073, 0, 0], M.dark, 0.035);
    for (let i = 0; i < 5; i++) box(port, `${label}-contact-${i}`, [0.025, 0.035, 0.34], [Math.sign(x) * 0.097, -0.1 + i * 0.05, 0], material, 0.01);
    box(port, `${label}-label`, [0.022, 0.055, 0.37], [Math.sign(x) * 0.09, 0.36, 0], M.ivory, 0.006);
  }
}
function addCore(root, kind) {
  const core = group(root, 'core', [0, 0.52, 0]);
  box(core, 'logic-carrier', [3.4, 0.13, 2.72], [0, 0.11, 0], M.board, 0.07);
  // Flat functional components: processor, memory banks, and copper bus rails.
  box(core, 'processor-package', [0.86, 0.24, 0.78], [-0.45, 0.31, 0], M.dark, 0.08);
  box(core, 'processor-lid', [0.58, 0.04, 0.5], [-0.45, 0.45, 0], M.edge, 0.025);
  box(core, 'processor-mark', [0.24, 0.012, 0.08], [-0.45, 0.477, 0], M.ivory, 0.01);
  for (let i = 0; i < 4; i++) {
    const z = -0.92 + i * 0.61;
    box(core, `memory-bank-${i}`, [0.34, 0.18, 0.46], [0.62, 0.28, z], M.dark, 0.04);
    box(core, `memory-label-${i}`, [0.24, 0.018, 0.055], [0.62, 0.38, z], i % 2 ? M.ivory : M.teal, 0.01);
    for (let p = 0; p < 7; p++) box(core, `memory-contact-${i}-${p}`, [0.025, 0.02, 0.026], [0.43 + p * 0.062, 0.205, z - 0.23], M.copper, 0.007);
  }
  for (const z of [-0.95, 0, 0.95]) box(core, `bus-rail-${z}`, [2.45, 0.045, 0.045], [0, 0.22, z], M.copper, 0.02);
  // Distinct power and network daughterboards provide recognizable cutaway layers.
  box(core, 'power-regulator', [0.72, 0.22, 0.63], [-1.18, 0.3, -0.82], M.edge, 0.05);
  for (let i = 0; i < 3; i++) cylinder(core, `power-capacitor-${i}`, 0.09, 0.19, [-1.35 + i * 0.17, 0.5, -0.83], M.dark, 20);
  box(core, 'network-interface', [0.72, 0.17, 0.57], [1.55, 0.28, 0.87], M.graphite, 0.05);
  for (let i = 0; i < 6; i++) box(core, `interface-contact-${i}`, [0.04, 0.035, 0.3], [1.29 + i * 0.09, 0.2, 1.22], M.copper, 0.01);
  for (let i = 0; i < 6; i++) line(core, `trace-${i}`, [-2.05, 0.2, -0.95 + i * 0.38], [-1.6 + (i % 3) * 0.16, 0.2, -0.95 + i * 0.38], 0.018, i % 2 ? M.teal : M.copper);
  const kindCode = kind === 'client' ? 'C' : kind === 'server' ? 'S' : 'D';
  box(core, `identity-marker-${kindCode}`, [0.16, 0.09, 0.16], [2.0, 0.27, -1.0], M.teal, 0.025);
  return core;
}
function addAnchors(root) {
  // Empty semantic reference points for a renderer's camera focus and stable assembly alignment.
  for (const [name, p] of Object.entries({
    'anchor-center': [0, 1.1, 0], 'anchor-front': [0, 0.8, -2.2],
    'anchor-core': [0, 1.12, 0], 'anchor-ingress': [-3.05, 0.67, -0.24],
    'anchor-egress': [3.05, 0.67, -0.24],
  })) group(root, name, p);
}

function optimizeDrawCalls(root, kind) {
  const pivots = new Set(['shell', 'lid', 'core', 'port-in', 'port-out', 'tray-0', 'tray-1', 'tray-2', 'tray-3']);
  const targets = new Map();
  root.traverse((node) => { if (pivots.has(node.name)) targets.set(node.name, node); });
  const bucketMeshes = new Map([...targets.keys()].map((name) => [name, new Map()]));
  root.updateMatrixWorld(true);
  const inverse = new Map([...targets].map(([name, target]) => [name, target.matrixWorld.clone().invert()]));

  root.traverse((node) => {
    if (!node.isMesh) return;
    let parent = node.parent;
    while (parent && !pivots.has(parent.name)) parent = parent.parent;
    if (!parent) throw new Error(`${kind}: renderable ${node.name} is outside a semantic pivot`);
    let material = node.material;
    if (kind === 'storage' && parent.name.startsWith('tray-')) {
      if (material === M.edge) material = M.graphite;
      else if (material === M.copper || material === M.teal) material = M.teal;
      else if (material === M.graphite || material === M.dark) material = M.dark;
    }
    if ((parent.name === 'lid' || parent.name === 'core' || parent.name === 'port-in' || parent.name === 'port-out') && material === M.ivory) material = M.edge;
    if (parent.name === 'lid' && material === M.glass) material = M.dark;
    if (parent.name === 'lid' && material === M.copper) material = M.teal;
    if (parent.name === 'shell' && material === M.copper) material = M.edge;
    if (parent.name === 'core' && material === M.teal) material = M.copper;
    if (kind === 'storage' && parent.name === 'core' && material !== M.board) material = M.dark;
    if (kind === 'storage' && parent.name.startsWith('tray-') && (material === M.teal || material === M.copper)) material = M.dark;
    const geometry = node.geometry.index ? node.geometry.toNonIndexed() : node.geometry.clone();
    const localMatrix = inverse.get(parent.name).clone().multiply(node.matrixWorld);
    geometry.applyMatrix4(localMatrix);
    const indexedGeometry = mergeVertices(geometry, 1e-5);
    geometry.dispose();
    const materialBucket = bucketMeshes.get(parent.name);
    if (!materialBucket.has(material)) materialBucket.set(material, []);
    materialBucket.get(material).push(indexedGeometry);
  });

  for (const [pivotName, pivot] of targets) {
    const buckets = bucketMeshes.get(pivotName);
    const renderables = [];
    for (const [material, geometries] of buckets) {
      if (!geometries.length) continue;
      const merged = mergeGeometries(geometries, true);
      for (const geometry of geometries) geometry.dispose();
      if (!merged) throw new Error(`${kind}/${pivotName}: geometry batch has incompatible attributes`);
      const mesh = new THREE.Mesh(merged, material);
      mesh.name = `batch-${pivotName}-${material.name.replaceAll(' ', '-')}`;
      mesh.castShadow = true; mesh.receiveShadow = true; renderables.push(mesh);
    }
    pivot.clear();
    for (const mesh of renderables) pivot.add(mesh);
  }
  const drawCalls = [...bucketMeshes.values()].reduce((sum, buckets) => sum + [...buckets.values()].filter((geometries) => geometries.length).length, 0);
  if (drawCalls > 25) throw new Error(`${kind}: optimized draw-call count ${drawCalls} exceeds 25 (${[...bucketMeshes].map(([name, buckets]) => `${name}:${[...buckets.values()].filter((geometries) => geometries.length).length}`).join(', ')})`);
  return drawCalls;
}
function buildClient() {
  const root = createRoot('client');
  const shell = addBaseAndShell(root, { wallHeight: 0.74 });
  addCore(root, 'client');
  addLid(root, 1.34, 'client-cover');
  addPorts(root, 'client');
  // A shallow input/status deck differentiates the workstation client without adding a screen prop.
  const deck = group(shell, 'input-deck', [0, 0.69, 1.27]);
  box(deck, 'deck-panel', [2.1, 0.09, 0.52], [0, 0, 0], M.dark, 0.045);
  for (let row = 0; row < 2; row++) for (let col = 0; col < 10; col++) box(deck, `key-${row}-${col}`, [0.13, 0.025, 0.11], [-0.83 + col * 0.184, 0.056, -0.11 + row * 0.19], col === 9 && row === 0 ? M.teal : M.edge, 0.018);
  addAnchors(root); return root;
}
function buildServer() {
  const root = createRoot('server');
  addBaseAndShell(root, { wallHeight: 1.12 });
  const core = addCore(root, 'server');
  const riser = group(core, 'compute-riser', [0, 0.46, 0]);
  box(riser, 'compute-bridge', [3.8, 0.12, 2.6], [0, 0.08, 0], M.edge, 0.06);
  for (let i = 0; i < 3; i++) {
    box(riser, `compute-module-${i}`, [0.78, 0.42, 1.9], [-1.15 + i * 1.15, 0.36, 0], M.graphite, 0.07);
    box(riser, `module-window-${i}`, [0.54, 0.025, 1.45], [-1.15 + i * 1.15, 0.58, 0], M.glass, 0.035);
    for (let slot = 0; slot < 5; slot++) box(riser, `module-vent-${i}-${slot}`, [0.07, 0.018, 0.68], [-1.34 + i * 1.15 + slot * 0.095, 0.602, 0], M.edge, 0.018);
  }
  for (let i = 0; i < 4; i++) {
    const fan = group(riser, `thermal-channel-${i}`, [-2.12 + i * 1.41, 0.38, 1.22]);
    box(fan, `channel-base-${i}`, [0.95, 0.16, 0.55], [0, 0, 0], M.dark, 0.05);
    box(fan, `channel-grille-${i}`, [0.75, 0.03, 0.36], [0, 0.1, 0], M.edge, 0.035);
    for (let l = 0; l < 5; l++) box(fan, `grille-slot-${i}-${l}`, [0.035, 0.012, 0.27], [-0.24 + l * 0.12, 0.119, 0], M.dark, 0.01);
  }
  addLid(root, 2.16, 'top-service-panel');
  addPorts(root, 'server');
  addAnchors(root); return root;
}
function buildStorage() {
  const root = createRoot('storage');
  addBaseAndShell(root, { wallHeight: 1.8 });
  const core = group(root, 'core', [0, 0.5, 1.57]);
  box(core, 'storage-backplane', [3.4, 1.45, 0.09], [0, 0.77, 0], M.board, 0.04);
  box(core, 'storage-controller', [1.2, 0.62, 0.14], [0, 0.92, -0.12], M.graphite, 0.045);
  for (let i = 0; i < 4; i++) {
    box(core, `storage-channel-${i}`, [0.16, 0.2, 0.035], [-0.39 + i * 0.26, 0.88, -0.205], M.teal, 0.02);
    box(core, `storage-bay-contact-${i}`, [0.42, 0.07, 0.035], [-1.08 + i * 0.72, 0.34, -0.12], M.copper, 0.01);
  }
  // Storage is a real four-bay drawer stack with visible pull lips and drive media blocks.
  for (let i = 0; i < 4; i++) {
    const y = 0.51 + i * 0.37;
    const tray = group(root, `tray-${i}`, [0, y, 0]);
    box(tray, `tray-bed-${i}`, [4.72, 0.12, 2.82], [0, 0, 0], M.edge, 0.055);
    box(tray, `tray-front-${i}`, [4.9, 0.26, 0.15], [0, 0.12, -1.49], M.graphite, 0.055);
    box(tray, `tray-pull-${i}`, [0.85, 0.045, 0.085], [0, 0.3, -1.58], M.dark, 0.025);
    box(tray, `tray-led-${i}`, [0.16, 0.04, 0.07], [-1.75, 0.28, -1.59], i === 0 ? M.teal : M.copper, 0.02);
    for (let bay = 0; bay < 2; bay++) {
      const x = -0.92 + bay * 1.84;
      box(tray, `media-${i}-${bay}`, [1.2, 0.18, 2.1], [x, 0.17, 0.05], M.dark, 0.075);
      box(tray, `media-label-${i}-${bay}`, [0.82, 0.018, 0.72], [x, 0.267, 0.03], M.graphite, 0.04);
      for (let stripe = 0; stripe < 2; stripe++) box(tray, `media-vent-${i}-${bay}-${stripe}`, [0.52, 0.012, 0.035], [x, 0.281, -0.1 + stripe * 0.2], M.edge, 0.01);
      for (let pin = 0; pin < 3; pin++) box(tray, `media-contact-${i}-${bay}-${pin}`, [0.035, 0.025, 0.08], [x - 0.2 + pin * 0.2, 0.15, 1.12], M.copper, 0.008);
    }
    for (let x of [-2.15, 2.15]) box(tray, `runner-${i}-${x}`, [0.08, 0.08, 2.6], [x, 0.1, 0], M.copper, 0.025);
  }
  const lid = addLid(root, 2.25, 'storage-top-cover');
  // The lid is deliberately kept within the overall 3 m envelope.
  lid.position.y = 2.25;
  addPorts(root, 'storage');
  addAnchors(root); return root;
}

async function exportGlb(scene, file) {
  const exporter = new GLTFExporter();
  const result = await exporter.parseAsync(scene, { binary: true, onlyVisible: true, trs: true, maxTextureSize: 256 });
  const buffer = Buffer.from(result);
  await writeFile(file, buffer);
  return buffer;
}

await mkdir(outDir, { recursive: true });
const manifest = { version: 1, provenance: 'Original procedural assets authored for Inside Socket Chat; CC0-1.0; no external meshes or textures.', models: {} };
for (const [id, build, extra] of [
  ['client', buildClient, []], ['server', buildServer, []], ['storage', buildStorage, ['tray-0', 'tray-1', 'tray-2', 'tray-3']],
]) {
  const scene = build();
  const drawCalls = optimizeDrawCalls(scene, id);
  const file = path.join(outDir, `${id}.glb`);
  const data = await exportGlb(scene, file);
  const names = [];
  scene.traverse((node) => { if (node.name) names.push(node.name); });
  const required = [...REQUIRED_COMMON, ...extra, 'anchor-center', 'anchor-core', 'anchor-ingress', 'anchor-egress'];
  const missing = required.filter((name) => !names.includes(name));
  if (missing.length) throw new Error(`${id}: missing semantic nodes ${missing.join(', ')}`);
  manifest.models[id] = {
    src: `/assets/models/${id}.glb`, bytes: data.length,
    sha256: createHash('sha256').update(data).digest('hex'),
    dimensions: { width: 5.9, depth: 3.9, height: id === 'client' ? 1.8 : id === 'server' ? 2.5 : 2.75 },
    origin: 'bottom-centered', nodes: [...REQUIRED_COMMON, ...extra, 'anchor-center', 'anchor-core', 'anchor-ingress', 'anchor-egress'],
    pivots: Object.fromEntries([...REQUIRED_COMMON.slice(0, 3), ...extra].map((name) => [name, 'local'])),
    drawCalls,
  };
  console.log(`${id}: ${data.length.toLocaleString()} bytes, ${names.length} named nodes`);
}
await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);

