/** The documentary shell. Only this module connects controls, player, audio, and presentation. */
import './style.css';
import { CHAPTERS, GLOSSARY } from './content';
import { EXPERIMENTS, experimentResult } from './scenario';
import { Player, restoreSnapshot } from './player';
import { AudioDirector } from './audio';
import { World } from './world';
import { annotations } from './schematics';
import type { SourceAtlas, SourceEntry, PlayerSnapshot } from './types';

const esc = (s: unknown): string =>
  String(s).replace(
    /[&<>"']/g,
    (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!,
  );
const paths: Record<string, string> = {
  cube: '<path d="m12 2 9 5v10l-9 5-9-5V7zM3 7l9 5 9-5M12 12v10"/>',
  arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
  play: '<path d="m9 5 11 7-11 7z"/>',
  pause: '<path d="M9 5v14M15 5v14"/>',
  back: '<path d="M5 5v14m14-14L8 12l11 7z"/>',
  next: '<path d="M19 5v14M5 5l11 7-11 7z"/>',
  grid: '<rect x="3" y="3" width="6" height="6"/><rect x="15" y="3" width="6" height="6"/><rect x="3" y="15" width="6" height="6"/><rect x="15" y="15" width="6" height="6"/>',
  code: '<path d="m8 6-6 6 6 6m8-12 6 6-6 6m-3-16-2 20"/>',
  sound: '<path d="m11 4-6 5H2v6h3l6 5zM15 8q5 4 0 8m3-11q8 7 0 14"/>',
  mute: '<path d="m11 4-6 5H2v6h3l6 5zM16 9l6 6m0-6-6 6"/>',
  settings:
    '<circle cx="12" cy="12" r="3"/><path d="m9 3 6 0 1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1z"/>',
  close: '<path d="m6 6 12 12M18 6 6 18"/>',
  inspect: '<path d="M3 8V3h5m8 0h5v5m0 8v5h-5m-8 0H3v-5"/><circle cx="12" cy="12" r="4"/>',
  book: '<path d="M12 5c-4-3-8-2-10-1v15c4-2 7-1 10 1 3-2 6-3 10-1V4c-3-2-7-2-10 1Zm0 0v15"/>',
  reset: '<path d="M3 10a9 9 0 1 1 2 8M3 4v6h6"/>',
};
const icon = (name: string) =>
  `<svg viewBox="0 0 24 24" aria-hidden="true">${paths[name] ?? paths.cube}</svg>`;
const timeText = (s: number) =>
  `${Math.floor(Math.max(0, s) / 60)}:${String(Math.floor(Math.max(0, s) % 60)).padStart(2, '0')}`;
const $ = <T extends HTMLElement = HTMLElement>(id: string): T => document.getElementById(id) as T;
const player = new Player(CHAPTERS);
const STORAGE_KEY = 'inside-socket-chat/v1';
let saved: PlayerSnapshot | null = null;
try {
  saved = restoreSnapshot(localStorage.getItem(STORAGE_KEY), CHAPTERS);
} catch {
  /* storage may be disabled */
}
const motionPreference = window.matchMedia('(prefers-reduced-motion: reduce)');
player.preferences.reducedMotion = saved?.preferences.reducedMotion ?? motionPreference.matches;
if (saved) player.preferences = { ...saved.preferences };
let started = false;
let world: World | null = null;
let sourceAtlas: SourceAtlas | null = null;
let sourceLoad: Promise<SourceAtlas> | null = null;
let sourceGeneration = 0;
let lastUIKey = '';
let lastSaveTime = 0;
let lastMetricsTime = 0;
let lastFrame = performance.now();
let animationId = 0;
const audio = new AudioDirector();

$('app').innerHTML = `
  <canvas id="world-canvas" aria-label="Interactive three-dimensional architecture cutaway" role="img"></canvas>
  <div id="poster" hidden aria-label="Illustrated architecture alternative"></div>
  <div id="world-labels" aria-hidden="true"></div><div class="shade"></div>
  <header class="site-header">
    <button class="brand" id="home" aria-label="Inside Socket Chat home">${icon('cube')}<span class="brand-name">Inside Socket Chat<span>A SYSTEM, REVEALED</span></span></button>
    <span class="header-note">AN INTERACTIVE TECHNICAL DOCUMENTARY</span>
    <nav class="header-actions" aria-label="Documentary navigation">
      <button class="text-button" data-dialog="chapters">${icon('grid')}Chapters</button>
      <button class="text-button atlas-nav" data-dialog="source">${icon('code')}Source atlas</button>
      <button class="sound-button" id="sound" aria-label="Mute narration">${icon('sound')}<span class="sound-label">SOUND ON</span></button>
      <button class="icon-button" data-dialog="settings" aria-label="Settings">${icon('settings')}</button>
    </nav>
  </header>
  <section class="landing" id="landing" aria-labelledby="landing-title">
    <div class="eyebrow">SOCKET CHAT / C++20</div>
    <h1 id="landing-title">Inside<br><em>Socket Chat.</em></h1>
    <p class="lede">Follow a message through the system.<br>Stop time. Explore how it works.</p>
    <div class="landing-actions"><button id="begin" class="primary-button">Begin the journey ${icon('arrow')}</button><button class="subtle-button" data-dialog="chapters">Explore chapters ${icon('grid')}</button></div>
    <div class="landing-meta"><span>${CHAPTERS.length} CHAPTERS</span><span>${timeText(player.totalDuration)} + EXPLORATION</span><span>YOUR PACE</span></div>
    <button id="resume" class="text-button" ${saved ? '' : 'hidden'}>Continue where you left off ${icon('arrow')}</button>
  </section>
  <footer class="landing-footer" id="landing-footer"><p><strong>A guided look beneath the interface.</strong><br>From a single click to a durable, private conversation.</p><div class="coordinates">AUTHORITATIVE SERVER / THREE CONNECTED CLIENTS<br><b>DESIGNED TO BE EXPLORED</b></div></footer>
  <section id="chapter-heading" class="chapter-heading" hidden aria-labelledby="chapter-title"><div class="eyebrow" id="chapter-index"></div><h1 id="chapter-title"></h1><p id="chapter-objective"></p></section>
  <aside class="scene-badge" id="scene-badge" hidden><span class="state-chip" id="player-status">PAUSED</span><br><strong id="chapter-stage"></strong><br><span class="simulation-note">EXPLANATORY SIMULATION / SYNTHETIC DATA</span></aside>
  <section class="caption-block" id="caption-block" hidden aria-live="polite" aria-atomic="true"><div class="beat-title" id="beat-title"></div><p id="caption"></p><div class="hold-actions" id="hold-actions" hidden><button class="subtle-button" id="hold-inspect">${icon('inspect')}Look inside</button><button class="primary-button" id="continue">Continue ${icon('arrow')}</button></div></section>
  <aside class="inspection-invitation" id="inspection-invitation" hidden><button class="text-button" id="inspect">${icon('inspect')}Stop & inspect</button><button class="text-button source-invitation" data-dialog="source">${icon('code')}See the implementation</button></aside>
  <aside id="inspection-panel" class="inspection-panel" hidden aria-label="Frozen scene inspection"></aside>
  <div class="inspection-hint" id="inspection-hint" hidden>DRAG TO ORBIT · SCROLL TO ZOOM · ESC TO RETURN</div>
  <aside id="experiment-panel" class="experiment-panel" hidden aria-label="Interactive experiment"></aside>
  <div id="outcome-line" class="outcome-line" hidden></div>
  <div id="poster-readout" class="poster-readout" hidden></div>
  <div id="accessible-schematic" class="sr-only" aria-live="polite"></div>
  <footer class="journey-footer" id="journey-footer" hidden>
    <nav class="chapter-segments" aria-label="Jump to chapter">${CHAPTERS.map((c, i) => `<button data-chapter="${i}" title="${esc(c.title)}" aria-label="Chapter ${i + 1}: ${esc(c.title)}"></button>`).join('')}</nav>
    <div class="transport-row"><div class="transport-left"><button class="skip-button" id="previous" aria-label="Previous beat">${icon('back')}</button><button class="play-button" id="play" aria-label="Play">${icon('play')}</button><button class="skip-button" id="next" aria-label="Next beat">${icon('next')}</button><span class="clock-text"><b id="elapsed">0:00</b> / <span id="total-time">${timeText(player.totalDuration)}</span></span></div>
    <label class="scrubber"><span class="sr-only">Chapter position</span><input id="scrubber" type="range" min="0" max="45" step="0.1" value="0" aria-label="Chapter position"><span class="mono" id="chapter-time">0:00</span></label>
    <div class="transport-right"><button class="text-button transcript-button" data-dialog="transcript">${icon('book')}Transcript</button><button class="text-button" data-dialog="glossary">Glossary</button></div></div>
    <div class="keyboard-hint"><span><kbd>SPACE</kbd> PLAY / PAUSE &nbsp;&nbsp; <kbd>← →</kbd> STEP THROUGH</span><span class="simulation-note">BUILT FROM THE ACTUAL C++ IMPLEMENTATION</span></div>
  </footer>
  <span class="loading-indicator" id="loading">PREPARING THE CUTAWAY</span>
  <div class="static-notice" id="static-notice" hidden>ILLUSTRATED PRESENTATION</div>
  <aside class="notice" id="notice" role="status" hidden><button id="dismiss-notice" aria-label="Dismiss notification">×</button><span id="notice-text"></span></aside>
  <dialog id="overlay" aria-labelledby="dialog-title"><header class="dialog-header"><div><span class="eyebrow" id="dialog-kicker"></span><h2 id="dialog-title"></h2></div><button id="dialog-close" class="icon-button" aria-label="Close overlay">${icon('close')}</button></header><div id="dialog-body" class="dialog-body"></div></dialog>
  <output id="render-metrics" hidden></output><output id="audio-metrics" hidden></output><output id="semantic-state" hidden></output>
`;

function notify(message: string) {
  $('notice-text').textContent = message;
  $('notice').hidden = false;
}
audio.onStatus = notify;
function persist() {
  if (!started) return;
  saved = player.snapshot();
  $('resume').hidden = false;
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(saved));
  } catch {
    /* keep the current run working */
  }
}
function update() {
  lastUIKey = '';
  syncUI();
  persist();
}
function begin(resume: boolean) {
  if (resume && saved) player.restore(saved);
  else player.jumpChapter(0);
  started = true;
  audio.unlock();
  player.play();
  update();
}
function poster() {
  $('poster').hidden = false;
  $('world-canvas').hidden = true;
  $('static-notice').hidden = false;
  $('loading').hidden = true;
  $('poster').innerHTML =
    `<svg class="poster-world" viewBox="0 0 1200 800" role="img" aria-label="Alice sends through the server to Bob; Charlie remains outside the private room"><defs><linearGradient id="plate" x2="1" y2="1"><stop stop-color="#24373e"/><stop offset="1" stop-color="#101e26"/></linearGradient></defs><g fill="none" stroke="#334c53" stroke-width="2"><path d="M410 480h200V350h240M610 350v-150M610 350h330v220"/></g><g fill="url(#plate)" stroke="#526665"><path d="m500 350 110-65 110 65-110 65z"/><path d="m500 350 110 65v90l-110-65zm220 0-110 65v90l110-65z"/><path d="m540 290 70-40 70 40-70 40z"/><path d="m340 480 70-40 70 40-70 40zm440-130 70-40 70 40-70 40zm90 220 70-40 70 40-70 40z"/><path d="m550 200 60-35 60 35-60 35z"/></g><g><text x="570" y="240">SERVER</text><text x="370" y="550">ALICE</text><text x="825" y="410">BOB</text><text x="900" y="635">CHARLIE</text><text x="570" y="150">SQLITE / WAL</text></g><circle cx="490" cy="480" r="5" fill="#f4b46a"/><circle cx="755" cy="350" r="5" fill="#79daca"/></svg>`;
}

try {
  if (new URLSearchParams(location.search).get('graphics') === 'static') poster();
  else {
    world = new World($('world-canvas'), $('world-labels'), (lost) => {
      player.pause();
      audio.pause();
      if (lost) {
        poster();
        notify(
          'Graphics context was lost. Your place is preserved in the illustrated presentation.',
        );
      } else {
        $('poster').hidden = true;
        $('world-canvas').hidden = false;
        $('static-notice').hidden = true;
        notify('Graphics restored. Press Play when you are ready.');
      }
      update();
    });
    world.onAssetError = notify;
    world.setQuality(player.preferences.quality);
    world.onSelect = (key) => {
      if (!started) return;
      const mapping: Record<string, number> = {
        alice: 2,
        server: 7,
        bob: 7,
        charlie: 7,
        storage: 9,
      };
      player.jumpChapter(mapping[key] ?? player.chapterIndex);
      player.enterInspection();
      update();
    };
    void world.ready.then(() => {
      $('loading').hidden = true;
    });
  }
} catch {
  poster();
  notify(
    'This browser cannot start WebGL2. The illustrated journey, narration, experiments, and source atlas are available.',
  );
}

function showInspection() {
  player.enterInspection();
  audio.pause();
  update();
}
function closeInspection() {
  player.exitInspection();
  update();
}

function syncUI() {
  document.body.classList.toggle('journey-started', started);
  $('landing').hidden = started;
  $('landing-footer').hidden = started;
  $('journey-footer').hidden = !started;
  $('scene-badge').hidden = !started;
  const inspection = player.mode === 'inspection',
    experiment = player.experiment !== null;
  $('chapter-heading').hidden = !started || inspection || experiment;
  $('caption-block').hidden = !started || inspection || experiment || !player.preferences.captions;
  $('inspection-invitation').hidden = !started || inspection || experiment;
  $('inspection-panel').hidden = !inspection || !started;
  $('inspection-hint').hidden = !inspection || !started;
  $('experiment-panel').hidden = !experiment;
  $('outcome-line').hidden = !experiment;
  $('world-canvas').dataset.inspection = String(inspection);
  $('app').dataset.phase = player.phase;
  $('app').dataset.mode = player.mode;
  $('app').dataset.chapter = player.chapter.id;
  $('app').dataset.beat = player.beat.id;
  $('app').dataset.time = player.time.toFixed(3);
  $('app').dataset.experiment = player.experiment ?? '';
  $('elapsed').textContent = timeText(player.elapsed);
  $('chapter-time').textContent = timeText(player.time);
  const range = $<HTMLInputElement>('scrubber');
  range.max = String(player.chapter.duration);
  range.value = String(player.time);
  range.setAttribute(
    'aria-valuetext',
    `${timeText(player.time)} of ${timeText(player.chapter.duration)}`,
  );
  range.disabled = experiment;
  const playing = player.phase === 'playing';
  $('play').setAttribute(
    'aria-label',
    playing ? 'Pause' : player.phase === 'held' ? 'Continue to next chapter' : 'Play',
  );
  $('play').innerHTML = icon(playing ? 'pause' : 'play');
  ($('play') as HTMLButtonElement).disabled = experiment;
  ($('previous') as HTMLButtonElement).disabled = experiment;
  ($('next') as HTMLButtonElement).disabled = experiment;
  $('sound').classList.toggle('muted', player.preferences.voice === 0);
  $('sound').setAttribute(
    'aria-label',
    player.preferences.voice === 0 ? 'Enable narration' : 'Mute narration',
  );
  $('sound').innerHTML =
    `${icon(player.preferences.voice > 0 ? 'sound' : 'mute')}<span class="sound-label">SOUND ${player.preferences.voice > 0 ? 'ON' : 'OFF'}</span>`;
  const state = inspection
    ? 'TIME FROZEN'
    : experiment
      ? 'EXPERIMENT'
      : player.phase === 'playing'
        ? 'JOURNEY IN PROGRESS'
        : player.phase === 'held'
          ? 'TAKE A CLOSER LOOK'
          : player.phase === 'completed'
            ? 'JOURNEY COMPLETE'
            : 'TIME PAUSED';
  $('player-status').textContent = state;
  const key = [
    started,
    player.chapter.id,
    player.beat.id,
    player.phase,
    player.mode,
    player.experiment,
    player.experimentStep,
  ].join('/');
  if (key === lastUIKey) return;
  lastUIKey = key;
  $('chapter-index').textContent =
    `CHAPTER ${String(player.chapterIndex + 1).padStart(2, '0')} / ${CHAPTERS.length}`;
  $('chapter-title').textContent = player.chapter.title;
  $('chapter-objective').textContent = player.chapter.subtitle;
  $('chapter-stage').textContent = player.chapter.stage.toUpperCase();
  $('beat-title').textContent = player.beat.title;
  $('caption').textContent =
    player.phase === 'held' || player.phase === 'completed'
      ? player.chapter.takeaway
      : player.beat.caption;
  $('hold-actions').hidden = player.phase !== 'held' && player.phase !== 'completed';
  $('continue').innerHTML =
    player.phase === 'completed'
      ? `Explore the source ${icon('code')}`
      : `Continue ${icon('arrow')}`;
  document.querySelectorAll<HTMLButtonElement>('[data-chapter]').forEach((el) => {
    const i = Number(el.dataset.chapter);
    el.dataset.state = i < player.chapterIndex ? 'done' : i === player.chapterIndex ? 'active' : '';
    if (i === player.chapterIndex) el.setAttribute('aria-current', 'step');
    else el.removeAttribute('aria-current');
  });
  if (inspection) renderInspection();
  if (experiment) renderExperiment();
  const result = player.experiment
    ? experimentResult(player.experiment, player.experimentStep)
    : null;
  const specs = annotations(player.chapter, player.beat, result, player.experiment);
  $('accessible-schematic').textContent = specs.map((s) => `${s.title}: ${s.value}`).join('. ');
  $('poster-readout').hidden = $('poster').hidden || !started;
  $('poster-readout').innerHTML = specs
    .map((s) => `<div><span>${esc(s.title)}</span><strong>${esc(s.value)}</strong></div>`)
    .join('');
}

function renderInspection() {
  $('inspection-panel').innerHTML =
    `<div class="panel-topline"><span class="eyebrow">TIME, HELD STILL</span><button class="icon-button" id="exit-inspection" aria-label="Return to guided view">${icon('close')}</button></div>
    <h2>${esc(player.beat.title)}</h2><p>${esc(player.beat.detail)}</p><div class="divider"></div>
    <div class="mini-heading">THE IMPLEMENTATION</div><button class="text-button" data-dialog="source">${icon('code')}Open source evidence ${icon('arrow')}</button>
    ${player.chapter.experiments.length ? `<div class="divider"></div><div class="mini-heading">CHANGE ONE THING</div>${player.chapter.experiments.map((id) => `<button class="experiment-choice" data-experiment="${esc(id)}">${esc(EXPERIMENTS.find((e) => e.id === id)?.title ?? id)}</button>`).join('')}` : ''}
    <div class="divider"></div><p>Drag to orbit. Scroll to zoom.<br>Use the source atlas for keyboard-accessible details of every component.</p>`;
}
function renderExperiment() {
  const def = EXPERIMENTS.find((e) => e.id === player.experiment)!;
  const result = experimentResult(def.id, player.experimentStep),
    step = def.steps[player.experimentStep];
  $('experiment-panel').innerHTML =
    `<div class="panel-topline"><span class="eyebrow">CHANGE ONE THING</span><button class="icon-button" id="exit-experiment" aria-label="Leave experiment">${icon('close')}</button></div><h2>${esc(def.title)}</h2><p>${esc(def.question)}</p>
    <div class="experiment-step">STEP ${player.experimentStep + 1} / ${def.steps.length}</div><p>${esc(step.explanation)}</p>
    <span class="result-status" data-status="${result.status}">${esc(result.status)} / ${esc(result.title)}</span>
    <dl class="experiment-metrics">${result.metrics.map((m) => `<div><dt>${esc(m.label)}</dt><dd>${esc(m.value)}</dd></div>`).join('')}</dl>
    <p>${esc(result.explanation)}</p><div class="experiment-actions">${player.experimentStep < def.steps.length - 1 ? `<button class="primary-button" id="experiment-next">${esc(def.steps[player.experimentStep + 1].action)} ${icon('arrow')}</button>` : `<button class="primary-button" id="experiment-done">Return to journey ${icon('arrow')}</button>`}<button id="experiment-reset" class="text-button">${icon('reset')}Reset</button></div>`;
  $('outcome-line').textContent = result.title;
  $('semantic-state').textContent = JSON.stringify(result);
}

async function atlas(): Promise<SourceAtlas> {
  if (sourceAtlas) return sourceAtlas;
  sourceLoad ??= fetch(`${import.meta.env.BASE_URL}content/source-atlas.json`)
    .then((r) => {
      if (!r.ok) throw new Error('Source catalogue could not load.');
      return r.json();
    })
    .then((data) => {
      sourceAtlas = data;
      return data;
    })
    .catch((e) => {
      sourceLoad = null;
      throw e;
    });
  return sourceLoad;
}
const dialog = $<HTMLDialogElement>('overlay');
function closeDialog() {
  sourceGeneration++;
  dialog.close();
  update();
}
async function openDialog(kind: string) {
  player.pause();
  audio.pause();
  syncUI();
  const titles: Record<string, [string, string]> = {
    chapters: ['THE JOURNEY', 'Thirteen ways into the system'],
    settings: ['MAKE IT YOURS', 'Playback & presentation'],
    transcript: ['EVERY WORD, AT YOUR PACE', 'The complete narration'],
    glossary: ['A FEW USEFUL TERMS', 'The language of the system'],
    source: ['THE IMPLEMENTATION', 'Follow the evidence'],
  };
  const [kicker, title] = titles[kind] ?? titles.chapters;
  $('dialog-kicker').textContent = kicker;
  $('dialog-title').textContent = title;
  dialog.className = `${kind}-dialog`;
  $('dialog-body').className = kind === 'source' ? '' : 'dialog-body';
  const generation = ++sourceGeneration;
  if (kind === 'chapters') {
    $('dialog-body').innerHTML =
      `<p class="dialog-intro">Follow the complete journey, or enter at the question you care about. ${timeText(player.totalDuration)} of authored playback, with time to stop and explore.</p><div class="chapter-grid">${CHAPTERS.map((c, i) => `<button class="chapter-card" data-chapter="${i}" data-active="${started && i === player.chapterIndex}"><span class="chapter-number">${String(i + 1).padStart(2, '0')}</span><span><h3>${esc(c.title)}</h3><p>${esc(c.subtitle)}</p></span><time>${timeText(c.duration)}</time></button>`).join('')}</div>`;
  } else if (kind === 'settings') {
    const p = player.preferences;
    $('dialog-body').innerHTML =
      `<label class="setting-row"><span>Graphics quality<small>Auto starts with the balanced laptop presentation.</small></span><select id="quality" aria-label="Graphics quality">${['auto', 'balanced', 'high'].map((q) => `<option value="${q}" ${p.quality === q ? 'selected' : ''}>${q[0].toUpperCase() + q.slice(1)}</option>`).join('')}</select></label>
    <label class="setting-row"><span>Reduced motion<small>Stable camera views and stepped reveals.</small></span><input id="reduced-motion" type="checkbox" ${p.reducedMotion ? 'checked' : ''}></label>
    <label class="setting-row"><span>Continuous watch<small>Advance automatically at chapter endings.</small></span><input id="watch-mode" type="checkbox" ${p.watch ? 'checked' : ''}></label>
    <label class="setting-row"><span>Captions<small>Short explanations alongside the scene.</small></span><input id="captions-setting" type="checkbox" ${p.captions ? 'checked' : ''}></label>
    <label class="setting-row"><span>Narration<small>English narration. The complete transcript is always available.</small></span><input id="voice-volume" aria-label="Narration volume" type="range" min="0" max="1" step="0.05" value="${p.voice}"></label>
    <label class="setting-row"><span>Sound effects</span><input id="effects-volume" aria-label="Sound effects volume" type="range" min="0" max="1" step="0.05" value="${p.effects}"></label>
    <p class="settings-foot">Progress and preferences stay in this browser. This is a deterministic explanation using synthetic data, with no connection to the chat server. Your system's reduced-motion setting is respected on the first visit.</p><button class="danger-button" id="reset-progress">Reset saved progress</button><br><a class="text-button" href="${import.meta.env.BASE_URL}legacy/index.html">Open the preserved original ↗</a>`;
  } else if (kind === 'transcript') {
    let offset = 0;
    $('dialog-body').innerHTML =
      `<p class="dialog-intro">Select any beat to return to that moment. Narration is optional; every explanation is available here and through inspection.</p>${CHAPTERS.map(
        (c, i) => {
          const base = offset;
          offset += c.duration;
          return `<h3 class="transcript-chapter">${String(i + 1).padStart(2, '0')} / ${esc(c.title)}</h3>${c.beats.map((b) => `<article class="transcript-entry"><button data-seek-chapter="${i}" data-seek-time="${b.start}"><time>${timeText(base + b.start)}</time><h3>${esc(b.title)}</h3></button><p>${esc(b.narration)}</p></article>`).join('')}`;
        },
      ).join('')}`;
  } else if (kind === 'glossary') {
    $('dialog-body').innerHTML =
      `<dl class="glossary-list">${GLOSSARY.map((g) => `<div><dt>${esc(g.term)}</dt><dd>${esc(g.meaning)}</dd></div>`).join('')}</dl>`;
  } else {
    $('dialog-body').innerHTML = '<div class="dialog-body">Loading the source catalogue…</div>';
    if (!dialog.open) dialog.showModal();
    try {
      const data = await atlas();
      if (generation !== sourceGeneration || !dialog.open) return;
      const recommended = player.beat.sourceIds
        .map((id) => data.sources.find((s) => s.id === id))
        .filter((s): s is SourceEntry => Boolean(s));
      const remaining = data.sources.filter((s) => !player.beat.sourceIds.includes(s.id));
      $('dialog-body').innerHTML =
        `<div class="source-layout"><nav class="source-menu" aria-label="Source symbols"><div class="mini-heading">THIS MOMENT</div>${recommended.map(sourceButton).join('')}<div class="mini-heading">ALL BOUNDARIES</div>${remaining.map(sourceButton).join('')}<div class="mini-heading">${data.files.length} COMPLETE FILES</div>${data.files.map((f) => `<button data-file="${esc(f.path)}">${esc(f.path)}</button>`).join('')}</nav><section class="source-detail" id="source-detail"></section></div>`;
      showSource((recommended[0] ?? data.sources[0]).id);
    } catch (e) {
      if (generation !== sourceGeneration) return;
      $('dialog-body').innerHTML =
        `<div class="dialog-body"><p class="view-error">${esc(e instanceof Error ? e.message : e)} The transcript and journey remain available.</p><button class="primary-button" data-dialog="source">Retry ${icon('reset')}</button></div>`;
    }
    return;
  }
  if (!dialog.open) dialog.showModal();
}
function sourceButton(s: SourceEntry) {
  return `<button data-source="${esc(s.id)}">${esc(s.symbol)}</button>`;
}
async function digest(text: string): Promise<string> {
  const bytes = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(bytes)].map((b) => b.toString(16).padStart(2, '0')).join('');
}
async function showSource(id: string) {
  const entry = sourceAtlas?.sources.find((s) => s.id === id);
  if (!entry) return;
  const generation = ++sourceGeneration;
  if ((await digest(entry.code)) !== entry.hash) {
    if (generation === sourceGeneration)
      $('source-detail').textContent =
        'This source snapshot failed its integrity check. Rebuild the source catalogue before using it as evidence.';
    return;
  }
  if (generation !== sourceGeneration || !dialog.open) return;
  document
    .querySelectorAll<HTMLElement>('[data-source]')
    .forEach((el) => (el.dataset.active = String(el.dataset.source === id)));
  const description = entry as SourceEntry & { explanation?: string; summary?: string };
  $('source-detail').innerHTML =
    `<div class="mini-heading">REVISION ${esc(sourceAtlas!.revision)} / SOURCE SNAPSHOT</div><h3>${esc(entry.path)}:${entry.startLine}–${entry.endLine}</h3><p>${esc(description.summary ?? entry.symbol)}</p><pre><code>${esc(entry.code)}</code></pre><p>${esc(description.explanation ?? '')}</p><span class="source-hash">SHA-256 ${esc(entry.hash.slice(0, 16))}… · checked against repository source</span>`;
}
async function showFile(path: string) {
  const file = sourceAtlas?.files.find((f) => f.path === path);
  if (!file) return;
  const generation = ++sourceGeneration;
  $('source-detail').textContent = 'Loading source…';
  try {
    const response = await fetch(
      `${import.meta.env.BASE_URL}content/source-files/${encodeURIComponent(path.replaceAll('/', '__'))}.txt`,
    );
    if (!response.ok) throw new Error('The source file could not load.');
    const text = await response.text();
    if (generation !== sourceGeneration || !dialog.open) return;
    if ((await digest(text)) !== file.hash)
      throw new Error('This source file failed its integrity check. Rebuild the source catalogue.');
    if (generation !== sourceGeneration || !dialog.open) return;
    $('source-detail').innerHTML =
      `<h3>${esc(path)}</h3><p>${esc(file.role)} · ${file.lineCount} lines</p><pre><code>${esc(text)}</code></pre>`;
  } catch (e) {
    if (generation === sourceGeneration)
      $('source-detail').textContent = e instanceof Error ? e.message : String(e);
  }
}

// Delegated controls remain stable while contextual content is replaced.
document.addEventListener('click', (e) => {
  const el = (e.target as HTMLElement).closest<HTMLElement>('button,a');
  if (!el) return;
  if (el.dataset.dialog) {
    void openDialog(el.dataset.dialog);
    return;
  }
  if (el.dataset.chapter !== undefined) {
    started = true;
    player.jumpChapter(Number(el.dataset.chapter));
    if (dialog.open) closeDialog();
    update();
    return;
  }
  if (el.dataset.seekChapter !== undefined) {
    started = true;
    player.jumpChapter(Number(el.dataset.seekChapter));
    player.seek(Number(el.dataset.seekTime));
    closeDialog();
    update();
    return;
  }
  if (el.dataset.experiment) {
    player.startExperiment(el.dataset.experiment);
    update();
    return;
  }
  if (el.dataset.source) {
    sourceGeneration++;
    showSource(el.dataset.source);
    return;
  }
  if (el.dataset.file) {
    void showFile(el.dataset.file);
    return;
  }
  switch (el.id) {
    case 'begin':
      begin(false);
      break;
    case 'resume':
      begin(true);
      break;
    case 'home':
      player.pause();
      audio.pause();
      persist();
      if (player.experiment) player.endExperiment();
      player.exitInspection();
      started = false;
      update();
      break;
    case 'play':
      audio.unlock();
      player.toggle();
      update();
      break;
    case 'previous':
      player.previousBeat();
      update();
      break;
    case 'next':
      player.nextBeat();
      update();
      break;
    case 'inspect':
    case 'hold-inspect':
      showInspection();
      break;
    case 'exit-inspection':
      closeInspection();
      break;
    case 'continue':
      if (player.phase === 'completed') void openDialog('source');
      else {
        audio.unlock();
        player.continue();
        update();
      }
      break;
    case 'sound':
      audio.unlock();
      player.preferences.voice = player.preferences.voice > 0 ? 0 : 0.8;
      update();
      break;
    case 'dialog-close':
      closeDialog();
      break;
    case 'dismiss-notice':
      $('notice').hidden = true;
      break;
    case 'experiment-next': {
      const def = EXPERIMENTS.find((x) => x.id === player.experiment)!;
      player.experimentStep = Math.min(def.steps.length - 1, player.experimentStep + 1);
      update();
      break;
    }
    case 'experiment-reset':
      player.resetExperiment();
      update();
      break;
    case 'experiment-done':
    case 'exit-experiment':
      player.endExperiment();
      update();
      break;
    case 'reset-progress':
      try {
        localStorage.removeItem(STORAGE_KEY);
      } catch {
        /* no saved storage */
      }
      saved = null;
      player.jumpChapter(0);
      started = false;
      $('resume').hidden = true;
      closeDialog();
      notify('Saved progress cleared.');
      break;
  }
});
document.addEventListener('input', (e) => {
  const input = e.target as HTMLInputElement;
  switch (input.id) {
    case 'scrubber':
      player.pause();
      audio.pause();
      player.seek(Number(input.value));
      break;
    case 'voice-volume':
      player.preferences.voice = Number(input.value);
      audio.unlock();
      break;
    case 'effects-volume':
      player.preferences.effects = Number(input.value);
      break;
  }
  update();
});
document.addEventListener('change', (e) => {
  const input = e.target as HTMLInputElement;
  switch (input.id) {
    case 'quality':
      player.preferences.quality = input.value as 'auto' | 'balanced' | 'high';
      world?.setQuality(player.preferences.quality);
      break;
    case 'reduced-motion':
      player.preferences.reducedMotion = input.checked;
      break;
    case 'watch-mode':
      player.preferences.watch = input.checked;
      break;
    case 'captions-setting':
      player.preferences.captions = input.checked;
      break;
  }
  update();
});
dialog.addEventListener('cancel', () => {
  sourceGeneration++;
  update();
});
document.addEventListener('keydown', (e) => {
  if (dialog.open) return;
  if (e.key === 'Escape') {
    if (player.experiment) player.endExperiment();
    else player.exitInspection();
    update();
    return;
  }
  if (
    !started ||
    (e.target as HTMLElement).closest('input,select,textarea,[contenteditable=true],button,a')
  )
    return;
  if (e.code === 'Space') {
    e.preventDefault();
    audio.unlock();
    player.toggle();
    update();
  } else if (e.key === 'ArrowRight') {
    e.preventDefault();
    player.nextBeat();
    update();
  } else if (e.key === 'ArrowLeft') {
    e.preventDefault();
    player.previousBeat();
    update();
  }
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    player.pause();
    audio.pause();
    persist();
    syncUI();
  }
  lastFrame = performance.now();
});
motionPreference.addEventListener('change', () => {
  player.preferences.reducedMotion = motionPreference.matches;
  update();
});
window.addEventListener('pagehide', () => {
  persist();
  audio.pause();
});

function frame(now: number) {
  const delta = Math.max(0, (now - lastFrame) / 1000);
  lastFrame = now;
  if (!document.hidden) {
    if (started && !dialog.open) player.advance(Math.min(delta, 0.25));
    const result = player.experiment
      ? experimentResult(player.experiment, player.experimentStep)
      : null;
    const rendered = world?.render({
      chapter: player.chapter,
      beat: player.beat,
      time: player.time,
      landing: !started,
      inspection: player.mode === 'inspection',
      reducedMotion: player.preferences.reducedMotion,
      experiment: result,
      experimentId: player.experiment,
      experimentStep: player.experimentStep,
    });
    if (delta < 0.25 && rendered && started && player.phase === 'playing')
      world?.recordFrame(delta * 1000);
    audio.sync(
      player.chapter,
      player.beat,
      player.time,
      started && player.phase === 'playing' && !dialog.open && !player.experiment,
      player.preferences,
    );
    $('audio-metrics').textContent = JSON.stringify(audio.metrics());
    syncUI();
    if (now - lastSaveTime > 1500) {
      lastSaveTime = now;
      persist();
    }
    if (now - lastMetricsTime > 1000) {
      lastMetricsTime = now;
      $('render-metrics').textContent = JSON.stringify({
        chapterId: player.chapter.id,
        beatId: player.beat.id,
        ...(world?.metrics() ?? { quality: 'illustrated' }),
      });
    }
  }
  animationId = requestAnimationFrame(frame);
}
syncUI();
animationId = requestAnimationFrame(frame);
if (import.meta.hot)
  import.meta.hot.dispose(() => {
    cancelAnimationFrame(animationId);
    world?.dispose();
    audio.dispose();
  });
