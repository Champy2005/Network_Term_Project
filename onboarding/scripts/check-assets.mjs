/** Validate authored GLB framing, semantic node IDs, manifest hashes, and size bounds. */
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { CHAPTERS } from '../src/content.ts';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const manifest = JSON.parse(await readFile(path.join(root, 'public/assets/manifest.json'), 'utf8'));
const baseParts = ['shell', 'lid', 'core', 'port-in', 'port-out'];
const anchors = ['anchor-center', 'anchor-core', 'anchor-ingress', 'anchor-egress'];
const failures = [];
for (const [id, entry] of Object.entries(manifest.models ?? {})) {
  const file = path.join(root, 'public', entry.src.replace(/^\//, ''));
  try {
    const glb = await readFile(file);
    if (glb.length !== entry.bytes) failures.push(`${id}: manifest byte count ${entry.bytes} != ${glb.length}`);
    if (glb.length > 1_500_000) failures.push(`${id}: ${glb.length} bytes exceeds the 1,500,000-byte model budget`);
    if (glb.toString('ascii', 0, 4) !== 'glTF' || glb.readUInt32LE(4) !== 2 || glb.readUInt32LE(8) !== glb.length)
      failures.push(`${id}: invalid GLB 2.0 header or total length`);
    const jsonLength = glb.readUInt32LE(12);
    if (glb.readUInt32LE(16) !== 0x4e4f534a || jsonLength < 2 || 20 + jsonLength > glb.length)
      throw new Error('missing/invalid JSON chunk');
    const doc = JSON.parse(glb.toString('utf8', 20, 20 + jsonLength));
    const drawCalls = (doc.meshes ?? []).reduce((sum, mesh) => sum + mesh.primitives.length, 0);
    if (drawCalls > 25) failures.push(`${id}: ${drawCalls} material batches exceed the 25 draw-call budget`);
    if (drawCalls !== entry.drawCalls) failures.push(`${id}: manifest drawCalls ${entry.drawCalls} != ${drawCalls}`);
    const names = (doc.nodes ?? []).map((node) => node.name).filter(Boolean);
    const duplicateNames = [...new Set(names.filter((name, i) => names.indexOf(name) !== i))];
    if (duplicateNames.length) failures.push(`${id}: duplicate node names ${duplicateNames.join(', ')}`);
    const required = [...baseParts, ...(id === 'storage' ? ['tray-0', 'tray-1', 'tray-2', 'tray-3'] : []), ...anchors];
    const missing = required.filter((name) => !names.includes(name));
    if (missing.length) failures.push(`${id}: missing required node(s) ${missing.join(', ')}`);
    if (JSON.stringify(entry.nodes) !== JSON.stringify(required)) failures.push(`${id}: manifest semantic node list differs from contract`);
    const hash = createHash('sha256').update(glb).digest('hex');
    if (hash !== entry.sha256) failures.push(`${id}: SHA-256 differs from manifest`);
    if (!entry.pivots || Object.keys(entry.pivots).some((name) => !names.includes(name))) failures.push(`${id}: pivot refers to missing node`);
  } catch (error) {
    failures.push(`${id}: ${error.message}`);
  }
}
if (!manifest.provenance?.includes('Original') || !manifest.provenance?.includes('CC0-1.0')) failures.push('manifest lacks original-work provenance/license');
if (Object.keys(manifest.models ?? {}).length !== 3) failures.push('expected exactly three model variants');
const audioPath = path.join(root, 'public/assets/audio-manifest.json');
const audioManifest = JSON.parse(await readFile(audioPath, 'utf8'));
const expectedCues = new Map(CHAPTERS.flatMap((chapter) => chapter.beats.map((beat) => [
  `${chapter.id}/${beat.id}`, {
    duration: beat.end - beat.start,
    textHash: createHash('sha256').update(beat.narration.trim(), 'utf8').digest('hex'),
  },
])));
if (Object.keys(audioManifest.cues ?? {}).length !== expectedCues.size) failures.push(`audio manifest has ${Object.keys(audioManifest.cues ?? {}).length} cues, expected ${expectedCues.size}`);
for (const [key, expected] of expectedCues) {
  const { duration: beatDuration, textHash } = expected;
  const cue = audioManifest.cues?.[key];
  if (!cue) { failures.push(`audio manifest missing cue ${key}`); continue; }
  if (cue.beatDuration !== beatDuration) failures.push(`${key}: beat duration differs from current content`);
  if (cue.textHash !== textHash) failures.push(`${key}: narration text differs from the text used to build this clip`);
  if (!Number.isFinite(cue.duration) || cue.duration <= 0) failures.push(`${key}: missing or invalid clip duration`);
  if (!cue.voice || cue.provisional !== true) failures.push(`${key}: missing voice/provisional metadata`);
  if (cue.fitsBeat !== true || cue.duration > beatDuration + 0.01) failures.push(`${key}: ${cue.duration}s clip exceeds its ${beatDuration}s beat`);
  try {
    const mp3 = await readFile(path.join(root, 'public', cue.src.replace(/^\//, '')));
    if (mp3.length !== cue.bytes) failures.push(`${key}: manifest byte count differs from clip`);
    if (createHash('sha256').update(mp3).digest('hex') !== cue.sha256) failures.push(`${key}: MP3 hash differs from manifest`);
    if (mp3.length < 512 || !(mp3.toString('ascii', 0, 3) === 'ID3' || (mp3[0] === 0xff && (mp3[1] & 0xe0) === 0xe0))) failures.push(`${key}: missing/invalid MP3 data`);
  } catch (error) { failures.push(`${key}: ${error.message}`); }
}
if (failures.length) {
  console.error(failures.map((message) => `FAIL ${message}`).join('\n'));
  process.exitCode = 1;
} else {
  const total = Object.values(manifest.models).reduce((sum, model) => sum + model.bytes, 0);
  if (total > 4_000_000) {
    console.error(`FAIL model total ${total} bytes exceeds the 4,000,000-byte budget`);
    process.exitCode = 1;
  } else {
    console.log(`PASS ${Object.keys(manifest.models).length} GLBs, ${total.toLocaleString()} total bytes; headers, hashes, semantic nodes, draw-call bounds, unique names, pivots, and provenance verified.`);
    console.log(`PASS ${expectedCues.size} English MP3 cues, narration text hashes, and beat durations verified.`);
  }
}
