# Implementation plan

This plan turns the [assignment requirements](requirements.md) into small,
testable checkpoints. Stage numbers 0-13 follow the team's agreed networking
sequence. Stages 14-16 cover the GUI, special features, and graded delivery
that the networking sequence alone would leave unfinished. The
[progress tracker](progress.md) distinguishes completed work from this plan.

## Ground rules for every stage

- Keep the server authoritative for identity, membership, room state, and
  routing. A client UI is not an authorization boundary.
- Use the existing length-frame codec for every future TCP message. Preserve
  the published Protobuf field numbers; add new fields instead of reusing old
  tags.
- Add automated tests for the new behavior and preserve all earlier tests.
  Build Debug and Release; use the sanitizer preset when supported.
- Handle disconnection and malformed input explicitly. Record limits and
  failure behavior in [architecture](architecture.md).
- Update [progress](progress.md), this plan if scope changes, and the
  [requirements evidence](requirements.md) when behavior is actually verified.
- End a stage only after its exit criteria pass. A compiling stub does not
  count as a working feature.

## Network foundation

| Stage | Work | Exit criteria and tests |
| --- | --- | --- |
| **0. Toolchain** | C++20, CMake/vcpkg manifest, `debug`/`release`/`asan` presets, warnings, GoogleTest. | Fresh configure/build/test works with the documented toolchain. |
| **1. Protocol and framing** | Generate C++ from `proto/chat.proto`; encode four-byte big-endian lengths; incrementally decode 1 to 1,048,576 byte frames. | Unit tests cover partial/coalesced frames, bounds, EOF truncation, Protobuf round-trip, and malformed serialized data. No socket behavior claimed. |
| **2. Plain TCP proof** | Add separate `apps/server` and `apps/cli` entry points; accept multiple connections; carry `Ping`/`Pong` in framed Protobuf over real TCP sockets. | Integration tests connect more than one client and verify matching Pong nonces, partial TCP reads, failed connection handling, and clean shutdown. Record an early two-computer LAN Ping/Pong smoke test when the second machine is available. |
| **3. Session ownership** | Introduce `ClientSession` lifetime management and one ordered async write path per connection. Define the hub/router executor boundary. | Stress tests send concurrent queued messages and disconnect during reads/writes without use-after-free, lost framing boundaries, or reordered writes. |
| **4. Login** | Validate usernames and make active names unique in the server registry. Bind requests to the registered connection. | Two clients claiming the same name cannot both log in; a rejected or unregistered client cannot chat; disconnect releases active presence. |
| **5. Direct rooms** | Give each user pair one direct room and route direct text only to its participants. | With Alice, Bob, Charlie connected, Alice's DM reaches Bob (and any sender-side status/echo by design), while Charlie receives no direct payload. Separate pairs remain separate. |
| **6. Groups** | Create group IDs and names, add only the creator, expose membership, accept self-join, and route group text to members. | Creator-only initial membership; Bob joins by his own request; Alice and Bob receive group text; nonmember Charlie receives nothing. Forged sender and nonmember send attempts fail. |
| **7. Presence and discovery** | Publish connected-user snapshots and all group/member snapshots on login, join, create, and disconnect. | Every client sees its own name and current others; every client sees all groups and members; updates converge after state changes. |

Stages 2-7 establish the network behavior behind R1-R5 and R7-R11, but they do
**not** finish the assignment: R1 still needs the two-physical-computer demo,
and R5-R6 need usable room views and text controls in the client GUI.

**Grading priority:** after Stage 7, the team should finish a minimal headless
client API and Stage 14 room GUI, then verify R1-R11 across physical computers.
This work can precede Stages 8-13. History, recovery, and TLS improve the
chosen architecture, but they must not postpone the fundamental grading gate.

## Durability, security, and client boundary

| Stage | Work | Exit criteria and tests |
| --- | --- | --- |
| **8. History** | Store users/rooms/membership/messages in SQLite with WAL; define when a message is considered accepted. | Room history survives server restart; authorized participants can load it; unrelated users and group nonmembers cannot. |
| **9. Heartbeats** | Periodic client `Ping`/`Pong`, complete inbound request activity, and configurable stale-session timers. | Quiet healthy clients survive; stale registered/unregistered peers expire, names/membership/slots are released, partial bytes/outbound traffic do not extend liveness, and read/write/timer cancellation releases ownership. |
| **10. IDs and acknowledgements** | Assign stable message IDs and monotonic per-room sequence numbers; acknowledge accepted operations and define duplicate handling. | Tests establish ordering, correlation through `request_id`, retry behavior, and absence of duplicate stored messages under the documented policy. Do not claim exactly-once delivery. |
| **11. Reconnect and sync** | Add history/sync protocol messages and resume from the last received room sequence. | After a forced disconnect, a client catches up without gaps or duplicate display under the chosen delivery policy. |
| **12. TLS** | Layer TLS 1.3 over the socket transport, verify the server chain and hostname, and document local certificate setup. | Valid certificates connect; wrong hostname, untrusted CA, and expired or invalid certificates fail. Plaintext behavior is retired or explicitly limited to a documented development mode. |
| **13. Client API and CLI polish** | Harden the headless API and expose persistent CLI discovery/DM/create/join/group flows, worker recovery, bounded input and receipt draining, while preserving diagnostics. | API users do not handle Boost.Asio, frame bytes, or Protobuf parsing. Reconnect/events and real CLI core/privacy/EOF/failure flows pass; native terminal acceptance remains separate. |

Stage 8's [history guide](history.md) defines commit-before-ACK,
bounded cursor pages, durable direct-participant capabilities, and group history
access after explicit current membership. Active membership still clears on
disconnect/restart; stored admissions are audit data. Resume is explicit through
the headless API. Stage 11 implements private sequence paging, bounded worker
recovery, and GUI replay; see [recovery](recovery.md).
Stage 10's [reliability guide](reliability.md) defines schema v2 migration, message
IDs/sequences, saved-key retry deduplication, and bounded ephemeral protection.
Verified TLS and certificate configuration are implemented in Stage 12; see
[TLS setup](tls.md). `Envelope` uses history tags
30-31, sync tags 32-33, and private direct-room discovery tags 34-35.
Stage 13 implements the persistent worker-backed [CLI](cli.md); no new wire tags
are needed for its core commands.

## Assignment completion

The [interactive onboarding](onboarding.md) application is separate educational
tooling. Its implementation, rendering/audio polish, physical performance, and
learner evaluation have their own acceptance gates; it does not replace any
graded live demonstration, code evidence, GUI walkthrough, or AI feature.

| Stage | Work | Exit criteria and evidence |
| --- | --- | --- |
| **14. Room GUI** | Build a client UI with connected users, all groups and members, separate direct/group room views, message history, and text input/send controls. This may be pulled forward after Stage 7 and a minimal client API. | A user can complete R3-R11 in the UI. Every room displays its own sent and incoming text. Three-client privacy scenarios pass; no background socket work freezes the UI. |
| **15. Special features** | Choose and implement up to four demonstrable features; at least one must use AI. Record the AI model and source. | Each claimed feature works live, has test evidence and a code snapshot, and is isolated from core routing/authorization. Do this after all Part 2 fundamentals pass. |
| **16. Demonstration and slides** | Run a server and at least two clients on different physical computers. Prepare the implementation-explanation deck and rehearse the required scenarios. | Architecture and protocol choices are explained; code snapshots match the final code; R1-R11 and each claimed special feature are shown live. The instructor can ask about socket code, framing, and privacy enforcement. |

Stage 14's first adapter uses native Windows controls, with a portable C++
room model and a worker owning the existing blocking client. Local transcripts
are bounded and reset on a new login; this is not Stage 8 persistence. Hidden
native-control tests exercise networking flows. Visual, keyboard, and three
visible GUI-client walkthroughs remain acceptance work; see the
[GUI guide](setup/gui.md). Their pending status does not block Stage 8 development.

The [testing guide](testing.md) contains the detailed automated and manual
scenarios. Keep a short record of the machine names, network, executable
versions, test results, and evidence captures used for the final demonstration.

## Work that can run in parallel

The numbered stages are **acceptance gates**, not a rule that every teammate
must wait idle or that optional hardening must finish before the GUI. The GUI
team can sketch room views and build a UI shell while the networking core is
being written, using a mock client-event interface. They should integrate it
with the real client API only after its minimum contract is stable, and Stage
14 is complete only when the real three-client flows pass. The
AI/presentation owner can evaluate candidate features and maintain an evidence
plan early; special-feature implementation and grading claims come after the
Part 2 fundamentals. OS setup checks can run at any time and should be
recorded in [progress](progress.md).
