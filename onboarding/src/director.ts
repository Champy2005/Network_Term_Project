/** Absolute-time camera tracks: no frame-dependent easing or idle orbit. */
import { Vector3 } from 'three';
import type { BeatDefinition, ChapterDefinition } from './types';

export const ANCHORS: Record<string, [number, number, number]> = {
  alice: [-12, 1.5, 4],
  bob: [12, 1.5, -4],
  charlie: [13, 1.5, 10],
  server: [0, 2, 0],
  storage: [0, 1.8, -12],
  client: [-12, 2, 4],
  frame: [0, 3.5, 1],
  queue: [0, 3.5, 1],
  identity: [0, 3.5, 0],
  group: [0, 2, 0],
  history: [0, 2, -12],
  clock: [0, 3.5, 0],
  recovery: [0, 2, 0],
};
export const smooth = (x: number) => {
  const t = Math.max(0, Math.min(1, x));
  return t * t * (3 - 2 * t);
};
export interface CameraPose {
  position: Vector3;
  target: Vector3;
  fov: number;
}

function pose(
  beat: BeatDefinition,
  chapter: ChapterDefinition,
  aspect: number,
  landing: boolean,
): CameraPose {
  if (landing)
    return { position: new Vector3(27, 27, 35), target: new Vector3(-5, 0, -1), fov: 41 };
  const focus = new Vector3(
    ...(ANCHORS[chapter.stage === 'client' && beat.focus === 'queue' ? 'client' : beat.focus] ??
      ANCHORS.server),
  );
  const isWide = beat.view === 'wide';
  const detail = beat.view === 'detail';
  const cutaway = beat.view === 'cutaway';
  const distance = isWide ? 36 : detail ? 15 : cutaway ? 21 : 26;
  // More vertical room is required when the viewport becomes narrow.
  const fit = aspect < 1.35 ? 1.35 / aspect : 1;
  const theta = chapter.stage === 'storage' ? -0.4 : chapter.stage === 'client' ? -0.5 : 0.55;
  const target = isWide ? new Vector3(0, 1, 0) : focus;
  return {
    position: target
      .clone()
      .add(
        new Vector3(
          Math.sin(theta) * distance * fit,
          distance * 0.74 * fit,
          Math.cos(theta) * distance * fit,
        ),
      ),
    target,
    fov: detail ? 37 : 42,
  };
}

/** Land at the next beat boundary, so stepping to a beat gives its intended shot. */
export function cameraAt(
  chapter: ChapterDefinition,
  time: number,
  aspect: number,
  reducedMotion = false,
  landing = false,
): CameraPose {
  const index = Math.max(
    0,
    chapter.beats.findLastIndex((b) => time >= b.start),
  );
  const current = chapter.beats[index];
  const from = pose(current, chapter, aspect, landing);
  const next = chapter.beats[index + 1];
  if (landing || reducedMotion || !next) return from;
  const to = pose(next, chapter, aspect, false);
  const t = smooth((time - (current.end - 1.4)) / 1.4);
  return {
    position: from.position.lerp(to.position, t),
    target: from.target.lerp(to.target, t),
    fov: from.fov + (to.fov - from.fov) * t,
  };
}

/** Cutaway poses persist across adjacent beats and reassemble before a wide shot. */
export function revealAt(chapter: ChapterDefinition, time: number, reducedMotion = false): number {
  const index = Math.max(
    0,
    chapter.beats.findLastIndex((b) => time >= b.start),
  );
  const current = chapter.beats[index],
    next = chapter.beats[index + 1];
  const weight = (b: BeatDefinition) => (b.view === 'cutaway' || b.view === 'detail' ? 1 : 0);
  if (reducedMotion || !next) return weight(current);
  return (
    weight(current) + (weight(next) - weight(current)) * smooth((time - current.end + 1.4) / 1.4)
  );
}
