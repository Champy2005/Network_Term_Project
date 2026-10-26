/** Observable audio lifecycle under pause, mute, and unavailable narration. */
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { AudioDirector } from '../src/audio';
import { CHAPTERS } from '../src/content';
import { defaultPreferences } from '../src/player';

const nodes: { stop: ReturnType<typeof vi.fn>; disconnect: ReturnType<typeof vi.fn> }[] = [];
class Media {
  preload = '';
  src = '';
  volume = 1;
  paused = true;
  currentTime = 0;
  readyState = 2;
  get currentSrc() {
    return this.src;
  }
  addEventListener() {}
  pause() {
    this.paused = true;
  }
  play() {
    this.paused = false;
    return Promise.resolve();
  }
  removeAttribute() {
    this.src = '';
  }
  load() {}
}
class Context {
  state = 'running';
  currentTime = 0;
  destination = {};
  resume() {
    return Promise.resolve();
  }
  close() {
    return Promise.resolve();
  }
  createOscillator() {
    const node = {
      type: '',
      frequency: { setValueAtTime() {} },
      connect() {},
      start() {},
      stop: vi.fn(),
      disconnect: vi.fn(),
      onended: null,
    };
    nodes.push(node);
    return node;
  }
  createGain() {
    return {
      gain: {
        setValueAtTime() {},
        linearRampToValueAtTime() {},
        exponentialRampToValueAtTime() {},
      },
      connect() {},
      disconnect() {},
    };
  }
}
beforeEach(() => {
  nodes.length = 0;
  vi.stubGlobal('Audio', Media);
  vi.stubGlobal('AudioContext', Context);
  vi.stubGlobal(
    'fetch',
    vi.fn(async () => ({ ok: true, json: async () => ({ cues: {} }) })),
  );
});
afterEach(() => vi.unstubAllGlobals());

it('stops active effects immediately on pause even with narration muted', () => {
  const audio = new AudioDirector();
  audio.unlock();
  const chapter = CHAPTERS[0],
    beat = chapter.beats[1];
  audio.sync(chapter, beat, beat.start, true, { ...defaultPreferences(), voice: 0, effects: 1 });
  expect(audio.metrics().activeEffects).toBe(1);
  expect(nodes[0].stop).toHaveBeenCalledWith(0.25);
  audio.pause();
  expect(nodes[0].stop).toHaveBeenLastCalledWith();
  expect(nodes[0].disconnect).toHaveBeenCalledOnce();
  expect(audio.metrics().activeEffects).toBe(0);
  expect(audio.metrics().paused).toBe(true);
  audio.dispose();
});

it('cancels the prior chapter effect when another beat is selected', () => {
  const audio = new AudioDirector();
  audio.unlock();
  const chapter = CHAPTERS[0];
  audio.sync(chapter, chapter.beats[1], 9, true, defaultPreferences());
  audio.sync(chapter, chapter.beats[2], 18, false, defaultPreferences());
  expect(nodes[0].stop).toHaveBeenLastCalledWith();
  expect(audio.metrics().activeEffects).toBe(0);
  audio.dispose();
});
