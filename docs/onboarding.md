# Inside Socket Chat

The `onboarding/` application is a browser-based technical documentary of the
current C++ project. It uses synthetic examples and deterministic explanatory
models. It does not connect to the chat server or establish assignment acceptance.

## Run and build

Use Node.js 24.13 or newer with npm. From the repository root:

```text
cd onboarding
npm ci
npm run dev
```

Open the loopback URL printed by Vite. Start with **Begin the journey**, choose
a chapter, or resume saved progress. The exact chapter durations supplied in
the design sum to **945 seconds (15:45)**; the earlier 17:25 total was an arithmetic
error. Inspection, experiments, source reading, and holds add time at the
learner's pace. The 13 chapters contain 71 narrated beats.

```text
npm run build
npm run preview
```

Serve `onboarding/dist/` as static content. The build uses relative URLs so it
can live under a hosting subdirectory. Opening the source HTML as a `file:` URL
does not run a Vite application. Hosting/publishing is a separate human action.
There are no CDN, account, analytics, or chat-server dependencies at runtime.
The original four-file visualiser is preserved at `/legacy/index.html` under
the same local server; its source files were copied before replacing the entry.

## Controls and learning paths

- Space plays or pauses; arrow keys move between semantic beats when focus is
  on the page. Buttons retain normal keyboard behavior.
- A chapter holds at its ending. Continue advances; Continuous Watch in
  Settings advances automatically.
- Stop & inspect freezes the narrative and opens the current mechanism.
  Drag to orbit and scroll to zoom; Escape returns to the authored view.
- Each experiment starts at a deterministic state. Advance through its actions,
  reset it, and leave to recover the original journey position.
- The chapter slider seeks without replaying side effects. Camera and cutaway
  poses are evaluated from absolute chapter time.
- Transcript, glossary, chapters, and source atlas are available through
  keyboard-accessible HTML controls. Opening an overlay pauses the lesson.
- Progress/preferences remain in this browser. Reset saved progress clears them.

Reduced Motion uses stable camera poses and stepped scene changes. Settings
honors the system preference on first visit. `?graphics=static` explicitly selects
the illustrated presentation; unsupported/lost WebGL also uses that presentation
while preserving the journey. Captions, narration, experiments, transcript,
and source inspection remain available.

## Architecture and content ownership

The player owns one narrative clock. The curriculum defines the sequence and
meaning. Small frame/authority/session models produce experiment outcomes.
The director computes camera and reveal poses; the Three.js renderer presents
them without deciding authorization. Recorded audio follows the same clock.
No live C++ API, wire tag, frame format, or server behavior changes.

The source atlas is lazy-loaded. It contains curated symbol snippets and metadata
for 34 full files, whose text is fetched individually. Snippets and full files
carry SHA-256 hashes. Browser display checks those hashes, and the content tool
compares them with actual repository bytes. Build fails on stale evidence.

```text
npm run content:check
npm run content:update
```

Use `content:update` only after reviewing how a source change affects the
explanation. Updating hashes does not prove a narration or diagram is accurate.

Custom client/server/storage GLBs are generated from original authoring source.
The three models total about 3.34 MB and preserve named semantic pivots for
disassembly. The current assets are reproducibly authored with Node/Three.js,
rather than hand-authored Blender scenes. The asset authoring guide records
provenance and regeneration steps. No raster texture compression is needed for
these untextured material/geometry assets.

```text
npm run assets:build
npm run audio:build
npm run assets:check
```

The checked-in 71 MP3 cues are **provisional Microsoft Zira synthesis**, produced
offline with Windows System.Speech and FFmpeg. They total approximately 4.01 MB.
Their text hashes and measured durations are checked against the current beats.
Original geometry, generator source, and narration text have explicit original-work
provenance. Rendered voice files are excluded from the CC0 dedication; their
redistribution terms require review before external distribution. A polished
recording and listening review remain release gates.

## Verification

```text
npm test
npm run build
npx playwright install chromium
npm run test:browser
npm run benchmark
```

The production build runs content and asset validation, TypeScript checking,
and bundling. Vitest checks the player, director, and explanatory models.
Playwright exercises the built application, real rendered freeze behavior,
experiments, source loading/integrity, delayed audio, settings, responsive
controls, and illustrated fallback. Diagnostic metrics are exposed in hidden
outputs for reproducible browser evidence.

The benchmark uses **headless Chromium with SwiftShader**. It records frame
times, draw calls, triangles, and resource counts, and enforces geometry/draw
budgets. This does not prove the Iris Xe laptop FPS or cold-network loading
targets. Record physical hardware/browser/viewport and network conditions
separately before claiming those targets.

The repository's `python tools/verify.py full` remains the C++/documentation
acceptance gate. Keep the working tree stable during that run: it deliberately
marks a run incomplete if tracked or untracked inputs change during verification.

## Remaining production acceptance

The application implementation and its automated checks are separate from the
full cinematic release standard. Still requiring explicit evidence:

- Human visual/motion review across all chapters against the intended premium
  reference quality, including slow camera sweeps on physical hardware.
- Final mastered narration and listening/redistribution review.
- Physical integrated-GPU and cold-network performance measurements.
- Dedicated reduced-detail GLBs; current quality tiers adjust rendering settings
  and internal resolution while retaining the same small reusable hero assets.
- Five unfamiliar developers completing the learning evaluation: at least four
  answer at least four of the five questions correctly without the source atlas.

The five questions are: trace a DM, locate authorization, explain a split TCP
read, predict group delivery before/after self-join, and explain a committed
send whose ACK was lost. Record comprehension problems and revise the relevant
scene. Do not mark the cinematic release or any coursework/manual/physical/AI
milestone complete based on automated onboarding tests.
