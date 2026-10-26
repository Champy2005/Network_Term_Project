# Inside Socket Chat assets

The three hardware cutaways are original procedural Three.js geometry created by
`scripts/build-assets.mjs`. They contain a graphite client assembly, a server
compute assembly, and a four-bay storage assembly. Named groups (`shell`, `lid`,
`core`, `port-in`, `port-out`, and `tray-0` through `tray-3`) are stable local
pivots for renderer-controlled exploded states. Fixed `anchor-*` groups provide
camera and focus points. Geometry is batched by material inside each movable
group; the generated models stay at or below 25 material draw calls apiece.

All meshes are procedural and authored in this repository. No downloaded,
scanned, or generated third-party meshes or textures are included. The models,
their authoring source, and the project-authored narration text are dedicated
under CC0-1.0; see `LICENSE.md`. The MP3s are local Microsoft Zira SAPI renders
included as provisional previews; they are not covered by the CC0 dedication.

From `onboarding/`, run `npm run assets:build` to reproduce the models and
`npm run assets:check` to validate GLB headers, hashes, semantic groups, local
pivots, unique names, provenance, per-model byte bounds, and draw-call bounds.
The generator uses the pinned project dependency `three` and GLTFExporter from
that same package. Each model currently ships as one detailed hero variant;
the generator does not yet produce a separate reduced-detail LOD file.

Narration is authored from the public beat text in `src/content.ts` by
`scripts/build-audio.mjs`. It uses installed Windows System.Speech voices and
encodes local WAV intermediates to MP3 with FFmpeg. The resulting audio is a
provisional synthetic read, not a professionally recorded or acoustically
reviewed performance. The audio manifest records the selected voice, measured
clip duration, and whether each cue fits its assigned beat. Confirm the
installed voice's terms before redistributing these rendered MP3s.
