import { CHAPTERS } from './content';
import type { ExperimentDefinition, ExperimentResult } from './types';

export { CHAPTERS };

export const EXPERIMENTS: ExperimentDefinition[] = [
  { id: 'split-header', title: 'Split the frame header', question: 'What can the decoder emit after only two header bytes?', chapterId: 'framing', steps: [
    { action: 'Send bytes 00 00.', explanation: 'A four-byte length is incomplete; the decoder retains two header bytes and emits no payload.' },
    { action: 'Send 00 14 and the twenty payload bytes.', explanation: 'The decoder completes the big-endian length, accumulates the full payload, and emits exactly one frame.' },
  ] },
  { id: 'coalesced', title: 'Coalesce two frames', question: 'Can one read contain more than one application message?', chapterId: 'framing', steps: [
    { action: 'Feed two complete frames together.', explanation: 'TCP read boundaries are unrelated to frame boundaries; the decoder consumes and emits both in order.' },
    { action: 'Inspect the trailing decoder state.', explanation: 'With exactly two complete frames, there are no buffered bytes and no third frame.' },
  ] },
  { id: 'oversized', title: 'Declare an oversized frame', question: 'Does a declared payload larger than one mebibyte get allocated?', chapterId: 'framing', steps: [
    { action: 'Feed a four-byte header declaring 1,048,577 bytes.', explanation: 'The codec rejects the declared length immediately, before buffering or allocating that payload.' },
  ] },
  { id: 'forged-sender', title: 'Forge a direct sender', question: 'Can a client choose the sender identity in a direct message?', chapterId: 'private-rooms', steps: [
    { action: 'Alice logs in and sends a direct request to Bob.', explanation: 'The server binds the connection to Alice during login.' },
    { action: 'Add a sender field naming Alice.', explanation: 'The server rejects a nonempty client-supplied sender field even when its value happens to match Alice.' },
    { action: 'Observe the hub outcome.', explanation: 'The request is rejected before commit and fanout; Bob and Charlie receive no forged delivery.' },
  ] },
  { id: 'nonmember', title: 'Send from outside a group', question: 'Can Charlie publish to a group he has not joined?', chapterId: 'groups', steps: [
    { action: 'Alice creates a group; Bob joins through his own connection.', explanation: 'Active membership contains Alice and Bob only; group creation does not automatically add others.' },
    { action: 'Charlie submits a group message.', explanation: 'The server checks Charlie’s current membership before routing group text.' },
    { action: 'Observe the rejection.', explanation: 'The request is rejected before storage or fanout, so no member receives a message from Charlie.' },
  ] },
  { id: 'slow-receiver', title: 'Stop draining a receiver', question: 'What happens when a peer cannot keep up with its outbound queue?', chapterId: 'session-lifecycle', steps: [
    { action: 'Queue frames while the peer stops reading.', explanation: 'The connection keeps one ordered writer and accounts queued frames and bytes, including its active write.' },
    { action: 'Reach the configured queue budget.', explanation: 'Admission cannot exceed the session limits; overflow closes the session and discards pending frames.' },
  ] },
  { id: 'cancel-write', title: 'Cancel an active write', question: 'Does closing the socket invalidate a buffer still used by a handler?', chapterId: 'session-lifecycle', steps: [
    { action: 'Begin one composed write with its owned frame buffer.', explanation: 'The active operation holds the frame memory it references.' },
    { action: 'Stop the session and cancel the socket.', explanation: 'Cancellation begins, but the active handler retains its buffer until completion.' },
    { action: 'Run the cancellation completion.', explanation: 'The operation releases its buffer only after the handler finishes; queued frames are cleared during close.' },
  ] },
  { id: 'lost-ack', title: 'Lose an ACK after commit', question: 'How does a retry avoid a second accepted message after an uncertain send?', chapterId: 'storage', steps: [
    { action: 'Alice sends a keyed direct message.', explanation: 'The storage transaction commits its message and room sequence before the hub queues an ACK or delivery.' },
    { action: 'Drop the ACK after the commit.', explanation: 'The server has accepted sequence seven even though Alice did not observe the receipt.' },
    { action: 'Resume and replay the room.', explanation: 'The worker applies contiguous missed room sequences, including seven, through its captured watermark.' },
    { action: 'Retry the same saved sender-scoped key.', explanation: 'The server returns the original receipt without another fanout; this is scoped deduplication, not exactly-once delivery.' },
  ] },
  { id: 'reclaim-name', title: 'Reclaim a display name', question: 'Does a new person using an old name inherit its durable history?', chapterId: 'identity', steps: [
    { action: 'The original Bob logs in and receives a resume capability.', explanation: 'Storage binds the capability hash to Bob’s durable principal.' },
    { action: 'Bob disconnects; a fresh user claims the same display name.', explanation: 'A fresh login creates a different principal; active-name availability does not establish prior identity.' },
    { action: 'Request the original direct history without its capability.', explanation: 'The new principal is not a participant in the old durable pair room, so the server denies access.' },
  ] },
  { id: 'partial-idle', title: 'Drip a partial frame', question: 'Do incomplete bytes or server writes reset the idle timer?', chapterId: 'heartbeat', steps: [
    { action: 'Send two bytes of a four-byte header.', explanation: 'The decoder buffers an incomplete header and emits no request.' },
    { action: 'Wait past the configured idle deadline.', explanation: 'Only complete valid inbound requests refresh activity; partial bytes and outbound traffic do not.' },
    { action: 'Observe session cleanup.', explanation: 'The idle timer closes the session, releasing its name, memberships, socket, and connection slot.' },
  ] },
];

/** Incremental educational mirror of the C++ bounded four-byte frame decoder. */
export class FrameDecoderModel {
  private header: number[] = [];
  private declaredLength = 0;
  private payload: number[] = [];
  private failed = false;

  constructor(readonly maximumPayload = 1_048_576) {
    if (!Number.isInteger(maximumPayload) || maximumPayload < 1 || maximumPayload > 1_048_576) {
      throw new RangeError('maximum frame payload must be in 1..1,048,576 bytes');
    }
  }

  get pendingHeaderBytes(): number { return this.header.length; }
  get pendingPayloadBytes(): number { return this.payload.length; }
  get expectedPayloadBytes(): number { return this.declaredLength; }

  feed(input: ArrayLike<number>): Uint8Array[] {
    if (this.failed) throw new Error('frame decoder is failed after invalid input');
    const emitted: Uint8Array[] = [];
    let offset = 0;
    const appendByte = (target: number[], value: number): void => {
      if (!Number.isInteger(value) || value < 0 || value > 255) {
        this.failed = true;
        throw new RangeError('frame input must contain byte values from 0 through 255');
      }
      target.push(value);
    };
    while (offset < input.length) {
      if (this.declaredLength === 0) {
        const count = Math.min(4 - this.header.length, input.length - offset);
        for (let index = 0; index < count; index += 1) appendByte(this.header, input[offset + index]);
        offset += count;
        if (this.header.length < 4) break;
        const length = this.header[0] * 0x1000000 + this.header[1] * 0x10000 +
          this.header[2] * 0x100 + this.header[3];
        if (length < 1 || length > this.maximumPayload) {
          this.failed = true;
          throw new RangeError(`frame payload length ${length} is outside 1..${this.maximumPayload}`);
        }
        this.header = [];
        this.declaredLength = length;
        this.payload = [];
      }
      const count = Math.min(this.declaredLength - this.payload.length, input.length - offset);
      for (let index = 0; index < count; index += 1) appendByte(this.payload, input[offset + index]);
      offset += count;
      if (this.payload.length === this.declaredLength) {
        emitted.push(Uint8Array.from(this.payload));
        this.declaredLength = 0;
        this.payload = [];
      }
    }
    return emitted;
  }

  finish(): void {
    if (this.failed || this.header.length !== 0 || this.declaredLength !== 0) {
      throw new Error('truncated frame at end of stream');
    }
  }
}

export type EducationalSendResult = {
  status: 'accepted' | 'denied' | 'key-conflict' | 'storage-failed' | 'not-member';
  sequence?: number;
  duplicate?: boolean;
  fanout: number;
};

type StoredAttempt = { room: string; target: number; text: string; sequence: number };

/** Deterministic server-authority and transaction model used by the explainer experiments. */
export class ScenarioAuthorityModel {
  private nextPrincipal = 1;
  private readonly names = new Map<number, string>();
  private readonly activeNames = new Map<string, number>();
  private readonly activePrincipals = new Set<number>();
  private readonly groups = new Map<string, Set<number>>();
  private readonly directRooms = new Map<string, { members: Set<number>; sequence: number }>();
  private readonly retryKeys = new Map<string, StoredAttempt>();

  freshLogin(name: string): number {
    if (!name || this.activeNames.has(name)) throw new Error(`display name ${name} is already active`);
    const principal = this.nextPrincipal++;
    this.names.set(principal, name);
    this.activeNames.set(name, principal);
    this.activePrincipals.add(principal);
    return principal;
  }

  disconnect(principal: number): void {
    const name = this.names.get(principal);
    if (name && this.activeNames.get(name) === principal) this.activeNames.delete(name);
    this.activePrincipals.delete(principal);
    for (const members of this.groups.values()) members.delete(principal);
  }

  openDirect(left: number, right: number): string {
    const pair = [left, right].sort((a, b) => a - b);
    const id = this.directRoomId(left, right);
    if (!this.directRooms.has(id)) this.directRooms.set(id, { members: new Set(pair), sequence: 0 });
    return id;
  }

  private directRoomId(left: number, right: number): string {
    const pair = [left, right].sort((a, b) => a - b);
    return `direct:${pair[0]}:${pair[1]}`;
  }

  canReadDirect(room: string, principal: number): boolean {
    return this.directRooms.get(room)?.members.has(principal) ?? false;
  }

  createGroup(id: string, creator: number): void { this.groups.set(id, new Set([creator])); }

  joinGroup(id: string, principal: number): boolean {
    const members = this.groups.get(id);
    if (!members) return false;
    members.add(principal);
    return true;
  }

  isGroupMember(id: string, principal: number): boolean {
    return this.groups.get(id)?.has(principal) ?? false;
  }

  discoverGroups(): { id: string; members: string[] }[] {
    return [...this.groups].map(([id, members]) => ({
      id,
      members: [...members].map((principal) => this.names.get(principal) ?? 'unknown'),
    }));
  }

  seedDirectSequence(left: number, right: number, sequence: number): string {
    const room = this.openDirect(left, right);
    const state = this.directRooms.get(room)!;
    state.sequence = sequence;
    return room;
  }

  roomSequence(left: number, right: number): number {
    return this.directRooms.get(this.openDirect(left, right))?.sequence ?? 0;
  }

  sendDirect(sender: number, recipient: number, key: string, text: string,
             options: { failCommit?: boolean; claimedSender?: string } = {}): EducationalSendResult {
    const actualName = this.names.get(sender);
    if (!actualName || !this.activePrincipals.has(sender) ||
        (options.claimedSender !== undefined && options.claimedSender.length > 0)) {
      return { status: 'denied', fanout: 0 };
    }
    const room = this.directRoomId(sender, recipient);
    const fanout = sender === recipient ? 1 : 2;
    if (!this.names.has(recipient) || !this.activePrincipals.has(recipient)) {
      // A committed saved key can still resolve after the peer disconnects.
      // New work to an offline or unknown principal is rejected.
      if (!key || !this.retryKeys.has(`${sender}:${key}`)) return { status: 'denied', fanout: 0 };
      return this.commit(room, sender, recipient, key, text, options.failCommit ?? false, fanout);
    }
    this.openDirect(sender, recipient);
    return this.commit(room, sender, recipient, key, text, options.failCommit ?? false, fanout);
  }

  sendGroup(group: string, sender: number, key: string, text: string,
            options: { failCommit?: boolean } = {}): EducationalSendResult {
    const members = this.groups.get(group);
    if (!this.activePrincipals.has(sender) || !members?.has(sender)) return { status: 'not-member', fanout: 0 };
    return this.commit(group, sender, sender, key, text, options.failCommit ?? false, members.size);
  }

  private commit(room: string, sender: number, target: number, key: string, text: string,
                 failCommit: boolean, fanout: number): EducationalSendResult {
    const scopedKey = key ? `${sender}:${key}` : undefined;
    const previous = scopedKey ? this.retryKeys.get(scopedKey) : undefined;
    if (previous) {
      if (previous.room !== room || previous.target !== target || previous.text !== text) {
        return { status: 'key-conflict', sequence: previous.sequence, duplicate: false, fanout: 0 };
      }
      return { status: 'accepted', sequence: previous.sequence, duplicate: true, fanout: 0 };
    }
    if (failCommit) return { status: 'storage-failed', duplicate: false, fanout: 0 };
    const state = room.startsWith('direct:') ? this.directRooms.get(room)! : undefined;
    const sequence = state ? ++state.sequence : (this.groupSequence(room) + 1);
    if (!state) this.setGroupSequence(room, sequence);
    if (scopedKey) this.retryKeys.set(scopedKey, { room, target, text, sequence });
    return { status: 'accepted', sequence, duplicate: false, fanout };
  }

  private readonly groupSequences = new Map<string, number>();
  private groupSequence(room: string): number { return this.groupSequences.get(room) ?? 0; }
  private setGroupSequence(room: string, value: number): void { this.groupSequences.set(room, value); }
}

/** Session queue, write-buffer ownership, and complete-request idle clock model. */
export class SessionLifecycleModel {
  private now = 0;
  private activity = 0;
  private readonly queuedBytes: number[] = [];
  private writing = false;
  private open = true;

  constructor(readonly limits: { idleTimeout: number; maxFrames: number; maxBytes: number }) {
    if (!Number.isInteger(limits.idleTimeout) || limits.idleTimeout < 0 ||
        !Number.isInteger(limits.maxFrames) || limits.maxFrames < 1 ||
        !Number.isInteger(limits.maxBytes) || limits.maxBytes < 1) {
      throw new RangeError('session limits must be positive and idle timeout nonnegative');
    }
  }

  get lastActivity(): number { return this.activity; }
  get activeBufferAlive(): boolean { return this.writing; }
  get pendingFrames(): number { return this.queuedBytes.length; }
  get isOpen(): boolean { return this.open; }

  receivePartialBytes(_count: number): void { /* incomplete input is not a request */ }
  sendOutbound(_count: number): void { /* outbound traffic is not peer activity */ }
  completeRequest(valid = true): void { if (this.open && valid) this.activity = this.now; }

  advanceTime(milliseconds: number): 'open' | 'closed' {
    if (!Number.isInteger(milliseconds) || milliseconds < 0) throw new RangeError('time must be a nonnegative integer');
    this.now += milliseconds;
    if (this.open && this.limits.idleTimeout > 0 && this.now - this.activity >= this.limits.idleTimeout) this.close();
    return this.open ? 'open' : 'closed';
  }

  enqueueFrame(bytes: number): boolean {
    if (!Number.isInteger(bytes) || bytes < 1) throw new RangeError('frame size must be a positive integer');
    if (!this.open) return false;
    const total = this.queuedBytes.reduce((sum, size) => sum + size, 0);
    if (this.queuedBytes.length >= this.limits.maxFrames || bytes > this.limits.maxBytes - total) {
      this.close();
      return false;
    }
    this.queuedBytes.push(bytes);
    return true;
  }

  beginWrite(): boolean {
    if (!this.open || this.writing || this.queuedBytes.length === 0) return false;
    this.writing = true;
    return true;
  }

  close(): void {
    if (!this.open) return;
    this.open = false;
    this.queuedBytes.length = 0;
  }

  completeWrite(): void {
    if (!this.writing) return;
    this.writing = false;
    if (this.open) this.queuedBytes.shift();
  }
}

function framed(payload: number[]): number[] {
  return [payload.length >>> 24, payload.length >>> 16 & 255, payload.length >>> 8 & 255,
    payload.length & 255, ...payload];
}

function buildExperimentStates(id: string): ExperimentResult[] {
  switch (id) {
    case 'split-header': {
      const decoder = new FrameDecoderModel();
      decoder.feed([0, 0]);
      const waiting: ExperimentResult = { title: 'Header retained', status: 'waiting',
        explanation: `${decoder.pendingHeaderBytes} of 4 header bytes are buffered; no payload is complete.`,
        bytes: [0, 0], receivedBytes: 2, emitted: 0,
        metrics: [{ label: 'Header bytes', value: `${decoder.pendingHeaderBytes} / 4` }, { label: 'Frames emitted', value: '0' }] };
      const emitted = decoder.feed([0, 20, ...Array(20).fill(65)]);
      return [waiting, { title: 'One frame emitted', status: 'accepted',
        explanation: `The decoder emitted ${emitted.length} payload of ${emitted[0]?.length ?? 0} bytes after all fragments arrived.`,
        bytes: [0, 0, 0, 20], receivedBytes: 24, emitted: emitted.length,
        metrics: [{ label: 'Payload length', value: `${emitted[0]?.length ?? 0} bytes` }, { label: 'Frames emitted', value: String(emitted.length) }] }];
    }
    case 'coalesced': {
      const decoder = new FrameDecoderModel();
      const emitted = decoder.feed([...framed(Array(20).fill(65)), ...framed(Array(20).fill(66))]);
      decoder.finish();
      return [
        { title: 'Two frames emitted in order', status: 'accepted',
          explanation: `One read produced ${emitted.length} frames: ${emitted.map((frame) => [...frame].join(',')).join(' then ')}.`,
          receivedBytes: 48, emitted: emitted.length,
          metrics: [{ label: 'Frames in read', value: String(emitted.length) }, { label: 'Order', value: 'preserved' }] },
        { title: 'Clean decoder boundary', status: 'accepted', explanation: 'finish() accepted because the coalesced read left no partial header or payload.',
          receivedBytes: 48, emitted: emitted.length, metrics: [{ label: 'Trailing bytes', value: '0' }] },
      ];
    }
    case 'oversized': {
      const decoder = new FrameDecoderModel();
      let message = '';
      try { decoder.feed([0, 16, 0, 1]); } catch (error) { message = (error as Error).message; }
      return [{ title: 'Length rejected', status: message.includes('1048577') ? 'rejected' : 'waiting',
        explanation: message || 'The model did not reject the declared payload.', bytes: [0, 16, 0, 1],
        receivedBytes: 4, emitted: 0,
        metrics: [{ label: 'Declared', value: '1,048,577 bytes' }, { label: 'Maximum', value: '1,048,576 bytes' }] }];
    }
    case 'forged-sender': {
      const authority = new ScenarioAuthorityModel();
      const alice = authority.freshLogin('alice');
      const bob = authority.freshLogin('bob');
      const base = { title: 'Alice is connection identity', status: 'waiting' as const,
        explanation: 'The server-side model binds Alice’s name to her principal.', deliveries: [] as string[], committed: false,
        metrics: [{ label: 'Registered sender', value: 'alice' }] };
      const forged = authority.sendDirect(alice, bob, 'key-1', 'hello', { claimedSender: 'alice' });
      return [base, { title: 'Metadata is untrusted', status: forged.status === 'denied' ? 'rejected' : 'accepted',
        explanation: 'The request carries a nonempty server-authored sender field, so validation rejects it even if it names Alice.',
        deliveries: [], committed: false, metrics: [{ label: 'Forged sender', value: 'alice' }] },
      { title: 'Rejected before fanout', status: forged.status === 'denied' && forged.fanout === 0 ? 'rejected' : 'accepted',
        explanation: `The request result is ${forged.status}; fanout count is ${forged.fanout}.`, deliveries: [], committed: false,
        metrics: [{ label: 'Bob delivery', value: 'none' }, { label: 'Charlie delivery', value: 'none' }] }];
    }
    case 'nonmember': {
      const authority = new ScenarioAuthorityModel();
      const alice = authority.freshLogin('alice');
      const bob = authority.freshLogin('bob');
      const charlie = authority.freshLogin('charlie');
      authority.createGroup('group-1', alice);
      const members = authority.discoverGroups()[0]?.members ?? [];
      const first: ExperimentResult = { title: 'Only explicit members', status: 'waiting',
        explanation: `Discovery reports current members ${members.join(', ')} and does not add anyone.`, deliveries: [], committed: false,
        members: [...members], metrics: [{ label: 'Members', value: members.join(', ') }] };
      authority.joinGroup('group-1', bob);
      const joinedMembers = authority.discoverGroups()[0]?.members ?? [];
      const send = authority.sendGroup('group-1', charlie, 'key-1', 'outside');
      const denied = { title: 'Membership check fails', status: send.status === 'not-member' ? 'rejected' as const : 'accepted' as const,
        explanation: 'Charlie has not self-joined, so the authoritative group check denies the send.', deliveries: [], committed: false,
        members: [...joinedMembers], metrics: [{ label: 'Sender membership', value: String(authority.isGroupMember('group-1', charlie)) }] };
      return [first, denied, { title: 'No unauthorized delivery', status: send.status === 'not-member' && send.fanout === 0 ? 'rejected' : 'accepted',
        explanation: `The modeled send returned ${send.status} with ${send.fanout} deliveries.`, deliveries: [], committed: false,
        members: [...joinedMembers], metrics: [{ label: 'Member deliveries', value: String(send.fanout) }, { label: 'Committed', value: 'no' }] }];
    }
    case 'slow-receiver': {
      const session = new SessionLifecycleModel({ idleTimeout: 0, maxFrames: 256, maxBytes: 2 * 1024 * 1024 });
      for (let i = 0; i < 256; i += 1) session.enqueueFrame(8);
      const full: ExperimentResult = { title: 'Queue remains bounded', status: 'waiting',
        explanation: `The session model admitted ${session.pendingFrames} frames within its configured frame bound.`,
        queue: session.pendingFrames, metrics: [{ label: 'Queued frames', value: String(session.pendingFrames) }, { label: 'Writer count', value: 'one per socket' }] };
      const admitted = session.enqueueFrame(8);
      return [full, { title: 'Overflow closes session', status: !admitted && !session.isOpen ? 'closed' : 'waiting',
        explanation: `The next admission returned ${admitted}; queue count after cleanup is ${session.pendingFrames}.`,
        queue: session.pendingFrames, deliveries: [], metrics: [{ label: 'Pending queue', value: 'discarded' }, { label: 'Session', value: session.isOpen ? 'open' : 'closed' }] }];
    }
    case 'cancel-write': {
      const session = new SessionLifecycleModel({ idleTimeout: 0, maxFrames: 2, maxBytes: 100 });
      session.enqueueFrame(10);
      session.beginWrite();
      const active: ExperimentResult = { title: 'Write owns its frame', status: 'waiting',
        explanation: `beginWrite() returned with activeBufferAlive=${session.activeBufferAlive}.`, activeBufferAlive: session.activeBufferAlive,
        metrics: [{ label: 'Active write', value: session.activeBufferAlive ? '1' : '0' }] };
      session.close();
      const cancelling: ExperimentResult = { title: 'Cancellation requested', status: 'waiting',
        explanation: `close() cleared queued work while the active buffer remains ${session.activeBufferAlive ? 'alive' : 'released'}.`,
        activeBufferAlive: session.activeBufferAlive, metrics: [{ label: 'Socket', value: session.isOpen ? 'open' : 'closing' }] };
      session.completeWrite();
      return [active, cancelling, { title: 'Handler completes', status: 'closed',
        explanation: `After completion, activeBufferAlive=${session.activeBufferAlive}; queued frames=${session.pendingFrames}.`,
        activeBufferAlive: session.activeBufferAlive, queue: session.pendingFrames,
        metrics: [{ label: 'Active buffer', value: session.activeBufferAlive ? 'retained' : 'released after completion' }] }];
    }
    case 'lost-ack': {
      const authority = new ScenarioAuthorityModel();
      const alice = authority.freshLogin('alice');
      const bob = authority.freshLogin('bob');
      authority.seedDirectSequence(alice, bob, 6);
      const accepted = authority.sendDirect(alice, bob, 'saved-key', 'meet at five');
      const initial: ExperimentResult = { title: 'Message committed', status: accepted.status === 'accepted' ? 'accepted' : 'rejected',
        explanation: `Transaction result ${accepted.status} assigned room sequence ${accepted.sequence}.`,
        committed: accepted.status === 'accepted', sequence: [accepted.sequence ?? 0], deliveries: ['bob'],
        metrics: [{ label: 'Sequence', value: String(accepted.sequence ?? 0) }, { label: 'ACK observed by Alice', value: 'no' }] };
      const lost: ExperimentResult = { title: 'ACK lost in transit', status: 'waiting',
        explanation: `The server retains committed sequence ${accepted.sequence}; Alice did not observe its ACK.`,
        committed: accepted.status === 'accepted', sequence: [accepted.sequence ?? 0], deliveries: ['bob'],
        metrics: [{ label: 'Server commit', value: accepted.status === 'accepted' ? 'yes' : 'no' }] };
      const replay: ExperimentResult = { title: 'Catch-up applies sequence', status: 'recovered',
        explanation: `The worker can apply contiguous sequence ${accepted.sequence} through its captured watermark.`,
        committed: accepted.status === 'accepted', sequence: [accepted.sequence ?? 0], deliveries: ['bob'],
        metrics: [{ label: 'Applied sequence', value: String(accepted.sequence ?? 0) }] };
      const retry = authority.sendDirect(alice, bob, 'saved-key', 'meet at five');
      return [initial, lost, replay, { title: 'Saved key returns receipt', status: retry.status === 'accepted' && retry.duplicate ? 'recovered' : 'rejected',
        explanation: `Retry returned ${retry.status}, duplicate=${retry.duplicate}, fanout=${retry.fanout}.`,
        committed: true, sequence: [retry.sequence ?? 0], deliveries: ['bob'],
        metrics: [{ label: 'Original sequence', value: String(retry.sequence ?? 0) }, { label: 'Additional fanout', value: String(retry.fanout) }] }];
    }
    case 'reclaim-name': {
      const authority = new ScenarioAuthorityModel();
      const originalBob = authority.freshLogin('Bob');
      const alice = authority.freshLogin('Alice');
      const room = authority.openDirect(alice, originalBob);
      const fresh: ExperimentResult = { title: 'Original principal', status: 'accepted',
        explanation: `The first Bob login created principal ${originalBob}.`, metrics: [{ label: 'Principal', value: String(originalBob) }] };
      authority.disconnect(originalBob);
      const replacement = authority.freshLogin('Bob');
      const reclaimed: ExperimentResult = { title: 'New principal', status: replacement !== originalBob ? 'accepted' : 'rejected',
        explanation: `Fresh login created principal ${replacement} for the same display name.`,
        metrics: [{ label: 'Display name', value: 'Bob' }, { label: 'Principal', value: String(replacement) }] };
      const canRead = authority.canReadDirect(room, replacement);
      return [fresh, reclaimed, { title: 'Old room denied', status: canRead ? 'accepted' : 'rejected',
        explanation: `Replacement principal ${replacement} has history access=${canRead}.`, metrics: [{ label: 'Old history access', value: canRead ? 'allowed' : 'denied' }] }];
    }
    case 'partial-idle': {
      const session = new SessionLifecycleModel({ idleTimeout: 30_000, maxFrames: 2, maxBytes: 100 });
      session.receivePartialBytes(2);
      session.sendOutbound(16);
      const partial: ExperimentResult = { title: 'Partial header buffered', status: 'waiting',
        explanation: `Two bytes arrived without a complete request; last activity remains ${session.lastActivity}ms.`,
        bytes: [0, 0], receivedBytes: 2, emitted: 0,
        metrics: [{ label: 'Header bytes', value: '2 / 4' }, { label: 'Activity refresh', value: String(session.lastActivity !== 0) }] };
      const expired = session.advanceTime(30_000);
      const deadline: ExperimentResult = { title: 'Deadline expires', status: expired === 'closed' ? 'closed' : 'waiting',
        explanation: `After partial input and outbound traffic, advancing to the deadline leaves session ${expired}.`,
        bytes: [0, 0], receivedBytes: 2, emitted: 0,
        metrics: [{ label: 'Complete requests', value: '0' }, { label: 'Session', value: expired }] };
      return [partial, deadline, { title: 'Connection state released', status: session.isOpen ? 'waiting' : 'closed',
        explanation: `The normal expiry path leaves the session ${session.isOpen ? 'open' : 'closed'}.`,
        bytes: [0, 0], receivedBytes: 2, emitted: 0, deliveries: [],
        metrics: [{ label: 'Remaining session', value: session.isOpen ? 'open' : 'closed' }] }];
    }
    default: return [];
  }
}

/** Replay deterministic educational actions; this model does not open sockets. */
export function experimentResult(id: string, step: number): ExperimentResult {
  const experiment = EXPERIMENTS.find((item) => item.id === id);
  if (!experiment) throw new RangeError(`Unknown experiment: ${id}`);
  if (!Number.isInteger(step) || step < 0 || step >= experiment.steps.length) {
    throw new RangeError(`Step ${step} is outside experiment ${id}`);
  }
  const states = buildExperimentStates(id);
  if (states.length !== experiment.steps.length) throw new Error(`Experiment ${id} model is out of sync with its authored steps.`);
  return structuredClone(states[step]);
}

export const runExperiment = experimentResult;
