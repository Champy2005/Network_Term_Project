import type { ChapterDefinition } from './types';

export const CONTENT_VERSION = 1;

type BeatSeed = {
  title: string; caption: string; narration: string; focus: string; phase: string;
  sourceIds: string[]; view: 'wide' | 'close' | 'cutaway' | 'detail'; labels?: string[]; members?: string[];
};
type ChapterSeed = Omit<ChapterDefinition, 'beats'> & { beats: BeatSeed[]; deepDive: string };

const source = {
  wire: 'wire-envelope', frame: 'frame-decoder', encode: 'frame-encode', finish: 'frame-finish',
  client: 'client-api', tls: 'tls-verify', session: 'session-ownership', idle: 'session-idle',
  login: 'hub-login', direct: 'hub-direct', groups: 'hub-groups', storage: 'storage-transaction',
  identity: 'storage-identity', history: 'storage-history', worker: 'worker-recovery',
  heartbeat: 'worker-heartbeat', buffering: 'client-buffering', model: 'room-model',
  console: 'cli-input', build: 'build-targets',
};

const makeBeats = (seed: ChapterSeed): ChapterDefinition['beats'] => {
  let cursor = 0;
  const privateRoomEnds = [8, 18, 28, 40, 53, 65, 72, 80];
  const phaseDetails: Record<string, string> = {
    waiting: 'No application outcome is known yet; buffered bytes and connection state remain owned by their current layer.',
    submit: 'The client submits intent and correlation data, while the server derives authority from its connection registry.',
    encode: 'The frame codec adds a bounded big-endian length before the serialized Protobuf envelope.',
    split: 'The decoder retains incomplete stream bytes and emits a frame only after its whole declared payload arrives.',
    complete: 'A completed operation can be described only at the boundary that has observed its result.',
    verify: 'Validation checks configured trust or server-owned state before application work proceeds.',
    authorize: 'The hub checks login, target, or membership before it permits routing or a protected read.',
    commit: 'When storage is enabled, the transaction commits message state and sequence before acknowledgement or fanout.',
    ack: 'An ACK confirms server acceptance; it does not prove a recipient received or displayed the message.',
    deliver: 'The server creates deliveries for authorized participants and attaches server-owned identity and ordering data.',
    deny: 'A rejected operation produces no unauthorized room delivery and does not gain authority from client metadata.',
    join: 'Membership changes only after the requesting connection passes the server-side group check.',
    queue: 'Admission is bounded and ordered per connection; overload is handled as an explicit failure.',
    close: 'Cancellation converges on connection cleanup, with active operation buffers retained until handlers finish.',
    resume: 'Recovery retains bounded intent in memory and attempts the same configured endpoint and identity.',
    replay: 'Catch-up applies contiguous room sequences through a captured watermark before later live updates.',
    received: 'Server liveness advances only after a complete valid inbound request has been decoded.',
  };
  return seed.beats.map((beat, index) => {
    const end = seed.id === 'private-rooms'
      ? privateRoomEnds[index]
      : index === seed.beats.length - 1
      ? seed.duration
      : Math.round(((index + 1) * seed.duration) / seed.beats.length);
    const paragraphs = seed.deepDive.split(/(?<=[.!?])\s+/);
    const context = paragraphs[index % paragraphs.length];
    const item = {
      id: `${seed.id}-beat-${index + 1}`,
      start: cursor,
      end,
      title: beat.title,
      caption: beat.caption,
      narration: beat.narration,
      detail: `${beat.caption} ${phaseDetails[beat.phase] ?? ''} ${context}`,
      focus: beat.focus,
      view: beat.view,
      labels: beat.labels ?? [],
      phase: beat.phase,
      ...(beat.members ? { members: [...beat.members] } : {}),
      sourceIds: beat.sourceIds,
    };
    cursor = end;
    return item;
  });
};

const authored: ChapterSeed[] = [
  {
    id: 'system-map', title: 'A message has a route', subtitle: 'One server coordinates three clients',
    objective: 'Trace a chat message from a client through the authoritative server and back to the intended participants.',
    duration: 45, stage: 'overview', accent: '#69d8ff', experiments: [], takeaway: 'Every client talks to the server; the server decides identity, membership, and delivery.',
    deepDive: 'In this project the reusable client API hides sockets, framing, Protobuf, and TLS from the user interface. The server owns the registry and makes each routing decision. Network delivery and application acceptance are different events: a valid byte stream still needs a valid envelope and an authorized operation. The animation names these boundaries so the diagram never suggests that clients route around the server.',
    beats: [
      { title: 'Three participants', caption: 'Alice, Bob, and Charlie each hold a client connection to one server.', narration: 'Alice, Bob, and Charlie connect separately to one server; no client has a direct network path to another.', focus: 'server', phase: 'waiting', view: 'wide', sourceIds: [source.client] },
      { title: 'One request', caption: 'Alice submits a message addressed to Bob.', narration: 'Alice asks to message Bob. She supplies the recipient and text; the connection supplies her registered identity.', focus: 'alice', phase: 'submit', view: 'wide', sourceIds: [source.direct] },
      { title: 'Server decision', caption: 'The hub derives the sender and selects the pair room.', narration: 'The hub checks Alice’s login, resolves Bob, and selects the direct room for their participant pair.', focus: 'server', phase: 'authorize', view: 'cutaway', sourceIds: [source.direct, source.login] },
      { title: 'Private fanout', caption: 'The pair receives the delivery; Charlie is outside it.', narration: 'The hub routes to Alice and Bob. Charlie is connected, but he is not a participant in their direct room.', focus: 'bob', phase: 'deliver', view: 'wide', sourceIds: [source.direct] },
      { title: 'Boundary recap', caption: 'A client view shows state; it does not enforce privacy.', narration: 'The client renders rooms and messages; the server enforces privacy through its routing and membership checks.', focus: 'charlie', phase: 'complete', view: 'wide', sourceIds: [source.direct, source.model] },
    ],
  },
  {
    id: 'architecture', title: 'Layers with distinct jobs', subtitle: 'Application intent meets network bytes',
    objective: 'Separate the application API, serialized messages, framing, transport, and server responsibilities.',
    duration: 60, stage: 'overview', accent: '#8ca7ff', experiments: [], takeaway: 'Each layer solves a different problem and keeps its own contract.',
    deepDive: 'The public API is GUI-independent and uses plain values and typed errors. Protobuf describes message meaning, the frame codec creates message boundaries over TCP bytes, TCP carries an ordered stream while connected, and TLS authenticates the server and protects that stream. None of these layers decides group membership except the authoritative hub. Keeping the boundaries explicit makes partial reads, invalid requests, cancellation, and recovery easier to reason about.',
    beats: [
      { title: 'Intent at the edge', caption: 'The application asks for a typed operation, such as send_direct.', narration: 'A user action becomes a typed client operation. The interface does not construct wire bytes or decide which other users are allowed to receive a message.', focus: 'client', phase: 'submit', view: 'wide', sourceIds: [source.client] },
      { title: 'Envelope contract', caption: 'Protobuf represents one versioned request or event.', narration: 'The Protobuf envelope carries protocol version, request correlation, and one payload. Receivers still validate these fields because proto3 parsing alone does not enforce every rule.', focus: 'frame', phase: 'encode', view: 'detail', sourceIds: [source.wire] },
      { title: 'Byte stream', caption: 'A four-byte length prefix marks each payload boundary.', narration: 'The frame codec puts a four-byte big-endian payload length before serialized bytes. TCP preserves byte order, but a read may stop anywhere or contain several frames.', focus: 'frame', phase: 'split', view: 'cutaway', sourceIds: [source.encode, source.frame] },
      { title: 'Protected transport', caption: 'TLS 1.3 wraps TCP and verifies the server certificate.', narration: 'TLS 1.3 runs over the socket and validates the configured certificate authority and server name before application messages are exchanged.', focus: 'server', phase: 'verify', view: 'cutaway', sourceIds: [source.tls] },
      { title: 'Authoritative hub', caption: 'The server validates the operation and updates room state.', narration: 'The server hub owns active names, rooms, group membership, routing, and storage decisions; client requests cannot authoritatively set those values.', focus: 'server', phase: 'authorize', view: 'wide', sourceIds: [source.login, source.direct, source.groups] },
    ],
  },
  {
    id: 'client-boundary', title: 'The client API boundary', subtitle: 'A stable surface above the socket',
    objective: 'Explain what the blocking headless client owns and how the GUI-independent worker uses it.',
    duration: 75, stage: 'client', accent: '#8ce2c4', experiments: [], takeaway: 'The headless API owns protocol work; the GUI sees values and events.',
    deepDive: 'ChatClient offers connect, login, ping, direct and group operations, event reads, and close without exposing Boost.Asio, Protobuf, or frame bytes. Calls are blocking and one caller at a time. ChatWorker owns those calls on its networking thread and publishes bounded value updates. The room model consumes events separately. That separation keeps UI code from holding socket buffers or participating in network cancellation.',
    beats: [
      { title: 'A small API', caption: 'Call connect, login, send, receive events, and close.', narration: 'The headless ChatClient exposes operations and typed events. Callers use plain request and result values rather than socket handles or generated Protobuf objects.', focus: 'client', phase: 'waiting', view: 'wide', sourceIds: [source.client] },
      { title: 'Single caller', caption: 'Blocking operations have one caller per client instance.', narration: 'Each client instance owns its networking loop and blocking calls require one caller at a time, so applications commonly put that work on a worker thread.', focus: 'client', phase: 'submit', view: 'detail', sourceIds: [source.client] },
      { title: 'Events are values', caption: 'The client retains interleaved events while awaiting a reply.', narration: 'A response can arrive beside unsolicited deliveries or snapshots. The client buffers those complete events and preserves partial decoder input across later calls.', focus: 'queue', phase: 'queue', view: 'cutaway', sourceIds: [source.buffering] },
      { title: 'Worker ownership', caption: 'ChatWorker runs operations and forwards bounded updates.', narration: 'The worker owns client calls on its networking thread, tracks command receipts, and publishes plain updates that the interface can poll without touching the socket.', focus: 'client', phase: 'complete', view: 'close', sourceIds: [source.worker, source.client] },
      { title: 'Model separation', caption: 'RoomModel presents room state and local transcript values.', narration: 'The portable room model selects views and maintains local drafts and transcripts. Those display choices never grant access to a server room or its history.', focus: 'client', phase: 'deliver', view: 'wide', sourceIds: [source.model] },
    ],
  },
  {
    id: 'framing', title: 'TCP needs message boundaries', subtitle: 'Incremental four-byte length framing',
    objective: 'Trace split and coalesced reads through the bounded frame decoder.',
    duration: 90, stage: 'framing', accent: '#ffd27b', experiments: ['split-header', 'coalesced', 'oversized'], takeaway: 'The decoder buffers bytes until complete bounded frames can be emitted in order.',
    deepDive: 'A frame is four unsigned big-endian length bytes followed by one serialized Envelope payload. Payload length must be from 1 through 1,048,576 bytes. The decoder may retain an incomplete header or payload between feed calls and emits each complete payload once. finish distinguishes a clean boundary from truncated input. Parsing the payload and validating its meaning happen after framing.',
    beats: [
      { title: 'Length prefix', caption: 'The header is four bytes; the payload is one to 1 MiB.', narration: 'Each frame begins with an unsigned four-byte big-endian payload length. Zero bytes and values above one mebibyte are rejected before allocation.', focus: 'frame', phase: 'encode', view: 'wide', sourceIds: [source.encode] },
      { title: 'Header split', caption: 'Two header bytes arrive, so no payload is emitted yet.', narration: 'TCP can deliver only part of a header. The decoder retains those bytes and waits for more, because an incomplete length cannot define a frame.', focus: 'frame', phase: 'split', view: 'detail', sourceIds: [source.frame] },
      { title: 'Payload split', caption: 'A complete header can still precede a partial payload.', narration: 'Once all four header bytes arrive, the decoder checks the bound and accumulates the declared payload. It emits nothing until every payload byte is present.', focus: 'frame', phase: 'waiting', view: 'detail', sourceIds: [source.frame] },
      { title: 'Coalesced frames', caption: 'One read may finish one frame and contain another.', narration: 'A feed call can contain several complete frames and a fragment of the next one. The decoder emits complete frames in order and keeps the trailing fragment.', focus: 'frame', phase: 'complete', view: 'wide', sourceIds: [source.frame] },
      { title: 'EOF is a boundary', caption: 'finish accepts a clean boundary and rejects truncation.', narration: 'When the connection ends, finish succeeds only if no header or payload remains incomplete. Frame acceptance still does not validate an Envelope.', focus: 'frame', phase: 'close', view: 'close', sourceIds: [source.finish] },
    ],
  },
  {
    id: 'tls-trust', title: 'Trust before application data', subtitle: 'TLS 1.3 authenticates the server endpoint',
    objective: 'Describe verified TLS trust, transport mode selection, and handshake failure boundaries.',
    duration: 70, stage: 'tls', accent: '#a9a1ff', experiments: [], takeaway: 'A failed certificate check is terminal; the client does not fall back to plaintext.',
    deepDive: 'The client uses an explicit CA and validates the peer certificate plus DNS or IP subject alternative name. TLS negotiation completes before login or chat messages. TLS protects traffic to the server and authenticates that server; it is not end-to-end encryption between Alice and Bob. Executables require TLS or an explicit plaintext development flag, and plaintext mode is limited to loopback. Cancellation closes the underlying socket and releases pending handler-owned buffers.',
    beats: [
      { title: 'Choose a mode', caption: 'Executables require TLS or explicit loopback development mode.', narration: 'The command line must select verified TLS or an explicit plaintext development mode. Plaintext connections are accepted only for loopback addresses.', focus: 'client', phase: 'submit', view: 'wide', sourceIds: [source.tls] },
      { title: 'Verify the peer', caption: 'The chain and DNS or IP name must match configured trust.', narration: 'The client verifies a trusted certificate chain and checks the requested DNS name or IP address against the certificate before any application exchange.', focus: 'server', phase: 'verify', view: 'detail', sourceIds: [source.tls] },
      { title: 'Handshake first', caption: 'Application login starts after the TLS handshake succeeds.', narration: 'TLS negotiation wraps the connected TCP socket. The application sends no login envelope until the handshake has completed successfully.', focus: 'frame', phase: 'waiting', view: 'cutaway', sourceIds: [source.tls] },
      { title: 'Failure stays visible', caption: 'Trust and protocol errors do not downgrade transport security.', narration: 'An invalid certificate or hostname is a terminal TLS error. Recovery never silently switches to plaintext or changes the configured identity.', focus: 'client', phase: 'deny', view: 'close', sourceIds: [source.tls, source.worker] },
      { title: 'Know the limit', caption: 'TLS protects each client-to-server connection, not end-to-end chats.', narration: 'TLS protects traffic between a client and the server and authenticates the server. The server can read application messages; this design is not end-to-end encryption.', focus: 'server', phase: 'complete', view: 'wide', sourceIds: [source.tls] },
    ],
  },
  {
    id: 'session-lifecycle', title: 'One connection, one write path', subtitle: 'Ownership, order, bounds, and cancellation',
    objective: 'Show how a server session owns asynchronous reads, writes, queues, and cleanup.',
    duration: 80, stage: 'session', accent: '#ffb77c', experiments: ['slow-receiver', 'cancel-write'], takeaway: 'A session serializes writes, bounds queued bytes, and closes through one cleanup path.',
    deepDive: 'Each ClientSession owns its socket stream, incremental decoder, read buffer, steady-clock timer, and bounded outbound queue. Admission is synchronized before executor work is posted. One scheduled pump runs one composed write at a time, and its active buffer remains alive while cancellation completes. Queue overflow, protocol failure, peer reset, idle expiry, and server stop converge on cleanup that removes hub state. A clean half-close can drain admitted replies; abortive failures discard pending work.',
    beats: [
      { title: 'Session owns state', caption: 'The session holds the stream, decoder, queue, and timer.', narration: 'A connection session owns its transport, framing state, read buffer, write queue, and idle timer for the lifetime of its asynchronous handlers.', focus: 'server', phase: 'waiting', view: 'wide', sourceIds: [source.session] },
      { title: 'Bound admission', caption: 'The queue limits frame count and framed bytes, including active write.', narration: 'Outgoing frames enter through bounded admission that counts queued frames and bytes, including the active write, before executor work can accumulate.', focus: 'queue', phase: 'queue', view: 'cutaway', sourceIds: [source.session] },
      { title: 'One ordered pump', caption: 'A single composed writer preserves admission order on each socket.', narration: 'Only one write pump is active per socket. This gives that connection one ordered write path even when several producers request sends.', focus: 'frame', phase: 'submit', view: 'detail', sourceIds: [source.session] },
      { title: 'Slow receiver', caption: 'A client that cannot drain eventually reaches the queue bound.', narration: 'If a receiver stays slow and outbound admission exceeds its configured frame or byte budget, the session closes and discards its pending queue.', focus: 'queue', phase: 'close', view: 'cutaway', sourceIds: [source.session] },
      { title: 'Cancel safely', caption: 'The active handler retains its buffer until completion.', narration: 'Closing the socket cancels pending I/O, while handlers retain the buffers they reference until they finish. Hub cleanup releases identity and group membership.', focus: 'server', phase: 'complete', view: 'close', sourceIds: [source.session, source.login, source.groups] },
    ],
  },
  {
    id: 'identity', title: 'A name is not proof of identity', subtitle: 'Active names and durable capabilities',
    objective: 'Distinguish a connection-scoped display name from explicit durable identity resume.',
    duration: 70, stage: 'identity', accent: '#ffe68e', experiments: ['reclaim-name'], takeaway: 'A fresh login with a familiar name is a new principal; resume requires its token.',
    deepDive: 'Without storage, login reserves a unique active username on the hub strand, then disconnect releases it. With storage, each fresh login receives a durable principal and a random resume token; only a hash is stored. A new claimant using an old display name receives a different principal and cannot read the old direct history. Explicit resume requires both the matching name and original bearer capability. The token is not a password or proof of a real-world person.',
    beats: [
      { title: 'Validate a claim', caption: 'Names use a bounded ASCII alphabet and are case sensitive.', narration: 'Login accepts one to thirty-two ASCII letters, digits, or underscores. Names are case sensitive, and the server validates them before reserving a slot.', focus: 'alice', phase: 'submit', view: 'wide', sourceIds: [source.login] },
      { title: 'Reserve atomically', caption: 'The hub binds the accepted name to this connection.', narration: 'The authoritative hub checks the active registry and reserves an unused name on its strand, binding that identity to this connection.', focus: 'server', phase: 'authorize', view: 'detail', sourceIds: [source.login] },
      { title: 'Disconnect cleanup', caption: 'Closing a session releases its active name and membership.', narration: 'When the session disconnects, hub cleanup removes its name and connection-scoped group memberships, including protocol and cancellation cleanup paths.', focus: 'server', phase: 'close', view: 'cutaway', sourceIds: [source.login, source.groups] },
      { title: 'Fresh durable login', caption: 'A reclaimed display name receives a new principal.', narration: 'With durable storage, a fresh login creates a new principal even when its display name matches a former user; name equality grants no old history.', focus: 'identity', phase: 'commit', view: 'detail', sourceIds: [source.identity] },
      { title: 'Explicit resume', caption: 'The original token proves continuity for the stored principal.', narration: 'Resume requires the matching username and original random bearer token. Storage keeps only its hash, and active name uniqueness still applies.', focus: 'identity', phase: 'verify', view: 'close', sourceIds: [source.identity] },
    ],
  },
  {
    id: 'private-rooms', title: 'A direct room is a participant pair', subtitle: 'Private routing stays server authoritative',
    objective: 'Trace an Alice-to-Bob message and show why Charlie receives no direct delivery.',
    duration: 80, stage: 'routing', accent: '#75d9ff', experiments: ['forged-sender'], takeaway: 'Direct delivery follows the server-owned participant pair; forged sender fields are rejected.',
    deepDive: 'The server derives the sender from the logged-in connection and resolves the recipient to the currently connected target. Direct room identity is scoped to the participant pair, and only those participants receive its deliveries. Client-submitted sender, room ID, message ID, sequence, and timestamp must be empty or zero; they never grant authority. Storage-backed direct rooms use durable principal pairs, so a reclaimed display name cannot inherit the former person’s room. ACK means server acceptance, not recipient receipt.',
    beats: [
      { title: '0s · Alice submits', caption: 'Alice names Bob and supplies text; sender identity comes from login.', narration: 'Alice names Bob and sends text; the server already knows which connection belongs to Alice.', focus: 'alice', phase: 'submit', view: 'wide', sourceIds: [source.direct, source.login] },
      { title: '8s · Request fields', caption: 'Recipient, text, and retry key are client fields; sender and room metadata are server-owned.', narration: 'The request carries Bob, text, and an optional retry key. Forged sender or room fields are rejected.', focus: 'frame', phase: 'verify', view: 'detail', sourceIds: [source.wire, source.direct] },
      { title: '18s · Identity binding', caption: 'The hub derives Alice from the connection accepted at login.', narration: 'At login, the hub binds Alice to this session and derives the sender from that binding.', focus: 'identity', phase: 'authorize', view: 'detail', sourceIds: [source.login, source.direct] },
      { title: '28s · Pair room', caption: 'The server resolves Bob and selects their direct room.', narration: 'The hub resolves Bob’s active session and chooses the room for this identity pair.', focus: 'server', phase: 'verify', view: 'cutaway', sourceIds: [source.direct] },
      { title: '40s · Transaction', caption: 'When storage is enabled, message and sequence commit before acknowledgement.', narration: 'Optional storage commits the message and sequence before an ACK or delivery is queued.', focus: 'storage', phase: 'commit', view: 'cutaway', sourceIds: [source.storage, source.direct] },
      { title: '53s · ACK and echo', caption: 'Alice receives an ACK and sender echo; Bob receives the direct delivery.', narration: 'After commit, the server ACKs Alice and sends the same direct delivery to Alice and Bob.', focus: 'alice', phase: 'deliver', view: 'wide', sourceIds: [source.direct, source.storage] },
      { title: '65s · Charlie stays outside', caption: 'Presence lists Charlie, but this pair room does not include him.', narration: 'Charlie may appear in presence, but he is outside this pair room and receives no direct message.', focus: 'charlie', phase: 'deny', view: 'wide', sourceIds: [source.direct] },
      { title: '72s · Privacy holds', caption: 'The server enforces delivery; a client room view grants no access.', narration: 'Only the server enforces private delivery; a client room view grants no access.', focus: 'server', phase: 'complete', view: 'wide', sourceIds: [source.direct] },
    ],
  },
  {
    id: 'groups', title: 'Joining is a user action', subtitle: 'Creation, membership, and member-only delivery',
    objective: 'Explain creator-only initial membership, explicit self-join, and server-enforced group routing.',
    duration: 80, stage: 'groups', accent: '#94e8bd', experiments: ['nonmember'], takeaway: 'Creating a group does not add other users; each member joins through their own request.',
    deepDive: 'A group receives an opaque server-assigned ID separate from its display name. Creation initially admits only the creator. Another user must issue a join request for that ID using their own registered, logged-in connection. The hub stores active membership and checks it before routing group text. Disconnect clears active membership, including after durable identity resume; a resumed user must self-join again. Historical admissions are audit data, not live permission. UI lists and buttons help navigation but do not authorize access.',
    beats: [
      { title: 'Create with a name', caption: 'Alice creates a group and receives its opaque group ID.', narration: 'Alice sends a group display name. The server validates it, creates the group, and returns an opaque ID that distinguishes it from duplicate names.', focus: 'alice', phase: 'submit', view: 'wide', members: ['Alice'], sourceIds: [source.groups] },
      { title: 'Creator only', caption: 'Initially Alice is the only active member.', narration: 'The hub adds the creator as the first member. Bob and Charlie remain outside until each makes an explicit join request.', focus: 'group', phase: 'join', view: 'cutaway', members: ['Alice'], sourceIds: [source.groups] },
      { title: 'Bob asks to join', caption: 'Bob submits his own request using the server-assigned group ID.', narration: 'Bob uses his own logged-in connection to request membership by opaque group ID; the creator cannot silently add him on his behalf.', focus: 'bob', phase: 'submit', view: 'close', members: ['Alice'], sourceIds: [source.groups] },
      { title: 'Membership changes', caption: 'The server updates membership and publishes a complete snapshot.', narration: 'After a valid join, the hub updates active membership and broadcasts a full group snapshot so every client can render current members.', focus: 'server', phase: 'join', view: 'wide', members: ['Alice', 'Bob'], sourceIds: [source.groups] },
      { title: 'Member delivery', caption: 'Alice’s group message reaches Alice and Bob; Charlie is excluded.', narration: 'Alice sends group text. The hub routes it to Alice and Bob, the current members, while Charlie receives nothing.', focus: 'group', phase: 'deliver', view: 'wide', members: ['Alice', 'Bob'], sourceIds: [source.groups] },
      { title: 'Route only to members', caption: 'Charlie’s group send is rejected before fanout.', narration: 'Before routing group text, the hub checks the sender’s active membership. Charlie’s nonmember request is rejected and creates no delivery.', focus: 'group', phase: 'deny', view: 'cutaway', members: ['Alice', 'Bob'], sourceIds: [source.groups] },
    ],
  },
  {
    id: 'storage', title: 'Commit before acknowledgement', subtitle: 'SQLite history and explicit read access',
    objective: 'Show transaction boundaries, authorized history access, and what durable storage does not imply.',
    duration: 80, stage: 'storage', accent: '#ffad88', experiments: ['lost-ack'], takeaway: 'When enabled, a message commit precedes ACK and routing; history still requires authorization.',
    deepDive: 'The server’s SQLite store uses WAL and transaction boundaries to keep message insertion and room sequence advancement together. Direct room creation and keyed message insertion commit before any ACK or fanout is queued. Failures reserve neither a sequence nor a retry key. History is explicit, bounded, and authorized for current participants; group history requires current membership. A stored row does not promise recipient receipt, automatic replay to every client, or unlimited disk capacity. Storage is optional in the embedded server API.',
    beats: [
      { title: 'Storage is a server choice', caption: 'The executable enables SQLite; the embedded server can be ephemeral.', narration: 'The server program enables SQLite history by default, while an embedded server configuration can run without persistent storage.', focus: 'storage', phase: 'waiting', view: 'wide', sourceIds: [source.storage] },
      { title: 'One transaction', caption: 'Message data and the room sequence advance together.', narration: 'A successful stored message and its room sequence are written transactionally, so a failed operation does not consume a sequence number.', focus: 'storage', phase: 'commit', view: 'cutaway', sourceIds: [source.storage] },
      { title: 'Then acknowledge', caption: 'No receipt or delivery is queued before a successful commit.', narration: 'The hub waits for the storage operation to commit before queueing the acknowledgement or routing the message to connected participants.', focus: 'server', phase: 'ack', view: 'detail', sourceIds: [source.storage, source.direct] },
      { title: 'Read is explicit', caption: 'History pages are bounded and checked against room authority.', narration: 'History requests ask for bounded pages. The server verifies direct participation or current group membership before returning stored message content.', focus: 'history', phase: 'authorize', view: 'cutaway', sourceIds: [source.history, source.groups] },
      { title: 'Durability has limits', caption: 'Stored acceptance does not promise delivery or display.', narration: 'A committed row survives server restart, but a message ACK does not promise recipient receipt, display, automatic replay, or bounded database growth.', focus: 'storage', phase: 'complete', view: 'wide', sourceIds: [source.storage, source.history] },
    ],
  },
  {
    id: 'heartbeat', title: 'Liveness is measured by requests', subtitle: 'Ping cadence and complete-request expiry',
    objective: 'Explain client heartbeat cadence and why partial bytes or outbound traffic do not refresh server activity.',
    duration: 55, stage: 'heartbeat', accent: '#a0c6ff', experiments: ['partial-idle'], takeaway: 'Only complete valid inbound requests refresh the server idle timer.',
    deepDive: 'The worker sends correlated Ping requests on a periodic cadence and expects the matching Pong under an operation deadline. Separately, the server’s session timer uses the time of the last complete valid inbound request. A partial frame header, partial payload, or server outbound message does not refresh that time. Expiry closes the session through normal cleanup, releasing active name, membership, and connection slot. A zero configured timeout disables expiry; client heartbeat cadence must suit custom deadlines.',
    beats: [
      { title: 'Periodic Ping', caption: 'The worker sends a correlated heartbeat while connected.', narration: 'The chat worker sends a Ping periodically and checks its correlated Pong, including the nonce, while it owns the client connection.', focus: 'client', phase: 'submit', view: 'wide', sourceIds: [source.heartbeat] },
      { title: 'Server activity clock', caption: 'A complete valid inbound request updates last activity.', narration: 'The session refreshes its activity time only after decoding a complete valid client request, not simply because bytes arrived.', focus: 'clock', phase: 'received', view: 'detail', sourceIds: [source.idle] },
      { title: 'Partial bytes do not count', caption: 'An unfinished header remains buffered without extending liveness.', narration: 'A slow peer can drip a partial header, but those incomplete bytes do not keep the session alive beyond its configured idle timeout.', focus: 'frame', phase: 'waiting', view: 'close', sourceIds: [source.idle, source.frame] },
      { title: 'Outbound is separate', caption: 'Server broadcasts do not prove the peer is responsive.', narration: 'Outgoing snapshots or messages also do not refresh client activity; a successful server write cannot establish that the remote application is still making requests.', focus: 'server', phase: 'deliver', view: 'cutaway', sourceIds: [source.idle] },
      { title: 'Expire and clean up', caption: 'Timeout closes the session and releases connection-scoped state.', narration: 'On expiry the session closes through the usual hub cleanup, releasing its active username, group membership, socket, and connection capacity.', focus: 'server', phase: 'close', view: 'wide', sourceIds: [source.idle, source.login, source.groups] },
    ],
  },
  {
    id: 'recovery', title: 'Reconnect with bounded intent', subtitle: 'Resume, rejoin, replay, and retry',
    objective: 'Describe worker recovery, identity capability retention, group rejoin, contiguous replay, and saved-key retry behavior.',
    duration: 100, stage: 'recovery', accent: '#c1a3ff', experiments: ['lost-ack', 'reclaim-name'], takeaway: 'Recovery is bounded and capability-based; it does not promise exactly-once delivery.',
    deepDive: 'The worker retains its resume capability, selected group intents, pending message keys, and bounded room cursors in memory for its current run. It reconnects with stop-aware backoff, resumes the same durable principal, self-rejoins chosen groups, captures sequence watermarks, and applies contiguous records before later live updates. If an ACK was lost after commit, retrying the same saved key returns the original receipt without another fanout. Malformed responses, authorization errors, gaps, or exhausted retries remain terminal. No token or cursor is persisted across process restart.',
    beats: [
      { title: 'Keep bounded intent', caption: 'The worker retains a capability, message keys, group choices, and cursors.', narration: 'During its current run, the worker retains the durable resume capability, pending retry keys, chosen groups, and bounded room cursors in memory.', focus: 'recovery', phase: 'queue', view: 'wide', sourceIds: [source.worker] },
      { title: 'Reconnect carefully', caption: 'Backoff and operation deadlines bound each recovery attempt.', narration: 'On transport failure the worker uses stop-aware backoff and bounded operation deadlines; it does not retry malformed protocol or trust failures as network loss.', focus: 'client', phase: 'resume', view: 'cutaway', sourceIds: [source.worker, source.tls] },
      { title: 'Resume same principal', caption: 'The original capability proves durable identity continuity.', narration: 'The worker resumes with its saved bearer capability and matching username, recovering the original durable principal instead of relying on the display name alone.', focus: 'identity', phase: 'verify', view: 'detail', sourceIds: [source.identity, source.worker] },
      { title: 'Rejoin chosen groups', caption: 'Membership is connection-scoped and must be requested again.', narration: 'After reconnect, the worker submits its selected self-join requests because active group membership ended with the old connection.', focus: 'group', phase: 'join', view: 'wide', sourceIds: [source.worker, source.groups] },
      { title: 'Replay a fixed range', caption: 'A captured watermark bounds contiguous room sequence catch-up.', narration: 'The worker captures a room watermark and applies contiguous sequence records through that limit before publishing later live deliveries.', focus: 'history', phase: 'replay', view: 'cutaway', sourceIds: [source.worker, source.history] },
      { title: 'Retry uncertain send', caption: 'A saved key returns the original ACK without a second delivery.', narration: 'If a message committed but its ACK was lost, retrying the same saved sender-scoped key returns the original receipt without repeating fanout.', focus: 'server', phase: 'ack', view: 'detail', sourceIds: [source.worker, source.storage] },
      { title: 'No exactly-once claim', caption: 'Failures outside the retry contract remain visible to the caller.', narration: 'Retries are bounded and deduplicate accepted saved keys within their documented scope; the system does not promise exactly-once delivery.', focus: 'recovery', phase: 'complete', view: 'wide', sourceIds: [source.worker, source.storage] },
    ],
  },
  {
    id: 'put-it-together', title: 'Read the system as evidence', subtitle: 'Trace a claim to its source boundary',
    objective: 'Practice connecting an animated claim to the implementation and its observable limits.',
    duration: 60, stage: 'overview', accent: '#f49dd0', experiments: [], takeaway: 'A visualization is a guide to contracts, not a substitute for source or behavioral evidence.',
    deepDive: 'CMake generates the Protobuf library and composes framing, transport, server, CLI, GUI, and test targets. The headless API owns socket exchanges, ChatWorker owns calls on its thread, and RoomModel owns portable presentation values. ChatHub enforces connection identity, room routing, group membership, and history authorization; HistoryStore commits records transactionally when enabled. Unit tests cover codec and value behavior, socket integration tests exercise delivery and cleanup, and Windows control tests cover native GUI interactions. These automated checks do not replace a visible GUI walkthrough, physical-network demonstration, or future AI acceptance.',
    beats: [
      { title: 'Build the project', caption: 'CMake creates protocol, transport, server, CLI, GUI, and test targets.', narration: 'Build targets separate the protocol, transport, authoritative server, persistent CLI, Windows GUI, and unit, socket-integration, and control-test executables.', focus: 'frame', phase: 'waiting', view: 'wide', sourceIds: [source.build, source.wire, source.frame] },
      { title: 'Trace an operation', caption: 'Follow a typed API request through the client and server boundaries.', narration: 'The headless client owns socket operations; the worker runs it, while the room model presents values without handling wire data.', focus: 'client', phase: 'submit', view: 'close', sourceIds: [source.client, source.worker, source.model] },
      { title: 'Find the authority', caption: 'The hub performs login, room, group, routing, and history checks.', narration: 'ChatHub validates active identity and membership before routing; HistoryStore commits durable records and checks authorized history access.', focus: 'server', phase: 'authorize', view: 'detail', sourceIds: [source.login, source.direct, source.groups, source.storage] },
      { title: 'Inspect observable proof', caption: 'Unit and socket tests cover framing, privacy, storage, and recovery.', narration: 'Unit tests exercise framing and state models; socket integration tests exercise real delivery, privacy, and connection cleanup.', focus: 'frame', phase: 'verify', view: 'cutaway', sourceIds: [source.build, source.frame, source.direct, source.idle] },
      { title: 'Keep claims precise', caption: 'Automated loopback evidence does not establish a physical-network demonstration.', narration: 'Automated evidence covers specified local behavior. Visible GUI walkthroughs, a physical-network demonstration, and AI feature acceptance remain separate.', focus: 'server', phase: 'complete', view: 'wide', sourceIds: [source.direct, source.tls] },
    ],
  },
];

export const CHAPTERS: ChapterDefinition[] = authored.map((chapter) => {
  const { deepDive: _authoringNotes, ...definition } = chapter;
  return { ...definition, beats: makeBeats(chapter) };
});

export const GLOSSARY = [
  { term: 'Envelope', meaning: 'The versioned Protobuf container holding one request, response, or event.' },
  { term: 'Frame', meaning: 'A four-byte big-endian payload length followed by one serialized envelope.' },
  { term: 'Room sequence', meaning: 'A server-assigned increasing number for ordering accepted messages within one room.' },
  { term: 'Resume token', meaning: 'A random bearer capability used to resume one durable principal; it is not a password.' },
  { term: 'Watermark', meaning: 'A fixed upper sequence captured to bound one reconnect catch-up range.' },
  { term: 'Self-join', meaning: 'A request from a user’s own logged-in connection to enter a group.' },
];

export const glossary = GLOSSARY;
