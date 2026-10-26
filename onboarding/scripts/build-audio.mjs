/** Build offline provisional narration from every authored chapter beat. */
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { CHAPTERS } from '../src/content.ts';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const assetRoot = path.join(root, 'public/assets');
const audioDir = path.join(assetRoot, 'audio');
const manifestPath = path.join(assetRoot, 'audio-manifest.json');
const voice = process.env.SOCKET_CHAT_VOICE || 'Microsoft Zira Desktop';
const rate = Number(process.env.SOCKET_CHAT_SAPI_RATE ?? 2);

function run(executable, args, { cwd, env, input } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, { cwd, env, stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
    let stdout = '', stderr = '';
    child.stdout.setEncoding('utf8').on('data', (chunk) => { stdout += chunk; });
    child.stderr.setEncoding('utf8').on('data', (chunk) => { stderr += chunk; });
    child.on('error', reject);
    child.on('close', (code) => code === 0 ? resolve({ stdout, stderr }) : reject(new Error(`${executable} exited ${code}: ${stderr || stdout}`)));
    if (input) child.stdin.end(input); else child.stdin.end();
  });
}
function wavDuration(buffer) {
  if (buffer.toString('ascii', 0, 4) !== 'RIFF' || buffer.toString('ascii', 8, 12) !== 'WAVE') throw new Error('SAPI output is not a WAV file');
  let offset = 12, bytesPerSecond = 0, dataSize = 0;
  while (offset + 8 <= buffer.length) {
    const id = buffer.toString('ascii', offset, offset + 4);
    const size = buffer.readUInt32LE(offset + 4);
    if (id === 'fmt ' && size >= 16) bytesPerSecond = buffer.readUInt32LE(offset + 16);
    if (id === 'data') { dataSize = size; break; }
    offset += 8 + size + (size % 2);
  }
  if (!bytesPerSecond || !dataSize) throw new Error('SAPI WAV has no duration-bearing PCM data');
  return dataSize / bytesPerSecond;
}
function cueFilename(chapterId, beatId) {
  const shortBeatId = beatId.startsWith(`${chapterId}-`) ? beatId.slice(chapterId.length + 1) : beatId;
  return `${chapterId}-${shortBeatId}.mp3`;
}

if (process.platform !== 'win32') throw new Error('Offline narration build requires Windows System.Speech');
if (!Number.isInteger(rate) || rate < -10 || rate > 10) throw new Error('SOCKET_CHAT_SAPI_RATE must be an integer from -10 to 10');
const cues = CHAPTERS.flatMap((chapter) => chapter.beats.map((beat) => {
  if (!beat.narration?.trim()) throw new Error(`Missing narration text for ${chapter.id}/${beat.id}`);
  return {
    key: `${chapter.id}/${beat.id}`,
    chapterId: chapter.id,
    beatId: beat.id,
    text: beat.narration.trim(),
    beatDuration: beat.end - beat.start,
    filename: cueFilename(chapter.id, beat.id),
  };
}));
if (!cues.length) throw new Error('No chapter narration cues were authored');

const tempDir = await mkdtemp(path.join(os.tmpdir(), 'inside-socket-audio-'));
process.once('exit', () => rmSync(tempDir, { recursive: true, force: true }));
const requestPath = path.join(tempDir, 'cues.json');
const wavDir = path.join(tempDir, 'wav');
await mkdir(wavDir, { recursive: true });
await mkdir(audioDir, { recursive: true });
await writeFile(requestPath, JSON.stringify(cues.map((cue, index) => ({
  path: path.join(wavDir, `${index}.wav`), text: cue.text,
}))), 'utf8');

const ps = String.raw`
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$cues = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($env:SOCKET_CHAT_REQUEST_PATH)) | ConvertFrom-Json
$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
$speaker.SelectVoice($env:SOCKET_CHAT_VOICE_NAME)
$speaker.Rate = [int]$env:SOCKET_CHAT_RATE
foreach ($cue in $cues) {
  $speaker.SetOutputToWaveFile($cue.path)
  $speaker.Speak($cue.text)
  $speaker.SetOutputToNull()
}
$speaker.Dispose()
Write-Output 'SAPI narration complete'
`;

const psEncoded = Buffer.from(ps, 'utf16le').toString('base64');
const psResult = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', psEncoded], {
  cwd: root,
  env: { ...process.env, SOCKET_CHAT_REQUEST_PATH: requestPath, SOCKET_CHAT_VOICE_NAME: voice, SOCKET_CHAT_RATE: String(rate) },
});
console.log(psResult.stdout.trim());

const manifest = {
  version: 1,
  provenance: 'Locally synthesized from project-authored English narration using Windows System.Speech; provisional synthetic voice; no external audio service.',
  voice,
  rate,
  cues: {},
};
const tooLong = [];
for (let index = 0; index < cues.length; index++) {
  const cue = cues[index];
  const wavPath = path.join(wavDir, `${index}.wav`);
  const wav = await readFile(wavPath);
  const duration = wavDuration(wav);
  const mp3Path = path.join(audioDir, cue.filename);
  await run(process.env.FFMPEG_PATH || 'ffmpeg', [
    '-hide_banner', '-loglevel', 'error', '-y', '-i', wavPath,
    '-codec:a', 'libmp3lame', '-b:a', '64k', '-ar', '22050', '-ac', '1', mp3Path,
  ]);
  const mp3 = await readFile(mp3Path);
  const fitsBeat = duration <= cue.beatDuration;
  const overflowSeconds = Math.max(0, duration - cue.beatDuration);
  manifest.cues[cue.key] = {
    src: `/assets/audio/${cue.filename}`,
    bytes: mp3.length,
    sha256: createHash('sha256').update(mp3).digest('hex'),
    textHash: createHash('sha256').update(cue.text, 'utf8').digest('hex'),
    duration: Number(duration.toFixed(2)),
    beatDuration: cue.beatDuration,
    fitsBeat,
    overflowSeconds: Number(overflowSeconds.toFixed(2)),
    voice,
    provisional: true,
  };
  if (!fitsBeat) tooLong.push(`${cue.key}: ${duration.toFixed(1)}s clip for ${cue.beatDuration}s beat (+${overflowSeconds.toFixed(1)}s)`);
  if ((index + 1) % 10 === 0 || index + 1 === cues.length) console.log(`encoded ${index + 1}/${cues.length} cues`);
}

await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');
await rm(tempDir, { recursive: true, force: true });
const totalBytes = Object.values(manifest.cues).reduce((sum, cue) => sum + cue.bytes, 0);
const totalDuration = Object.values(manifest.cues).reduce((sum, cue) => sum + cue.duration, 0);
console.log(`Generated ${cues.length} provisional cues (${Math.round(totalDuration)}s narration, ${totalBytes.toLocaleString()} bytes MP3).`);
if (tooLong.length) {
  console.log(`Beat overflow: ${tooLong.length} cue(s) exceed their beat at SAPI rate ${rate}; clips were preserved untrimmed.`);
  for (const cue of tooLong) console.log(`  ${cue}`);
} else console.log('All narration cues fit their assigned beat durations.');
