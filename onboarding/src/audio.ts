/** Recorded narration follows the player's clock; no speech service or independent playlist. */
import type { BeatDefinition, ChapterDefinition, Preferences } from './types';
type Cue = { src: string; duration: number; provisional?: boolean; voice?: string };
type Manifest = { cues: Record<string, Cue> };

/** Reject malformed or nonlocal cues without interrupting the captioned lesson. */
function readManifest(value: unknown): Manifest {
  if (!value || typeof value !== 'object' || !('cues' in value))
    throw new Error('Invalid narration manifest');
  const cues = value.cues;
  if (!cues || typeof cues !== 'object' || Array.isArray(cues))
    throw new Error('Invalid narration cues');
  for (const cue of Object.values(cues)) {
    if (
      !cue ||
      typeof cue !== 'object' ||
      typeof cue.src !== 'string' ||
      !/^\/?assets\/audio\/[a-z0-9-]+\.mp3$/.test(cue.src) ||
      !Number.isFinite(cue.duration) ||
      cue.duration <= 0
    )
      throw new Error('Invalid narration cue');
  }
  return { cues: cues as Record<string, Cue> };
}

export class AudioDirector {
  private narration = new Audio();
  private manifest: Manifest = { cues: {} };
  private cueKey = '';
  private enabled = false;
  private context: AudioContext | null = null;
  private abort = new AbortController();
  private generation = 0;
  private blocked = false;
  private lastEffectKey = '';
  private effects = new Map<OscillatorNode, GainNode>();
  onStatus: ((message: string) => void) | null = null;
  constructor() {
    this.narration.preload = 'auto';
    this.narration.addEventListener(
      'error',
      () =>
        this.onStatus?.(
          'Narration unavailable for this beat. Captions and transcript remain available.',
        ),
      { signal: this.abort.signal },
    );
    void fetch(`${import.meta.env.BASE_URL}assets/audio-manifest.json`, {
      signal: this.abort.signal,
    })
      .then((r) => {
        if (!r.ok) throw new Error('Audio manifest unavailable');
        return r.json();
      })
      .then((m) => {
        this.manifest = readManifest(m);
        this.cueKey = '';
      })
      .catch((e) => {
        if (e.name !== 'AbortError')
          this.onStatus?.(
            'Narration assets are unavailable. The complete transcript is available.',
          );
      });
  }
  /** Called only after a user gesture, as required by browser audio policies. */
  unlock() {
    this.enabled = true;
    this.blocked = false;
    try {
      this.context ??= new AudioContext();
      void this.context.resume().catch(() => {});
    } catch {
      /* captions remain the complete alternative */
    }
  }
  sync(
    chapter: ChapterDefinition,
    beat: BeatDefinition,
    time: number,
    playing: boolean,
    p: Preferences,
  ) {
    const key = `${chapter.id}/${beat.id}`,
      cue = this.manifest.cues[key];
    if (key !== this.cueKey) {
      this.stopEffects();
      this.generation++;
      this.narration.pause();
      this.cueKey = key;
      this.blocked = false;
      if (cue) this.narration.src = `${import.meta.env.BASE_URL}${cue.src.replace(/^\//, '')}`;
      else this.narration.removeAttribute('src');
    }
    this.narration.volume = p.voice;
    if (!playing || p.effects === 0) this.stopEffects();
    if (this.enabled && playing && key !== this.lastEffectKey) {
      this.lastEffectKey = key;
      if (p.effects > 0 && this.context?.state === 'running') this.effect(beat.phase, p.effects);
    }
    const position = Math.max(0, time - beat.start);
    const shouldPlay = this.enabled && playing && p.voice > 0 && cue && position < cue.duration;
    if (!shouldPlay) {
      this.narration.pause();
      return;
    }
    if (this.narration.readyState >= 1 && Math.abs(this.narration.currentTime - position) > 0.18)
      this.narration.currentTime = position;
    if (this.narration.paused && !this.blocked) {
      const generation = this.generation;
      void this.narration.play().catch((e) => {
        if (generation !== this.generation || e.name === 'AbortError') return;
        this.blocked = true;
        this.onStatus?.('Enable audio with the sound control. Captions are available.');
      });
    }
  }
  private effect(phase: string, level: number) {
    if (!this.context) return;
    const important = /submit|commit|deny|reject|close|resume|complete|deliver/.test(phase);
    if (!important) return;
    const oscillator = this.context.createOscillator(),
      gain = this.context.createGain();
    const now = this.context.currentTime;
    oscillator.type = 'sine';
    oscillator.frequency.setValueAtTime(
      /deny|reject|close/.test(phase) ? 170 : /commit|complete/.test(phase) ? 660 : 440,
      now,
    );
    gain.gain.setValueAtTime(0, now);
    gain.gain.linearRampToValueAtTime(level * 0.1, now + 0.025);
    gain.gain.exponentialRampToValueAtTime(0.0001, now + 0.22);
    oscillator.connect(gain);
    gain.connect(this.context.destination);
    this.effects.set(oscillator, gain);
    oscillator.start(now);
    oscillator.stop(now + 0.25);
    oscillator.onended = () => {
      this.effects.delete(oscillator);
      oscillator.disconnect();
      gain.disconnect();
    };
  }
  private stopEffects() {
    for (const [oscillator, gain] of this.effects) {
      oscillator.onended = null;
      oscillator.stop();
      oscillator.disconnect();
      gain.disconnect();
    }
    this.effects.clear();
  }
  pause() {
    this.narration.pause();
    this.stopEffects();
    this.generation++;
  }
  metrics() {
    return {
      source: this.narration.currentSrc,
      currentTime: this.narration.currentTime,
      paused: this.narration.paused,
      readyState: this.narration.readyState,
      blocked: this.blocked,
      activeEffects: this.effects.size,
    };
  }
  dispose() {
    this.abort.abort();
    this.pause();
    this.narration.removeAttribute('src');
    this.narration.load();
    if (this.context) void this.context.close();
  }
}
