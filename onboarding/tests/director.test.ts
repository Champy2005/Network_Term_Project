import { describe, it, expect } from 'vitest';
import { cameraAt, revealAt } from '../src/director';
import type { ChapterDefinition } from '../src/types';
const chapter = {
  stage: 'routing',
  beats: [
    { start: 0, end: 8, focus: 'server', view: 'wide' },
    { start: 8, end: 20, focus: 'server', view: 'cutaway' },
  ],
} as ChapterDefinition;
describe('authored camera tracks', () => {
  it('settles instead of drifting over an explanation', () => {
    expect(cameraAt(chapter, 11, 1.78)).toEqual(cameraAt(chapter, 18, 1.78));
  });
  it('reconstructs a shot exactly after seeking backward', () => {
    const first = cameraAt(chapter, 8.7, 1.78);
    cameraAt(chapter, 18, 1.78);
    expect(cameraAt(chapter, 8.7, 1.78)).toEqual(first);
  });
  it('uses instant stable framing for reduced motion', () => {
    expect(cameraAt(chapter, 8, 1.78, true)).toEqual(cameraAt(chapter, 18, 1.78, true));
  });
  it('lands on the intended shot and exploded pose when stepping to a beat', () => {
    expect(cameraAt(chapter, 8, 1.78)).toEqual(cameraAt(chapter, 11, 1.78));
    expect(revealAt(chapter, 8)).toBe(1);
    expect(revealAt(chapter, 18)).toBe(1);
  });
});
