/** Semantic annotations shared by the 3D cutaway and illustrated fallback. */
import type { BeatDefinition, ChapterDefinition, ExperimentResult } from './types';
export interface Annotation {
  title: string;
  value: string;
  at: [number, number, number];
  tone?: 'amber' | 'cyan' | 'muted' | 'red';
}
/** A rejection cannot remove earlier membership; only an explicit state transition can. */
export function membersAt(beat: BeatDefinition, experiment: ExperimentResult | null): string[] {
  return (experiment?.members ?? beat.members ?? []).map((m) => m.toLowerCase());
}
export function annotations(
  chapter: ChapterDefinition,
  beat: BeatDefinition,
  experiment: ExperimentResult | null,
  experimentId: string | null,
): Annotation[] {
  const phase = beat.phase;
  const rejected =
    experiment?.status === 'rejected' || experiment?.status === 'closed' || phase === 'deny';
  const a = (
    title: string,
    value: string,
    at: [number, number, number],
    tone: Annotation['tone'] = 'cyan',
  ): Annotation => ({ title, value, at, tone });
  if (
    chapter.stage === 'framing' ||
    (chapter.stage === 'overview' &&
      beat.focus === 'frame' &&
      ['encode', 'split'].includes(phase)) ||
    ['split-header', 'coalesced', 'oversized'].includes(experimentId ?? '')
  ) {
    const count =
      experiment?.receivedBytes ??
      (phase === 'split'
        ? 2
        : phase === 'waiting'
          ? 12
          : phase === 'complete'
            ? 48
            : phase === 'close'
              ? 24
              : 4);
    const emitted = experiment?.emitted ?? (phase === 'complete' ? 2 : phase === 'close' ? 1 : 0);
    const prefix = experimentId === 'oversized' ? '00 10 00 01' : '00 00 00 14';
    return [
      a('4-BYTE LENGTH', prefix, [-4, 5.7, 1.6], 'amber'),
      a('SERIALIZED PAYLOAD', '20 bytes · length example', [2, 5.7, 1.6]),
      a(
        'FRAME DECODER',
        rejected
          ? 'Rejected before allocation'
          : `${count} bytes in · ${emitted} frame${emitted === 1 ? '' : 's'} out`,
        [0, 3, 5],
        rejected ? 'red' : 'cyan',
      ),
    ];
  }
  if (chapter.stage === 'client' && beat.focus === 'queue')
    return [
      a('MATCHING RESPONSE', 'request_id correlation', [-15, 5.8, 4], 'amber'),
      a('UNSOLICITED EVENTS', 'Retained in bounded buffers', [-9, 5.8, 4]),
      a('NEXT_EVENT', 'Returns preserved plain values', [-12, 3, 8]),
    ];
  if (chapter.stage === 'client')
    return [
      a('PRESENTATION', 'UI + RoomModel', [-15, 5, 4], 'amber'),
      a('OWNING THREAD', 'ChatWorker', [-12, 6, 4]),
      a('NETWORK API', 'ChatClient', [-9, 5, 4]),
    ];
  if (chapter.stage === 'tls')
    return [
      a('CERTIFICATE CHAIN', 'Trusted CA', [-3, 5.8, 1], 'amber'),
      a('SERVER NAME', 'DNS / IP SAN', [0, 6.6, 1]),
      a(
        'APPLICATION I/O',
        rejected
          ? 'Blocked · no downgrade'
          : phase === 'waiting' || phase === 'complete'
            ? 'Handshake verified'
            : 'Wait for verification',
        [3, 5.8, 1],
        rejected ? 'red' : 'cyan',
      ),
    ];
  if (
    chapter.stage === 'session' ||
    ['slow-receiver', 'cancel-write'].includes(experimentId ?? '')
  ) {
    const released = experiment?.activeBufferAlive === false;
    return [
      a(
        'BOUNDED ADMISSION',
        experiment?.queue === 0 ? 'Pending queue cleared' : '256 frames / 2 MiB',
        [-3, 5.8, 1],
        'amber',
      ),
      a('ORDERED WRITE', 'One active operation', [3, 5.8, 1]),
      a(
        'BUFFER OWNER',
        released
          ? 'Completion releases frame'
          : experiment?.activeBufferAlive
            ? 'Handler retains frame'
            : 'Handler + admitted queue',
        [0, 3, 4.5],
        released ? 'muted' : 'cyan',
      ),
    ];
  }
  if (chapter.stage === 'identity' || experimentId === 'reclaim-name')
    return [
      a('DISPLAY NAME', 'Bob · reusable', [-3, 5.8, 1], 'amber'),
      a(
        'DURABLE PRINCIPAL',
        experimentId === 'reclaim-name' && experiment?.title.includes('New')
          ? 'New principal ≠ original'
          : 'Identity ≠ display name',
        [0, 6.8, 1],
      ),
      a(
        'PRIVATE HISTORY',
        rejected ? 'Access denied' : 'Requires original capability',
        [3, 5.8, 1],
        rejected ? 'red' : 'cyan',
      ),
    ];
  if (chapter.stage === 'groups' || experimentId === 'nonmember') {
    const joined = membersAt(beat, experiment).includes('bob');
    return [
      a('ALICE', 'Creator · member', [-1.8, 5.8, 0], 'amber'),
      a('BOB', joined ? 'Self-joined · member' : 'Discovered ≠ joined', [
        joined ? 1.5 : 4.5,
        5.8,
        0,
      ]),
      a(
        'CHARLIE',
        rejected ? 'Send denied · no delivery' : 'Outside membership',
        [6.4, 4, 2],
        rejected ? 'red' : 'muted',
      ),
    ];
  }
  if (chapter.stage === 'storage')
    return [
      a(
        'ONE TRANSACTION',
        phase === 'commit' || phase === 'ack' || phase === 'complete'
          ? 'COMMIT → acceptance'
          : 'Message + sequence + key',
        [-3, 5.2, -12],
        'amber',
      ),
      a('ROOM SEQUENCE', '… 5 → 6 → 7', [2.7, 5.2, -12]),
      a('RETRY KEY', 'Scoped to sender', [0, 3, -8]),
    ];
  if (chapter.stage === 'heartbeat' || experimentId === 'partial-idle')
    return [
      a('HEALTHY WORKER', 'Ping every 10 s', [-3, 5.8, 0], 'amber'),
      a(
        'SERVER IDLE',
        rejected ? '30 s expired · closed' : '30 s after complete request',
        [3, 5.8, 0],
        rejected ? 'red' : 'cyan',
      ),
      a('PARTIAL BYTES / OUTBOUND', 'Do not refresh activity', [0, 3, 4.5], 'muted'),
    ];
  if (chapter.stage === 'recovery' || experimentId === 'lost-ack') {
    const z = beat.focus === 'history' ? -12 : 1;
    return [
      a('RETAINED IN MEMORY', 'Identity + chosen groups + key', [-3.5, 5.8, z], 'amber'),
      a('BOUNDED REPLAY', 'Fixed upper watermark', [3.5, 5.8, z]),
      a(
        'SAVED-KEY RETRY',
        experiment?.status === 'recovered' || phase === 'ack'
          ? 'Original ACK · no new fanout'
          : 'Acceptance may be uncertain',
        [0, 3, z + 4],
      ),
    ];
  }
  if (chapter.stage === 'routing' || experimentId === 'forged-sender') {
    if (experimentId === 'forged-sender')
      return [
        a('REGISTERED CONNECTION', 'Alice', [-3, 5, 1], 'amber'),
        a('CLIENT SENDER FIELD', 'Untrusted · must be empty', [3, 5, 1]),
        a(
          'SERVER RESULT',
          rejected ? 'INVALID_REQUEST · no delivery' : 'Awaiting request',
          [0, 3, 5],
          rejected ? 'red' : 'cyan',
        ),
      ];
    if (phase === 'encode')
      return [
        a('RECIPIENT', 'Bob', [-3, 5.8, 1], 'amber'),
        a('MESSAGE TEXT', 'Meet at 5', [0, 6.6, 1]),
        a('SAVED RETRY KEY', 'Retain before sending', [3, 5.8, 1]),
      ];
    if (phase === 'verify')
      return [
        a('SENDER PRINCIPAL', '1 · Alice', [-3, 5.8, 1], 'amber'),
        a('RECIPIENT PRINCIPAL', '2 · Bob', [3, 5.8, 1]),
        a('DURABLE PAIR ROOM', 'Principals 1 ↔ 2', [0, 3, 5]),
      ];
    if (phase === 'authorize')
      return [
        a('REQUEST', 'Recipient: Bob · text · key', [-3, 5.8, 1], 'amber'),
        a('CONNECTION REGISTRY', 'Sender: Alice · principal 1', [3, 5.8, 1]),
        a('SERVER OWNED', 'Identity + pair-room selection', [0, 3, 5]),
      ];
    if (phase === 'commit')
      return [
        a('PERSISTENT ROOM', 'Principals 1 ↔ 2', [-3, 5.5, -12], 'amber'),
        a('COMMIT FIRST', 'Message + sequence + key', [3, 5.5, -12]),
        a('ACK ≠ RECEIPT', 'Acceptance does not prove display', [0, 3, -8]),
      ];
    if (phase === 'deliver' || phase === 'complete' || phase === 'deny' || phase === 'ack')
      return [
        a('SENDER ECHO', 'Alice receives the message', [-12, 3.2, 4], 'amber'),
        a('RECIPIENT DELIVERY', 'Bob receives the message', [12, 3.2, -4]),
        a('OUTSIDE THIS ROOM', 'Charlie: no private payload', [13, 3.2, 10], 'muted'),
      ];
  }
  return [];
}
