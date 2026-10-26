import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const outDir = path.join(root, 'onboarding/public/content');
const atlasPath = path.join(outDir, 'source-atlas.json');
const prefix = 'window.SOCKET_CHAT_DATA = ';

const specs = [
  ['wire-envelope', 'proto/chat.proto', 'message Envelope {', 'Envelope & Oneof Payload Dispatch'],
  ['frame-encode', 'src/protocol/framing.cpp', 'encode_frame(const std::span', 'encode_frame() — Big-Endian Header Serialization'],
  ['frame-decoder', 'src/protocol/framing.cpp', 'FrameDecoder::feed(', 'FrameDecoder::feed() — Incremental Stream State Machine'],
  ['frame-finish', 'src/protocol/framing.cpp', 'FrameDecoder::finish() const', 'FrameDecoder::finish() — Clean vs Truncated EOF Check'],
  ['tls-verify', 'src/transport/tls.cpp', 'void verify_tls_peer(', 'client_tls_context(); verify_tls_peer()'],
  ['session-ownership', 'src/server/client_session.cpp', 'bool ClientSession::send(', 'send(), stop() & stop_on_executor()'],
  ['session-idle', 'src/server/client_session.cpp', 'void ClientSession::wait_for_idle(', 'wait_for_idle() — Timestamp-Based Lazy Idle Timer'],
  ['session-read', 'src/server/client_session.cpp', 'void ClientSession::read(', 'read() — Incremental Frame Decoding'],
  ['hub-login', 'src/server/chat_hub.cpp', 'void ChatHub::login(', 'login() — Unique Username Reservation'],
  ['hub-direct', 'src/server/chat_hub.cpp', 'void ChatHub::direct(', 'direct() — Pair-Scoped Direct Rooms'],
  ['hub-groups', 'src/server/chat_hub.cpp', 'void ChatHub::group_message(', 'create_group(), join_group() & group_message()'],
  ['storage-transaction', 'src/server/history_store.cpp', 'explicit Transaction(sqlite3* db)', 'RAII Statement, Transaction (`BEGIN IMMEDIATE`)'],
  ['storage-identity', 'src/server/history_store.cpp', 'StoredIdentity HistoryStore::identity(', 'identity(), groups(), create_group()'],
  ['storage-history', 'src/server/history_store.cpp', 'protocol::HistoryResponse HistoryStore::history(', 'direct_participant(), history(), sync()'],
  ['client-api', 'include/chat/client/ping_client.hpp', 'class PingClient', 'PingClient (`ChatClient`) Blocking Operations API'],
  ['client-buffering', 'src/client/ping_client.cpp', 'protocol::Envelope exchange(', 'event(), receive() & exchange() — Two-Phase Event Loop'],
  ['worker-recovery', 'src/client/chat_worker.cpp', 'auto replay =', 'run() Lambdas: `publish`, `track`, `replay`'],
  ['worker-heartbeat', 'src/client/chat_worker.cpp', 'auto next_heartbeat =', 'Initial Connect/Login, Recovery Loop'],
  ['room-model', 'src/client/room_model.cpp', 'void RoomModel::apply(', 'apply(const ClientEvent&) — Snapshot & Delivery Router'],
  ['cli-input', 'apps/cli/chat_console.cpp', 'Command parse_command(', 'parse_command() — `/help`, `/users`'],
  ['build-targets', 'CMakeLists.txt', 'add_library(chat_proto STATIC', 'Dependencies, protoc Custom Command'],
];

const sha = (text) => createHash('sha256').update(text).digest('hex');
const normalize = (value) => value.replaceAll('\\', '/');

function legacyData(source) {
  const start = source.indexOf(prefix);
  if (start < 0) throw new Error('Legacy source catalogue assignment was not found.');
  const jsonStart = start + prefix.length;
  const open = source.indexOf('{', jsonStart);
  let depth = 0;
  let inString = false;
  let escaped = false;
  for (let i = open; i < source.length; i += 1) {
    const char = source[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (char === '\\') escaped = true;
      else if (char === '"') inString = false;
      continue;
    }
    if (char === '"') inString = true;
    else if (char === '{') depth += 1;
    else if (char === '}' && --depth === 0) return JSON.parse(source.slice(open, i + 1));
  }
  throw new Error('Legacy source catalogue JSON is incomplete.');
}

async function build() {
  const legacyText = await readFile(path.join(root, 'onboarding/public/legacy/codebase_data.js'), 'utf8');
  const legacy = legacyData(legacyText);
  const byPath = new Map(legacy.files.map((file) => [normalize(file.path), file]));
  const entries = [];
  for (const [id, relative, needle, blockName] of specs) {
    const absolute = path.join(root, relative);
    const code = await readFile(absolute, 'utf8');
    const offset = code.indexOf(needle);
    if (offset < 0) throw new Error(`${id}: missing source anchor ${JSON.stringify(needle)} in ${relative}`);
    const lines = code.split(/\r?\n/);
    const line = code.slice(0, offset).split(/\r?\n/).length;
    const startLine = Math.max(1, line - 4);
    const endLine = Math.min(lines.length, line + 9);
    const snippet = lines.slice(startLine - 1, endLine).join('\n');
    const legacyFile = byPath.get(relative);
    const block = legacyFile?.blocks.find((candidate) => candidate.name === blockName)
      ?? legacyFile?.blocks.find((candidate) => candidate.name.includes(blockName));
    entries.push({
      id, path: relative, symbol: block?.name ?? needle, startLine, endLine,
      hash: sha(snippet), code: snippet,
      summary: block?.summary ?? '', explanation: block?.details ?? '', risk: block?.risk ?? '',
    });
  }

  const sourcePaths = legacy.files.map((file) => normalize(file.path));
  const files = [];
  const fullFiles = [];
  for (const relative of sourcePaths) {
    const absolute = path.join(root, relative);
    let code;
    try { code = await readFile(absolute, 'utf8'); }
    catch { throw new Error(`Legacy catalogue source file is missing: ${relative}`); }
    files.push({ path: relative, role: byPath.get(relative)?.role ?? '', lineCount: code.split(/\r?\n/).length, hash: sha(code), code: '' });
    fullFiles.push({ relative, code });
  }
  const revision = execFileSync('git', ['rev-parse', '--short', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim();
  return { atlas: { version: 1, revision, sources: entries, files }, fullFiles };
}

const { atlas: expected, fullFiles } = await build();
const lazyDir = path.join(outDir, 'source-files');
if (process.argv.includes('--update')) {
  await mkdir(outDir, { recursive: true });
  await mkdir(lazyDir, { recursive: true });
  await writeFile(atlasPath, `${JSON.stringify(expected, null, 2)}\n`);
  for (const file of fullFiles) {
    const filename = `${file.relative.replaceAll('/', '__').replaceAll('\\', '__')}.txt`;
    await writeFile(path.join(lazyDir, filename), file.code);
  }
  console.log(`Updated ${path.relative(root, atlasPath)}: ${expected.sources.length} curated symbols, ${expected.files.length} lazy source files.`);
} else {
  let current;
  try { current = JSON.parse(await readFile(atlasPath, 'utf8')); }
  catch { throw new Error(`Missing or invalid ${path.relative(root, atlasPath)}; run content: update explicitly.`); }
  const fingerprint = (atlas) => JSON.stringify({
    sources: atlas.sources?.map(({ id, path: file, symbol, startLine, endLine, hash, code }) => ({ id, path: file, symbol, startLine, endLine, hash, code })),
    files: atlas.files?.map(({ path: file, lineCount, hash }) => ({ path: file, lineCount, hash })),
  });
  if (fingerprint(current) !== fingerprint(expected)) {
    throw new Error('Source atlas drift detected (missing anchor, changed snippet hash, line range, or full-file inventory). Run content: update explicitly after reviewing the source changes.');
  }
  for (const file of fullFiles) {
    const filename = `${file.relative.replaceAll('/', '__').replaceAll('\\', '__')}.txt`;
    const artifact = path.join(lazyDir, filename);
    let saved;
    try { saved = await readFile(artifact, 'utf8'); }
    catch { throw new Error(`Missing lazy full-file atlas entry for ${file.relative}; run content: update explicitly.`); }
    if (saved !== file.code) throw new Error(`Lazy full-file atlas drift detected for ${file.relative}; run content: update explicitly.`);
  }
  console.log(`Source atlas current: ${expected.sources.length} curated symbol hashes and ${expected.files.length} full-file entries checked.`);
}
