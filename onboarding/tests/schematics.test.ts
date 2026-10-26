import { describe, it, expect } from 'vitest';
import { annotations, membersAt } from '../src/schematics';
import { CHAPTERS } from '../src/content';
import { experimentResult } from '../src/scenario';
describe('explicit membership presentation', () => {
  it('does not treat creator-only join phase as Bob joining', () => {
    const chapter = CHAPTERS.find((c) => c.stage === 'groups')!;
    const beat = { ...chapter.beats[0], phase: 'join', members: ['alice'] };
    expect(annotations(chapter, beat, null, null).find((a) => a.title === 'BOB')!.value).toContain(
      '≠ joined',
    );
  });
  it('retains Bob when a later request is denied', () => {
    const chapter = CHAPTERS.find((c) => c.stage === 'groups')!;
    const beat = { ...chapter.beats[0], phase: 'deny', members: ['alice', 'bob'] };
    expect(membersAt(beat, null)).toEqual(['alice', 'bob']);
    expect(annotations(chapter, beat, null, null).find((a) => a.title === 'BOB')!.value).toContain(
      'member',
    );
  });
  it('uses the experiment snapshot instead of inheriting unrelated chapter state', () => {
    const chapter = CHAPTERS.find((c) => c.stage === 'groups')!;
    const beat = { ...chapter.beats[0], members: ['alice', 'bob'] };
    expect(membersAt(beat, experimentResult('nonmember', 0))).toEqual(['alice']);
    expect(membersAt(beat, experimentResult('nonmember', 1))).toEqual(['alice', 'bob']);
  });
});
