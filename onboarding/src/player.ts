/** A single narrative clock. Seeking reconstructs state; paused time never accumulates. */
import type {
  ChapterDefinition,
  PlaybackPhase,
  PlayerSnapshot,
  Preferences,
  ViewingMode,
} from './types';

export const defaultPreferences = (): Preferences => ({
  quality: 'auto',
  reducedMotion: false,
  voice: 0.8,
  effects: 0.22,
  ambience: 0.06,
  watch: false,
  captions: true,
});
const volume = (value: unknown, fallback: number): number =>
  typeof value === 'number' && Number.isFinite(value) ? Math.min(1, Math.max(0, value)) : fallback;

/** Treat persisted browser data as untrusted, including schema and numeric bounds. */
export function restoreSnapshot(
  raw: string | null,
  chapters: ChapterDefinition[],
): PlayerSnapshot | null {
  try {
    if (!raw) return null;
    const s = JSON.parse(raw);
    if (
      s.version !== 1 ||
      !Number.isInteger(s.chapter) ||
      s.chapter < 0 ||
      s.chapter >= chapters.length ||
      !Number.isFinite(s.time) ||
      s.time < 0 ||
      s.time > chapters[s.chapter].duration
    )
      return null;
    const p = s.preferences ?? {},
      d = defaultPreferences();
    return {
      version: 1,
      chapter: s.chapter,
      time: s.time,
      preferences: {
        quality: ['auto', 'balanced', 'high'].includes(p.quality) ? p.quality : d.quality,
        reducedMotion: typeof p.reducedMotion === 'boolean' ? p.reducedMotion : d.reducedMotion,
        voice: volume(p.voice, d.voice),
        effects: volume(p.effects, d.effects),
        ambience: volume(p.ambience, d.ambience),
        watch: typeof p.watch === 'boolean' ? p.watch : d.watch,
        captions: typeof p.captions === 'boolean' ? p.captions : d.captions,
      },
    };
  } catch {
    return null;
  }
}

export class Player {
  chapterIndex = 0;
  time = 0;
  phase: PlaybackPhase = 'ready';
  mode: ViewingMode = 'guided';
  preferences = defaultPreferences();
  experiment: string | null = null;
  experimentStep = 0;
  private savedTime = 0;
  constructor(readonly chapters: ChapterDefinition[]) {
    if (!chapters.length || chapters.some((c) => !(c.duration > 0)))
      throw new Error('A journey needs positive chapter durations.');
  }
  get chapter() {
    return this.chapters[this.chapterIndex];
  }
  get beatIndex() {
    return Math.max(
      0,
      this.chapter.beats.findLastIndex((b) => this.time >= b.start),
    );
  }
  get beat() {
    return this.chapter.beats[this.beatIndex];
  }
  get totalDuration() {
    return this.chapters.reduce((sum, c) => sum + c.duration, 0);
  }
  get elapsed() {
    return (
      this.chapters.slice(0, this.chapterIndex).reduce((sum, c) => sum + c.duration, 0) + this.time
    );
  }

  play() {
    if (this.experiment) return;
    this.mode = 'guided';
    if (this.time >= this.chapter.duration) {
      this.continue();
      return;
    }
    this.phase = 'playing';
  }
  pause() {
    if (this.phase === 'playing') this.phase = 'paused';
  }
  toggle() {
    this.phase === 'playing' ? this.pause() : this.play();
  }
  advance(seconds: number) {
    if (
      this.phase !== 'playing' ||
      this.mode !== 'guided' ||
      this.experiment ||
      !Number.isFinite(seconds) ||
      seconds <= 0
    )
      return;
    this.time += seconds;
    while (this.time >= this.chapter.duration) {
      const remaining = this.time - this.chapter.duration;
      this.time = this.chapter.duration;
      if (this.chapterIndex === this.chapters.length - 1) {
        this.phase = 'completed';
        return;
      }
      if (!this.preferences.watch) {
        this.phase = 'held';
        return;
      }
      this.chapterIndex++;
      this.time = remaining;
    }
  }
  seek(time: number) {
    if (!Number.isFinite(time) || this.experiment) return;
    this.time = Math.min(this.chapter.duration, Math.max(0, time));
    this.mode = 'guided';
    if (this.time === this.chapter.duration)
      this.phase = this.chapterIndex === this.chapters.length - 1 ? 'completed' : 'held';
    else if (this.phase !== 'playing') this.phase = 'paused';
  }
  jumpChapter(index: number, play = false) {
    if (!Number.isInteger(index) || index < 0 || index >= this.chapters.length) return;
    this.chapterIndex = index;
    this.time = 0;
    this.mode = 'guided';
    this.experiment = null;
    this.experimentStep = 0;
    this.phase = play ? 'playing' : 'paused';
  }
  nextBeat() {
    if (this.experiment) return;
    const next = this.chapter.beats[this.beatIndex + 1];
    if (next) this.seek(next.start);
    else this.seek(this.chapter.duration);
  }
  previousBeat() {
    if (this.experiment) return;
    const beat = this.beat;
    this.seek(
      this.time > beat.start + 0.05
        ? beat.start
        : this.chapter.beats[Math.max(0, this.beatIndex - 1)].start,
    );
  }
  continue() {
    if (this.chapterIndex < this.chapters.length - 1) this.jumpChapter(this.chapterIndex + 1, true);
    else {
      this.phase = 'completed';
      this.time = this.chapter.duration;
    }
  }
  enterInspection() {
    this.pause();
    this.mode = 'inspection';
  }
  exitInspection() {
    this.mode = 'guided';
  }
  startExperiment(id: string) {
    if (!this.experiment) this.savedTime = this.time;
    this.pause();
    this.mode = 'guided';
    this.experiment = id;
    this.experimentStep = 0;
  }
  resetExperiment() {
    this.experimentStep = 0;
  }
  endExperiment() {
    this.experiment = null;
    this.experimentStep = 0;
    this.time = this.savedTime;
    this.phase = 'paused';
  }
  snapshot(): PlayerSnapshot {
    return {
      version: 1,
      chapter: this.chapterIndex,
      time: this.experiment ? this.savedTime : this.time,
      preferences: { ...this.preferences },
    };
  }
  restore(s: PlayerSnapshot) {
    this.jumpChapter(s.chapter);
    this.time = s.time;
    this.preferences = { ...s.preferences };
    this.phase = 'paused';
  }
}
