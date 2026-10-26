/** Typed authoring boundary: content describes meaning; the renderer only presents it. */
export type StageKind =
  | 'overview'
  | 'client'
  | 'framing'
  | 'tls'
  | 'session'
  | 'identity'
  | 'routing'
  | 'groups'
  | 'storage'
  | 'heartbeat'
  | 'recovery';
export type ViewKind = 'wide' | 'close' | 'cutaway' | 'detail';
export interface BeatDefinition {
  id: string;
  start: number;
  end: number;
  title: string;
  caption: string;
  narration: string;
  detail: string;
  focus: string;
  view: ViewKind;
  labels: string[];
  /** Semantic phase selects a defined visual state, never a free-running animation. */
  phase: string;
  /** Authoritative scenario membership, independent of transient animation phase. */
  members?: string[];
  sourceIds: string[];
}
export interface ChapterDefinition {
  id: string;
  title: string;
  subtitle: string;
  objective: string;
  duration: number;
  stage: StageKind;
  accent: string;
  beats: BeatDefinition[];
  experiments: string[];
  takeaway: string;
}
export interface ExperimentDefinition {
  id: string;
  title: string;
  question: string;
  chapterId: string;
  steps: { action: string; explanation: string }[];
}
export interface ExperimentResult {
  title: string;
  status: 'waiting' | 'accepted' | 'rejected' | 'closed' | 'recovered';
  explanation: string;
  metrics: { label: string; value: string }[];
  bytes?: number[];
  receivedBytes?: number;
  emitted?: number;
  deliveries?: string[];
  committed?: boolean;
  queue?: number;
  activeBufferAlive?: boolean;
  sequence?: number[];
  members?: string[];
}
export interface SourceEntry {
  id: string;
  path: string;
  symbol: string;
  startLine: number;
  endLine: number;
  hash: string;
  code: string;
}
export interface SourceAtlas {
  version: number;
  revision: string;
  sources: SourceEntry[];
  files: { path: string; role: string; lineCount: number; code: string; hash: string }[];
}
export type PlaybackPhase = 'ready' | 'playing' | 'paused' | 'held' | 'completed' | 'error';
export type ViewingMode = 'guided' | 'inspection';
export type Quality = 'auto' | 'balanced' | 'high';
export interface Preferences {
  quality: Quality;
  reducedMotion: boolean;
  voice: number;
  effects: number;
  ambience: number;
  watch: boolean;
  captions: boolean;
}
export interface PlayerSnapshot {
  version: number;
  chapter: number;
  time: number;
  preferences: Preferences;
}
