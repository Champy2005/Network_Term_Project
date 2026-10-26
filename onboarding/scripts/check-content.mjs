import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const content = await readFile(path.join(root, 'onboarding/src/content.ts'), 'utf8');
const scenario = await readFile(path.join(root, 'onboarding/src/scenario.ts'), 'utf8');
const atlas = JSON.parse(await readFile(path.join(root, 'onboarding/public/content/source-atlas.json'), 'utf8'));
const expectedDurations = [45, 60, 75, 90, 70, 80, 70, 80, 80, 80, 55, 100, 60];
const expectedStages = ['overview', 'overview', 'client', 'framing', 'tls', 'session', 'identity', 'routing', 'groups', 'storage', 'heartbeat', 'recovery', 'overview'];

const fail = (message) => { throw new Error(message); };
const speechOverruns = [];
const chapters = [...content.matchAll(/^    id: '([^']+)', title:.*?^    duration: (\d+), stage: '([^']+)'/gms)];
if (chapters.length !== 13) fail(`Expected 13 authored chapters, found ${chapters.length}.`);
const durations = chapters.map((match) => Number(match[2]));
const stages = chapters.map((match) => match[3]);
if (JSON.stringify(durations) !== JSON.stringify(expectedDurations)) fail(`Chapter durations drifted: ${durations.join(', ')}.`);
if (durations.reduce((sum, duration) => sum + duration, 0) !== 945) fail('The exact chapter durations sum to 945 seconds.');
if (JSON.stringify(stages) !== JSON.stringify(expectedStages)) fail(`Chapter stage sequence drifted: ${stages.join(', ')}.`);

const authoredSections = [...content.matchAll(/^    id: '([^']+)', title:.*?(?=^    id: '|^\];)/gms)];
if (authoredSections.length !== 13) fail(`Could not isolate each chapter section (found ${authoredSections.length}).`);
let authoredBeatCount = 0;
for (let index = 0; index < authoredSections.length; index += 1) {
  const [section, id] = authoredSections[index];
  const beats = [...section.matchAll(/\{ title: '([^']+)', caption: '([^']+)', narration: '([^']+)'/g)];
  const count = beats.length;
  authoredBeatCount += [...section.matchAll(/^      \{ title: /gm)].length;
  if (count < 5 || count > 8) fail(`${id} has ${count} beats; each chapter needs 5–8.`);
  const beatLines = [...section.matchAll(/^      \{ title: [^\n]+/gm)].map((match) => match[0]);
  if (beatLines.some((line) => !/\bview: '(wide|close|cutaway|detail)'/.test(line))) fail(`${id} has a beat without an explicit renderer view.`);
  const duration = Number(section.match(/^    duration: (\d+)/m)?.[1]);
  const privateEnds = [8, 18, 28, 40, 53, 65, 72, 80];
  let cursor = 0;
  for (let beatIndex = 0; beatIndex < beats.length; beatIndex += 1) {
    const end = id === 'private-rooms' ? privateEnds[beatIndex]
      : beatIndex === beats.length - 1 ? duration
        : Math.round(((beatIndex + 1) * duration) / beats.length);
    const seconds = end - cursor;
    const wordCount = beats[beatIndex][3].trim().split(/\s+/).length;
    const limit = Math.floor(seconds * 2.5);
    if (wordCount > limit) speechOverruns.push(`${id}/${beats[beatIndex][1]}: ${wordCount} words for ${seconds}s (limit ${limit})`);
    cursor = end;
  }
}

const privateSection = authoredSections.find((match) => match[1] === 'private-rooms')?.[0];
if (!privateSection || [...privateSection.matchAll(/\{ title: '/g)].length !== 8) fail('The private-room chapter must contain eight beats.');
if (!content.includes('const privateRoomEnds = [8, 18, 28, 40, 53, 65, 72, 80]')) fail('Private-room timeline must use boundaries 0, 8, 18, 28, 40, 53, 65, 72, and 80 seconds.');

const atlasIds = new Set(atlas.sources.map((entry) => entry.id));
const sourceRefs = [...content.matchAll(/sourceIds: \[([^\]]*)\]/g)].flatMap((match) => [...match[1].matchAll(/source\.([a-zA-Z0-9_]+)/g)].map((item) => item[1]));
const sourceBlock = content.match(/const source = \{([\s\S]*?)\};/)?.[1] ?? '';
const sourceMap = new Map([...sourceBlock.matchAll(/([a-zA-Z0-9_]+): '([^']+)'/g)].map((match) => [match[1], match[2]]));
for (const name of sourceRefs) {
  const id = sourceMap.get(name);
  if (!id) fail(`Beat references undefined source alias ${name}.`);
  if (!id || !atlasIds.has(id)) fail(`Beat source ${name} maps to missing atlas ID ${id ?? '(none)'}.`);
}
if (sourceRefs.length < 60) fail(`Expected source links on all beats; only found ${sourceRefs.length}.`);

const narrationMatches = [...content.matchAll(/narration: '([^']*)'/g)];
if (narrationMatches.length !== authoredBeatCount) fail(`Expected ${authoredBeatCount} authored beat narrations, found ${narrationMatches.length}.`);
if (speechOverruns.length) fail(`Narration cues exceed their beat windows:\n${speechOverruns.join('\n')}`);

const experimentIds = [...scenario.matchAll(/^  \{ id: '([^']+)', title:/gm)].map((match) => match[1]);
const expectedExperiments = ['split-header', 'coalesced', 'oversized', 'forged-sender', 'nonmember', 'slow-receiver', 'cancel-write', 'lost-ack', 'reclaim-name', 'partial-idle'];
if (JSON.stringify(experimentIds) !== JSON.stringify(expectedExperiments)) fail(`Experiment inventory drifted: ${experimentIds.join(', ')}.`);
for (const id of expectedExperiments) {
  const escaped = id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  if (!new RegExp(`case '${escaped}':`).test(scenario)) fail(`Experiment ${id} has no deterministic action replay model.`);
}
console.log(`Content valid: 13 chapters / 945s from the exact supplied durations, ${authoredBeatCount} narrated beats, ${sourceRefs.length} beat-source links, and 10 deterministic experiments.`);
