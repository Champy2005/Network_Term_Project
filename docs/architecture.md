# Architecture and protocol

## Current checkpoint

This repository is being initialized through **Stage 1**. It defines the
Protocol Buffers wire schema and a standalone length-prefixed frame codec with
tests. It does not yet contain a working chat server, networked client, TLS,
rooms, persistence, GUI, or AI feature. Later-stage behavior below is a design
target, not a claim about the current executable.

The assignment requires separate client and server programs using socket
programming, at least two clients on different physical computers, unique
connected names, visible user and group lists, separate direct and group chat
rooms, self-join groups, and private delivery. A demonstration and
implementation-explanation slides will be needed to establish those
requirements; Stage 1 alone satisfies none of the end-to-end chat behaviors.

## Intended runtime architecture

```text
client program A ---- TCP socket ----+
                                    |
client program B ---- TCP socket ----+---- server program
                                    |       sessions -> hub/router -> rooms
client program C ---- TCP socket ----+                 |
                                              SQLite (later stage)
```

Each client talks only to the server. The server is authoritative for session
identity, connected users, direct rooms, groups, membership, message routing,
and room sequence numbers. A future headless client library will hide sockets,
framing, Protobuf, reconnection, and eventually TLS from a GUI. The CLI client
will exercise that library before a GUI exists.

The planned protocol stack is: application messages in `proto/chat.proto`,
Protocol Buffers serialization, four-byte length framing, TCP transport over
IP, and eventually TLS 1.3 over TCP. TCP is selected for ordered, reliable byte
delivery on each connection. IP routes between the physical computers, while
Wi-Fi or Ethernet supplies the local link. TLS is planned after plaintext TCP
works; Stage 1 does not encrypt traffic.

## Wire format

One frame is exactly:

```text
4-byte unsigned payload length (network/big-endian byte order)
N serialized bytes of chat.protocol.Envelope
```

`N` must be between 1 and 1,048,576 bytes (1 MiB), inclusive. A zero or
oversized declared length is invalid. TCP supplies a byte stream, so a read
may contain part of a header, part of a payload, one frame, or several frames.
The decoder buffers partial input, emits each complete frame in order, and
rejects an unfinished frame when the connection ends. The frame codec is
independent of sockets and Protobuf; the caller parses an emitted payload as
`Envelope` and validates its contents. Frame acceptance alone does not mean a
valid chat operation.

`Envelope.protocol_version` is 1 for this schema. `Envelope.request_id` is a
client-chosen operation identifier: a direct response echoes it, while a
server-initiated update may leave it blank. A receiver must reject missing or
unsupported versions, missing oneof payloads, malformed serialized messages,
and invalid payload fields. These semantic checks belong above the frame codec
and are not implemented at Stage 1. Unknown future fields can be ignored by
Protobuf, but a future incompatible version must be rejected explicitly.

### Message direction and meaning

| Payload | Direction | Meaning |
| --- | --- | --- |
| `LoginRequest` | client to server | Claim a username for this connection. |
| `LoginResponse` | server to client | Accept or reject that claim with an explanation. |
| `DirectMessage` | both | Send text to a named user; server delivers to the participants. |
| `CreateGroupRequest` | client to server | Create a group whose only initial member is the creator. |
| `JoinGroupRequest` | client to server | The requesting user joins by opaque `group_id`. |
| `GroupMessage` | both | Send text to a group; server delivers only to members. |
| `UserListUpdate` | server to client | Complete connected-user snapshot, including this client. |
| `GroupListUpdate` | server to client | Complete group and membership snapshot. |
| `Ping` / `Pong` | both | Echo a nonce to check connection liveness. |
| `Ack` | server to client | A request succeeded; carries assigned resource/message data when applicable. |
| `Error` | server to client | A request failed; carries a machine-readable code and explanation. |

For client-submitted `DirectMessage`, only `recipient` and `text` are meaningful;
`sender`, `room_id`, `message_id`, sequence, and server timestamp must be
empty/zero. For client-submitted `GroupMessage`, only `group_id` and `text` are
meaningful. The server derives the sender from the registered connection,
checks the target and membership, and fills delivery metadata. It must never
authorize a request based on client-supplied sender or room metadata.

Group IDs and direct room IDs are server-assigned opaque values. Group display
names are separate from IDs, so a client joins the selected `group_id` rather
than relying on the displayed name. A successful create request returns its new
group ID in `Ack.resource_id`. Accepted messages can return `message_id` and
`room_sequence` in `Ack`; the server also includes these fields in deliveries.
The exact ID generation and retry policy are future work.

### Planned flows

1. A client connects, sends `LoginRequest`, and receives `LoginResponse`.
   Duplicate active usernames are rejected by the server.
2. After login, the server sends connected-user and group snapshots. It sends
   replacements whenever those sets change.
3. A direct-message request is routed into one room for the user pair. Only
   the sender and recipient receive that room's messages.
4. A create-group request adds only its creator. Another user may join only by
   issuing their own `JoinGroupRequest`; the server checks membership before
   routing group messages.
5. The server acknowledges accepted operations or returns `Error` with the
   matching `request_id`.

These are intended flows. Their handlers and privacy enforcement are not part
of Stage 1.

## Concurrency and ownership plan

The future Boost.Asio server will accept multiple TCP connections. Each
`ClientSession` will own one socket and serialize its outgoing writes through
one ordered queue. Async operations must keep the session alive until they
finish or are cancelled. A central hub/router will coordinate users, sessions,
groups, rooms, and membership; sessions will submit events instead of mutating
shared state independently. This keeps authorization and disconnect cleanup in
one place. The exact executor/strand and database worker design will be chosen
and documented when those stages are implemented.

## Reliability and security plan

Stage 1 provides bounded framing and serialization primitives only. TCP will
later provide ordered, reliable bytes while a connection remains healthy. A
future stage may assign message IDs and monotonically increasing room sequence
numbers, acknowledge accepted messages, persist history in SQLite/WAL, and
resume from the last received sequence after reconnect. The present schema
leaves room for history/sync payloads (oneof tags 30 and above). Exactly-once
delivery is **not** promised; the eventual retry and deduplication rules must
be specified before a reliability claim is made.

Future limits will cover username, group name, text, pending write queue, and
heartbeat timeout in addition to the current 1 MiB frame bound. Group
membership and direct-message privacy must be enforced on the server. Login
currently means selecting a unique connected name, not proving a real-world
identity. TLS 1.3 and certificate validation are planned for Stage 12. Until
then, any future plaintext TCP demo should be used on a trusted network only.
Certificate handling is described in `certs/README.md`.

## Milestones

| Stage | Intended checkpoint |
| --- | --- |
| 0 | C++20 toolchain, dependencies, CMake presets, warnings, tests. |
| 1 | Protobuf generation, bounded framing, serialization and codec tests. |
| 2 | Plain TCP server and CLI client; framed `Ping`/`Pong`. |
| 3 | Safe session lifetime and ordered asynchronous writes. |
| 4 | Login and unique connected usernames. |
| 5 | Direct rooms and private message routing. |
| 6 | Group creation, discovery, self-join, member-only routing. |
| 7 | Connected-user and group membership updates. |
| 8 | SQLite/WAL history persistence. |
| 9 | Heartbeats and stale-connection cleanup. |
| 10 | Message IDs, room sequencing, acknowledgements. |
| 11 | Reconnection and history synchronization. |
| 12 | TLS 1.3 and certificate validation. |
| 13 | Reusable client API and CLI polish. |

Later, a separate GUI must provide a message display and text input for each
direct and group room, and the project must demonstrate an AI feature to earn
the assignment's special-feature points. Neither is scheduled as a Stage 1
deliverable.
