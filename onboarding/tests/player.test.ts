import { describe, expect, it } from 'vitest';
import { Player, restoreSnapshot } from '../src/player';
import type { ChapterDefinition } from '../src/types';

const chapters = [
  {
    id: 'a',
    duration: 20,
    beats: [
      { start: 0, end: 8 },
      { start: 8, end: 20 },
    ],
  },
  { id: 'b', duration: 10, beats: [{ start: 0, end: 10 }] },
] as ChapterDefinition[];
describe('the public cinematic player', () => {
  it('freezes narrative time during pause and inspection', () => {
    const p = new Player(chapters);
    p.play();
    p.advance(5);
    p.pause();
    p.advance(8);
    expect(p.time).toBe(5);
    p.enterInspection();
    p.advance(3);
    expect(p.time).toBe(5);
    expect(p.mode).toBe('inspection');
    p.exitInspection();
    p.play();
    p.advance(2);
    expect(p.time).toBe(7);
  });
  it('holds at a chapter boundary and continues explicitly', () => {
    const p = new Player(chapters);
    p.play();
    p.advance(30);
    expect([p.chapterIndex, p.time, p.phase]).toEqual([0, 20, 'held']);
    p.continue();
    expect([p.chapterIndex, p.time, p.phase]).toEqual([1, 0, 'playing']);
  });
  it('watch mode advances with correct remaining time', () => {
    const p = new Player(chapters);
    p.preferences.watch = true;
    p.play();
    p.advance(24);
    expect([p.chapterIndex, p.time]).toEqual([1, 4]);
    p.advance(6);
    expect(p.phase).toBe('completed');
  });
  it('seeks and revisits semantic beats without accumulating state', () => {
    const p = new Player(chapters);
    p.seek(14);
    p.previousBeat();
    expect(p.time).toBe(8);
    p.previousBeat();
    expect(p.time).toBe(0);
    p.nextBeat();
    expect(p.time).toBe(8);
    p.seek(Infinity);
    expect(p.time).toBe(8);
    p.seek(-2);
    expect(p.time).toBe(0);
  });
  it('restores the journey after an experiment', () => {
    const p = new Player(chapters);
    p.seek(12);
    p.startExperiment('split-header');
    p.advance(9);
    expect(p.time).toBe(12);
    p.experimentStep = 2;
    p.resetExperiment();
    expect(p.experimentStep).toBe(0);
    p.endExperiment();
    expect(p.time).toBe(12);
    expect(p.phase).toBe('paused');
  });
  it('treats corrupt, incompatible, or non-finite saved progress as absent', () => {
    expect(restoreSnapshot('{bad', chapters)).toBeNull();
    expect(
      restoreSnapshot(
        JSON.stringify({ version: 99, chapter: 0, time: 1, preferences: {} }),
        chapters,
      ),
    ).toBeNull();
    expect(
      restoreSnapshot(
        JSON.stringify({ version: 1, chapter: 500, time: 1, preferences: {} }),
        chapters,
      ),
    ).toBeNull();
  });
  it('bounds persisted preferences and copies them independently', () => {
    const p = new Player(chapters);
    p.preferences.voice = 0.3;
    const s = restoreSnapshot(JSON.stringify(p.snapshot()), chapters)!;
    expect(s.preferences.voice).toBe(0.3);
    s.preferences.voice = 1;
    expect(p.preferences.voice).toBe(0.3);
  });
});
