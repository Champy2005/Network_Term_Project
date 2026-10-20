# Progress tracker

**Last reviewed:** 2026-10-07. This page records verified work, while the
[implementation plan](plan.md) describes intended future work. A status changes
to **Done** only after its exit criteria and tests pass. The
[requirements checklist](requirements.md) separately tracks what the graded
assignment still needs.

## Current checkpoint

**Stages 0-13 are complete for local automated acceptance.** The repository has
separate runnable server/CLI programs, bounded sessions, server-validated login,
private direct rooms, voluntary group membership, and presence/group snapshots.
A GUI-independent headless chat API exposes operations and typed events.
Stage 14's Windows room GUI and portable worker/model are implemented with
automated evidence. SQLite/WAL history, bounded reads, and explicit durable
participant resume and periodic worker heartbeats/server idle cleanup are implemented.
Stable message IDs/room sequences and explicit saved-key retry deduplication are
implemented, including schema v1 migration. Bounded worker recovery resumes the
same durable identity, rejoins chosen groups, retries uncertain messages with
saved keys, and replays missing sequences while retaining GUI drafts/transcripts.
Verified TLS 1.3 and explicit executable transport modes are implemented.
The persistent CLI now supports core chat flows, live events, and worker recovery.
AI and manual acceptance remain pending.
Server/API and Windows controls for R2-R11 have automated evidence;
live demonstrations, code snapshots, and two-physical-computer acceptance
remain pending.

| Stage | Status | Verified evidence |
| --- | --- | --- |
| 0. Toolchain | Done | C++20 CMake project, vcpkg manifest, warnings, `debug`/`release`/`asan` presets, and GoogleTest target are present. |
| 1. Protocol and framing | Done | Generated Protobuf schema and standalone frame codec compile. Debug, Release, and ASAN builds each passed the CTest target `chat_tests`: 12 GoogleTest cases in two suites. |
| 2. Plain TCP proof | Done (local); LAN smoke pending | Separate `chat_server`/`chat_cli`, reusable `PingClient`, and 15 loopback integration cases. Debug, Release, and ASAN each passed both CTest targets (27 cases total). |
| 3. Session ownership | Done (automated) | Reusable `ClientSession`, one bounded ordered write queue, shared hub strand; eight real-socket ownership/ordering/overflow/cancellation tests. |
| 4. Login | Done (automated); manual checks pending | Server validates and uniquely reserves active names, binds identity to connections, and cleans up disconnects; 12 login integration cases. |
| 5. Direct rooms | Done (automated) | Pair-specific rooms, sender/recipient-only delivery, server identity, forged-metadata rejection, and three-client privacy tests. |
| 6. Groups | Done (automated) | Creator-only initial membership, explicit self-join, member-only delivery, bounded group creation, and disconnect cleanup tests. |
| 7. Presence and discovery | Done (automated) | Registered clients receive full user/group/member snapshots on login, create, join, and disconnect; headless API events verified. |
| 8. History | Done (automated) | SQLite/WAL schema, commit-before-ACK, durable participant tokens, persisted groups/admissions/messages, bounded authorized history API, and 13 restart/privacy/failure/response tests pass. |
| 9. Heartbeats | Done (automated) | Worker Ping every 10 seconds, server expiry after 30 seconds without a complete request, configurable timers, cleanup/cancellation, 14 socket/worker cases and one hidden GUI failure case pass in all three presets. |
| 10. IDs and ACK | Done (automated) | ACK/delivery/history metadata, per-room counters, sender-scoped retry keys, durable receipt recovery without redelivery, bounded ephemeral cache, v1 migration, and 18 ordering/retry/privacy/failure/client tests pass in all presets. |
| 11. Reconnect and sync | Done (automated) | Private sequence/directory APIs, fixed replay watermarks, bounded identity resume and chosen-group rejoin, saved-key retry, ordered replay/duplicate suppression, and GUI retention; 15 socket, 2 model, and 1 hidden-control cases pass in all presets. |
| 12. TLS | Done (automated); LAN/manual acceptance pending | Verified TLS 1.3, DNS/IP SAN and chain/validity checks, explicit loopback development, handshake deadlines/cancellation, encrypted privacy/recovery; 17 socket, 3 options, and 2 hidden-control cases pass in all presets. |
| 13. Client API/CLI polish | Done (automated); terminal acceptance pending | Typed headless API, worker-backed persistent CLI discovery/DM/create/join/group flows, bounded stdin/receipt draining, TLS replay, terminal failure, and unknown-enum admission; 9 unit and 11 socket/presentation cases plus real three-process smoke pass. |
| 14. Room GUI | Windows implementation; manual acceptance pending | Separate room views/drafts, presence/group/member controls, create/self-join/send, networking worker, and recovery retention. 13 model and 8 hidden native-control tests plus socket worker cases; visual/keyboard and full GUI walkthrough pending. |
| 15. Special features | Not started | No AI or other special feature selected or implemented. |
| 16. Demonstration and slides | Not started | No physical-computer demo or completed slide deck. |

## Engineering workflow tooling

On 2026-10-07, a repository-local [engineering workflow](workflow.md) was
added with 12 skills, four task playbooks, four Luna worker profiles, and scoped
verification commands. Correctness, requirement fit, stability, and
maintainability are mandatory; deeper security/performance work follows actual
risk. Refactoring is assessed after substantial changes, with broader debt
reviewed at milestones. The [setup record](workflow-setup.md) documents sources
and configuration boundaries. This tooling is not a graded project stage.

Native Codex CLI 0.153.4 discovery listed all 12 repository skills enabled
without errors; its config read confirmed the project layer and Luna/high
defaults with a three-child cap. Profile TOML passed structural validation.
This chat exercised the documented explicit-role fallback rather than native
named-profile selection. Two read-only Luna workers exercised feature routing
and reviewed the verification runner; identified skip/filter gaps were repaired
with regressions that failed before the fixes.

`python tools/verify.py full` passed on Windows x64 with 23 workflow-tool tests,
37 unit, 157 socket, and 8 hidden native-control cases in **each** of Debug,
Release, and ASAN, plus CLI/TLS process checks per preset, all 52 clangd checks,
and strict Doxygen/Sphinx builds. The final run deliberately inherited
nonmatching GoogleTest filters and sharding; acceptance children cleared these
overrides and still executed all 202 C++ cases. No source changes occurred
during that run. Its logs and summary are retained locally under
`.cache/verification/20261007T131521Z-full-d87dd971/`.

Existing product work was preserved. Manual GUI/native-console/editor checks,
physical LAN, other-OS builds, AI, and demonstration/slides remain pending.

## Stage 1 verification details

At the Stage 1 checkpoint, the unit suite checked big-endian length encoding, incremental header
and payload reads, several frames in one read, the maximum 1 MiB payload,
zero and oversized lengths, truncated frames at EOF, Protobuf round-trip, and
malformed Protobuf rejection. `chat_tests` contains 12 GoogleTest cases.
The local Debug, Release, and ASAN test logs each
report 12 passed cases. The ASAN preset uses `RelWithDebInfo` with sanitizer
instrumentation; on Windows its tests need an x64 Visual Studio developer
environment so the sanitizer runtime DLL is available.

These tests exercise bytes and serialization in process. They do **not** prove
TCP communication, privacy, authorization, persistence, or assignment R1-R11.
There is no cross-platform CI result yet; the verified builds were run in the
local Windows x64 Visual Studio environment.

## Stage 2 verification details

On 2026-09-30, configure/build/test succeeded with the `debug`, `release`, and
`asan` presets in the Windows x64 Visual Studio developer environment. Each
preset passed `chat_tests` (12 unit cases) and `chat_integration_tests` (15
socket cases), with no sanitizer reports. The new suite covers concurrent
persistent clients; nonce/request-ID matching and boundary nonce values;
split and coalesced writes; exact 1 MiB payloads; invalid lengths, serialized
data, versions, and payload types; truncated EOF; half-close; shutdown while
reads are pending; connection refusal; client timeout and explicit reconnect;
invalid/fragmented Pong responses; and connection-cap enforcement and reuse.

At the Stage 2 checkpoint, the server used one thread, up to 64 active connections, and one bounded
response batch per connection, pausing reads while its write drains. This is
the original Stage 2 design, subsequently replaced by Stage 3 sessions and
queues. At that checkpoint the client API was blocking and diagnostic only. No chat or privacy
claim follows from these Ping/Pong results. The early LAN smoke test remains
pending until a second physical computer is available.

The separate Debug executables also passed a local smoke test: a server bound
to an ephemeral loopback port, and two CLI processes printed matching Pong
nonces (`42` and the maximum uint64 value) with exit code zero. An invalid
CLI port returned exit code one. Ctrl+C ended the server process.

## Actionable task board

Owners are **TBD** until the team assigns them. Tasks below record verified status;
the stage table above remains the summary of verified status. Record observed
evidence in the stage summary or task row before changing a task to Done.
`INFRA-01A` improves portability and is non-blocking; it is not a graded
assignment stage.

| ID | Stage / dependency | Deliverable and required evidence | Owner | Status |
| --- | --- | --- | --- | --- |
| NET-02A | 2, after Stage 1 | Separate server and CLI entry points; integration test with two TCP clients. | TBD | Done: both programs build; two persistent clients exchange independent nonces in all three presets. |
| NET-02B | 2, after NET-02A | Framed Protobuf Ping/Pong; nonce, fragmented-read, failure, and shutdown tests. | TBD | Done: 15 socket cases pass in Debug, Release, and ASAN; LAN smoke pending. |
| NET-03A | 3, after Stage 2 | Safe `ClientSession` ownership and cancellation; disconnect-under-load test. | TBD | Done (automated): pending-read/write stop and peer reset drain handlers; weak ownership expires after cancellation. |
| NET-03B | 3, after NET-03A | One ordered bounded write queue per socket; concurrent-send ordering test. | TBD | Done (automated): four concurrent producers, FIFO writes, byte/frame admission bounds, and active-write accounting pass. |
| NET-04A | 4, after Stage 3 | Server-validated login, unique active names, and disconnect cleanup; duplicate and unauthenticated tests. | TBD | Done (automated): 12 real-socket login cases; interactive/physical demo pending. |
| NET-05A | 5, after Stage 4 | Pair-specific direct rooms and delivery; two-room separation test. | TBD | Done (automated): reverse direction reuses a room; other pairs and reconnected identities get separate rooms. |
| NET-05B | 5, after NET-05A | Direct privacy enforcement; three-client test asserts Charlie receives no payload. | TBD | Done (automated): sender and recipient receive identical text; third client stays quiet; forged metadata is denied. |
| NET-06A | 6, after Stage 5 | Group create and self-join operations; creator-only and explicit-join tests. | TBD | Done (automated): creator-only membership, voluntary join, duplicate/unknown join rejection, and distinct group IDs. |
| NET-06B | 6, after NET-06A | Member-only group delivery; three-client nonmember and forged-sender tests. | TBD | Done (automated): members receive text; nonmembers receive no payload and cannot send; forged metadata is denied. |
| NET-07A | 7, after Stage 6 | Connected-user and all-group/member snapshots; login/create/join/disconnect update tests. | TBD | Done (automated): snapshots converge after login/create/join/disconnect and slow-reader removal; rejected clients receive none. |
| DATA-08A | 8, after Stage 7 | SQLite/WAL room history; restart, authorization, and failure tests. | TBD | Done (automated): 13 tests cover restart/resume, privacy, pagination, commits, failures, and response validation. |
| NET-09A | 9, after Stage 8 | Heartbeat timeout and stale-session cleanup; healthy and unresponsive client tests. | TBD | Done (automated): correlated periodic Pings, request-only activity, stale cleanup/slot reuse, and read/write/timer cancellation pass; GUI status checked with hidden controls. |
| NET-10A | 10, after Stage 9 | Message IDs, room sequences, ACK correlation, and dedup policy; retry/order tests. | TBD | Done (automated): matching metadata, contiguous room order, saved-key duplicates/conflicts, unread-ACK recovery, restart/auth/privacy, migration/rollback, and bounded ephemeral eviction verified. |
| NET-11A | 11, after Stage 10 | Reconnect and catch-up protocol; gap and duplicate-display tests. | TBD | Done (automated): forced disconnect/lost ACK, multi-page catch-up, no gap/duplicate display, chosen-group/privacy, bounded retry/cancellation, and hidden GUI retention verified. |
| SEC-12A | 12, after Stage 11 | TLS 1.3 with chain and hostname validation; valid/invalid certificate tests. | TBD | Done (automated): trust, DNS/IP SAN, validity/signature, version, deadlines/cancellation, privacy, and recovery verified; certificate tooling and executable smoke pass. |
| API-13A | 13, after Stage 12 | Stable headless client API and persistent/diagnostic CLI; API event and reconnect tests. | TBD | Done (automated): core CLI flows/privacy, typed events, encrypted recovery, bounded input/exit, and real executable smoke pass; native terminal walkthrough pending. |
| UI-14A | 14, after Stage 7 and minimum client API | User/group navigation and separate direct/group message views with text send controls; manual R5-R6 walkthrough. | TBD | Windows controls/model implemented; automated tests pass; manual walkthrough pending. |
| UI-14B | 14, after UI-14A | Three-client GUI acceptance for R3-R11, backed by wire-level privacy tests. | TBD | Automated hidden-window plus two headless-client flows pass; three visible GUI clients and physical acceptance pending. |
| FEAT-15A | 15, after Part 2 | Select and implement an AI special feature; model reference, test, live demo, and code snapshot. | TBD | Not started |
| FEAT-15B | 15, after Part 2 | Select up to three other special features, then implement and verify each claimed one with a live demo and code snapshot. | TBD | Not started |
| DEMO-16A | 16, after UI-14B | Server plus clients on two physical computers; record machines, network, version, and R1-R11 results. | TBD | Not started |
| DEMO-16B | 16, after DEMO-16A | Final implementation-explanation slides with required code snapshots and feature evidence. | TBD | Not started |
| INFRA-01A | Non-blocking, after Stage 1 | Verify macOS and Linux configure/build/test, and add CI when a remote is available; record actual platform results. | TBD | Not started |

## Next checkpoint: manual acceptance and special-feature selection

The Windows room GUI is implemented and automated checks exercise its actual
controls. Stage 14 remains open for manual acceptance. Stage 8 history is verified;
Stage 9 heartbeats and Stage 10 message IDs/room sequences and retry policy are
verified. Stage 11 automatic reconnect and sequence catch-up are also verified.
Stage 12 TLS and Stage 13 persistent CLI core flows are verified. The next
development work is selecting an AI special feature and other demonstrable
features for Stage 15. Stage 14 visual/keyboard and physical-computer acceptance
stay pending and do not block independent implementation work.

## Stages 3-4 automated evidence and pending checks

On 2026-09-30, Debug, Release, and ASAN passed both CTest targets: 12 unit
cases and 35 socket integration cases (47 total). Eight session cases exercise
four concurrent producers with two context workers, exact FIFO order for
sequential sends, byte and frame limits before executor dispatch, active-write
accounting, oversized output rejection, stop during a large pending write/read,
peer reset under load, and a single close callback with released ownership.
The 12 login cases cover the headless API, rejection/retry, a simultaneous
duplicate-name race with two server workers, invalid/boundary names,
case sensitivity, rename denial, cleanup after EOF and protocol failure,
unauthenticated and rejected-client denial despite forged sender metadata,
unsupported registered chat at that checkpoint with a bounded no-delivery check, and malformed
accepted-login identity from a fake server.

No new manual checks were performed in this checkpoint, as requested.

## Stages 5-7 automated evidence

On 2026-09-30, Debug, Release, and ASAN configure/build/test passed both CTest
targets: 12 unit cases and 61 socket integration cases (73 total), with no
sanitizer reports. The 26 new chat cases verify direct room reuse/separation,
sender echo and recipient delivery, self-DM, no third-party delivery, forged
metadata rejection, text/name boundaries, creator-only membership, voluntary
join, nonmember send/delivery denial, and all-user group discovery.

Snapshot checks cover login/create/join/disconnect, reclaimed usernames,
connection-scoped membership, empty groups, and bounded group creation.
Headless API tests exercise operations, typed events, correlated responses
with partial/coalesced event frames, soft event timeouts preserving partial
reads, and event count/byte limits. A slow group reader is disconnected when
its write queue fills; healthy clients continue and receive cleaned-up
presence/membership snapshots. The old unsupported-chat login test now checks
forged metadata rejection because valid registered chat is implemented.

No GUI, interactive CLI, physical-network, or cross-platform manual checks
were performed. Operation ACKs report in-memory acceptance/routing attempts;
they do not claim durable storage, recipient receipt, or retry deduplication.

## Stage 14 Windows GUI automated evidence

On 2026-09-30, Debug, Release, and ASAN configure/build/test passed all three
CTest targets: 23 unit, 69 socket integration, and 4 hidden native-control
GoogleTest cases (96 total), with no sanitizer reports. The 23 new cases
exercise the portable room model and networking worker plus actual Windows
controls. Coverage includes separate direct/group views and drafts, server
echo without optimistic duplication, Unicode text, creator-only groups,
explicit GUI self-join, oversized Unicode draft retention during presence
updates, duplicate-login retry, disconnect state, and responsive window-message
processing during a silent-server login. The third/nonmember socket is checked
for unintended direct/group deliveries. View/line/byte retention, worker queue
count/byte overflow, bounded commands, and in-flight cancellation also pass.

The first adapter is Windows-only; the worker/model remain portable C++.
No manual GUI, DPI/layout, keyboard/focus, three visible GUI-client, or physical
LAN checks were performed. Stage 14 remains pending manual acceptance. Local
transcripts are display buffers, not Stage 8 durable history; reconnect,
heartbeats, TLS, and AI remain later work. All 30 owned C++ files pass clangd
without enabled compiler/tidy diagnostics. The source/API catalogue includes
the new code and scenario comments; see the [GUI guide](setup/gui.md). Strict
Doxygen/Sphinx builds pass with the new API references and source pages.
The native transcript test also receives text containing an embedded NUL and
verifies that the remainder remains visible, using `␀` for the NUL byte.

## Stage 8 history automated evidence

On 2026-09-30, Debug, Release, and ASAN passed all three CTest targets:
23 unit, 82 socket integration, and 4 native-control cases (109 total), with
no sanitizer reports. Thirteen new history cases cover durable direct rooms
across restart, original-token/name resume, fresh same-name denial, offline
participant reads, group discovery/rejoin and full-history policy, committed
state visible to another database reader, hashed-token storage, 100-record
maximum pages, default/empty pages, Unicode/NUL text, room isolation, busy and
insert failures, failed login/join admission, read failure, invalid/future-schema
startup, legacy ephemeral mode, malformed pages, and partial/interleaved events.

The server executable defaults to `data/chat.sqlite3`. The embedded API can
still run ephemerally. Active membership is not restored from stored admissions.
History and token resume are headless API operations; the GUI keeps its local
transcripts and performs fresh logins. Automatic replay/recovery, GUI token
retention/history loading, live wire sequences/IDs, and deduplication are pending.
No new manual or physical-network checks were performed. See [history](history.md).
The four native-control cases also pass against a storage-enabled `:memory:`
server. A separate server/CLI process smoke passed twice against the same file
database, reopening after process termination and retaining committed identity
rows. All 33 C++ files pass clangd; strict Doxygen/Sphinx builds and local HTML
links/API references pass. Real power-loss durability has not been tested.

## Stage 9 heartbeat automated evidence

On 2026-09-30, Debug, Release, and ASAN passed all three CTest targets:
23 unit, 96 socket integration, and 5 hidden native-control cases (124 total),
with no sanitizer reports. Fourteen socket/worker cases verify quiet registered
and unregistered expiry, name/member cleanup and slot reuse, partial-frame
trickling, complete Ping and chat request activity, outgoing-only delivery,
pending read/write/timer cancellation and released ownership, periodic
correlation, missing/incorrect Pongs, stop during an active heartbeat, sustained
command/snapshot traffic, disabled timers, and invalid budgets. The fifth native
control case verifies heartbeat error status, responsive window messages, and
usable reconnect controls.

The worker defaults to a 10-second Ping cadence with its three-second operation
deadline; the server defaults to 30 seconds without a complete inbound request.
Partial bytes and outbound traffic do not extend activity. Expiry uses the
existing close/registry cleanup and ordered-write paths. Timer waits retain
session ownership and are cancelled by stop; no second writer is introduced.
The blocking headless API still requires its owner to schedule Pings. Server
and worker timer budgets are configurable in 0..24 hours; zero disables each.
See [heartbeats](heartbeats.md) for configuration and executor timing limits.

All 34 owned C++ files pass clangd without compiler or enabled tidy diagnostics.
Strict Doxygen/Sphinx builds pass; local targets across 76 HTML pages and the new
timer fields in the API reference are verified. File/scenario comments and the
124-case count match the source and all three preset logs. A server/CLI process
smoke passes with the default, disabled, and 120ms idle budgets, and five invalid
CLI timeout values are rejected. Windows expiry may report EOF or reset; both
are valid closure outcomes.

No manual GUI, editor/browser walkthrough, physical-network interruption, or
cross-platform acceptance was performed. Automatic reconnect, GUI history
recovery, IDs/sequences/deduplication, and TLS remain pending.

## Stage 10 message reliability automated evidence

On 2026-09-30, Debug, Release, and ASAN passed all three CTest targets:
23 unit, 114 socket integration, and 5 hidden native-control cases (142 total),
with no sanitizer reports. Eighteen new cases cover matching ACK/echo/recipient/
history metadata, independent direct/group sequences, two concurrent group
senders, exact duplicate receipts without redelivery/storage, changed-content/
target/type conflicts, sender-key isolation, restart/resume, fresh same-name
denial of old identity/history, pinned original recipients, group rejoin,
unread ACK recovery, failed direct/group counter updates, legacy requests,
bounded ephemeral eviction, key boundaries, successful/failed v1 migration,
and malformed metadata/correlation. Three additional scenarios in the existing
history validation case reject missing IDs and invalid/repeated room sequences.

Schema v2 commits message/key/counter state together before ACK or routing.
Existing v1 cursor IDs, content, participant pairs, and admissions remain intact;
room sequences are backfilled and legacy timestamps are zero. Stored duplicate
keys are scoped to the durable sender and live as long as the row. Accepted
duplicates return the same ID/sequence under the current request correlation,
with no echo or fanout. Ephemeral connections retain the last 256 accepted keyed
receipts; eviction/disconnect/restart ends that protection. Rejected or rolled
back attempts reserve no key or sequence. Headless explicit-key operations can
recover receipts after identity resume; the GUI still uses fresh-key sends and
does not retain attempts across failures. See [message reliability](reliability.md).

All 37 owned C++ files pass clangd without compiler or enabled tidy diagnostics.
Random-key crypto implementation resides in the transport library, leaving its
shared header independent of OpenSSL include paths. Strict Doxygen/Sphinx builds
pass, and local targets across 80 HTML pages plus the new receipt/key/sequence
API members are verified. Source file/scenario comments and the 142-case count
match the three preset logs. Server/CLI process smoke still passes with default,
disabled, and 120ms idle budgets, plus invalid timeout rejection.

No manual GUI/editor/browser walkthrough or physical-network acceptance was
performed. Automatic reconnect, persistent GUI retry queues, sequence catch-up,
duplicate-free replay, TLS, and AI remain pending. ACKs still do not establish
recipient receipt or exactly-once delivery.

## Stage 11 recovery automated evidence

On 2026-09-30, Debug, Release, and ASAN passed all three CTest targets:
25 unit, 129 socket integration, and 6 hidden native-control cases (160 total),
with no sanitizer reports. Fifteen new socket cases cover fixed-watermark pages,
private directory pagination and offline original peers, fresh namesake denial,
current group membership, deleted-sequence gaps, a lost committed ACK, 120 missed
group messages across pages, live gap repair, durable server restart, bounded
retries/room tracking, immediate backoff cancellation, invalidated capabilities,
uncertain group creation, wire bounds, ephemeral denial, and malformed pages.
Two model cases retain draft/cursor state after eviction and verify original-peer
room binding. A hidden-control case preserves the selected group, unsent draft,
two-line transcript, reconnect status, and responsive send/disconnect controls.

The worker keeps its resume capability and command keys in memory, distinguishes
transport errors from protocol failures, and never falls back to a fresh identity
on resume denial. Recovery self-rejoins only user-chosen groups, discovers only
the durable principal's direct rooms, applies contiguous sequences through a fixed
watermark, and suppresses already admitted/displayed sequences. Message retries
return one completion with the original receipt; group creation is not blindly
retried. Cursors advance after update admission and survive local transcript
eviction. All paths retain the existing ordered socket owner and bounded queues.
Queue overflow, unauthorized/gapped replay, and exhausted budgets end explicitly.
Manual reconnect remains a fresh login; client process restart saves no tokens,
attempts, or room cursors. See [recovery](recovery.md) for the complete policy.

All 39 owned C++ files pass clangd without compiler or enabled tidy diagnostics.
Strict Doxygen/Sphinx builds pass; generated guide/API links and the 160-case
source/log counts are checked. Source/scenario comments and the generated source
catalogue cover both new recovery test files. No manual GUI/editor/browser
walkthrough or physical-network acceptance was performed. TLS, CLI chat polish,
AI, and the assignment demonstration remain pending.

## Stage 12 TLS verification

On 2026-09-30, Debug, Release, and ASAN each passed all three CTest targets:
28 unit, 146 socket integration, and 8 hidden native-control cases (182 total),
with no sanitizer reports. Seventeen new socket cases verify TLS 1.3, explicit
CA trust, DNS/IP SAN matching, certificate validity/signature and key failures,
TLS 1.2/plaintext denial, encrypted split/coalesced/exact-1-MiB frames,
three-client direct/group privacy, stored retry deduplication and sync,
handshake deadlines, pending-operation cancellation and ownership release,
loopback-only plaintext development, and encrypted worker recovery. Recovery
revalidates the server and ends on an invalid replacement certificate without
falling back. Three application-options cases and two hidden-control cases
cover explicit modes, verified GUI login/send, and responsive name rejection.

Server, CLI, and Windows GUI executables now require `--tls` configuration or
explicit `--plaintext-dev`; only loopback endpoints permit plaintext. TLS keeps
the existing framed protocol, authoritative hub, strand, and bounded ordered
writer. Server handshakes have an independent deadline; the client connect
deadline includes DNS, TCP, and negotiation. Stop cancels socket/timer work;
close is abortive rather than waiting for a graceful TLS shutdown.

`python tools/dev_tls.py` generated local development certificates under ignored
`certs/development/`; credentials are never overwritten or committed.
`python tools/check_tls_apps.py` passed real server/CLI startup, independent
Python TLS 1.3 negotiation, login/Ping, wrong-name rejection, missing/mixed mode
and credential rejection, and explicit loopback-development checks. The
[TLS guide](tls.md) documents certificates, executable commands, SANs, API
settings, trust distribution, deadlines, and limits.

All 46 owned C++ files pass clangd without compiler or enabled tidy warnings.
Strict Doxygen/Sphinx builds pass; automated checks confirm TLS API declarations,
valid local targets across 93 generated HTML pages, file explanations, and the
182-case source/log counts in all three presets. Manual visible-GUI/editor/browser
walkthroughs, LAN certificate distribution, and physical-network acceptance
remain pending. Stage 13 interactive chat CLI polish is the next implementation
checkpoint; AI and assignment demonstration work remain open.

## Stage 13 client API and CLI verification

On 2026-09-30, Debug, Release, and ASAN each passed all three CTest targets:
37 unit, 157 socket integration, and 8 hidden native-control cases (202 total),
with no sanitizer reports. Nine new unit cases verify literal command syntax,
missing/extra arguments, exact byte limits, control-character denial, remote
terminal escaping, fragmented/coalesced CRLF input, UTF-8 backspace, oversized
line discard/resynchronization, EOF, and input chunk bounds. Eleven new
integration cases verify TLS CLI core commands/events, three-client direct/group
privacy, creator-only discovery, member views, nonmember denial, explicit join,
login/certificate failure, quit receipt draining, malformed input recovery,
unknown worker enum rejection, encrypted replay without duplicate display,
quit during backoff, and a silent operation exceeding its exit drain deadline.

The persistent `chat` form uses the GUI-independent `ChatWorker`; the one-shot
login/Ping form remains available. The presentation thread polls stdin and
worker updates while the worker alone owns networking, heartbeats, and durable
recovery. Input chunks, partial lines, snapshots, pending IDs, commands, and
updates are bounded. `/quit` and EOF stop input and drain admitted receipts for
at most five seconds, then request cancellation under the active operation's
deadline. Output escapes remote ASCII controls/delimiters; capabilities and keys
are never printed or saved. The server still authorizes identity and membership.

`python tools/check_cli_apps.py` passed in all three presets, driving three real
CLI processes per transport mode through Unicode direct messages, group
creation/discovery, nonmember rejection, explicit join/member delivery, quiet
nonrecipients, idle open stdin, unterminated EOF command receipt, quit, and
initial certificate/login/connection failure without input-thread hangs.
`python tools/check_tls_apps.py` also passed again for Debug and ASAN.
See the [CLI guide](cli.md) for executable syntax, commands, receipt semantics,
exit codes, ownership, and limits.

All 52 owned C++ files pass clangd without compiler or enabled tidy diagnostics;
the integration file was checked again after its final deadline test was added.
Strict Doxygen/Sphinx builds pass; automated checks confirm CLI API declarations,
valid local targets across 101 generated HTML pages, file explanations, and
the 202-case source/log counts in every preset. Native console appearance,
keyboard editing/Ctrl+C, visible GUI/editor/browser walkthroughs, physical LAN,
and macOS/Linux builds remain pending. Stage 14 remains open for manual
acceptance; Stage 15 feature selection/implementation is still open.

## Editor and documentation tooling

On 2026-09-30, shared clangd configuration and workspace settings/tasks were
added for Zed and VS Code. The previous missing-header failures were reproduced
without the compilation database; `.clangd` now selects `build/debug`.
At that tooling checkpoint, `python tools/check_clangd.py` passed all 20 handwritten C++ sources/headers
with no compiler or enabled clang-tidy warnings (clangd 21.1.8, Windows x64).
Refactoring-location self-tests are excluded from this parsing check.

Doxygen 1.18.0 and the VS Code Doxygen comment helper were installed locally;
Sphinx 8.2.3, Breathe 4.36.0, MyST Parser 4.0.1, and Mermaid 1.2.3 are isolated
in `.venv-docs`. The setup command is repeatable. Doxygen and Sphinx both build
with documentation warnings treated as failures. All C++ files have short
file explanations; all 73 test cases at that checkpoint had scenario comments. Public headers
document the API/ownership contracts, and the source catalogue includes
handwritten code, tests, scripts, schema, and shared configuration.

Automated checks confirmed expected API declarations, no missing local HTML
link targets, and HTTP 200 from the loopback preview server. Debug, Release,
and ASAN builds/tests were rerun and each passed all 73 cases. Generated docs,
compiler databases, caches, and the Python environment remain ignored.
Editor navigation/formatting and rendered-page walkthroughs remain pending;
no manual or physical-network acceptance was performed. Follow the
[tooling guide](setup/tooling.md) to use the editor tasks and maintain comments.

| Check | Status / reason |
| --- | --- |
| Interactive CLI login walkthrough | Pending; not manually exercised. |
| Two-physical-computer LAN smoke | Pending; requires another machine and network setup. |
| Live R3 uniqueness demonstration and code snapshot | Pending; server behavior has automated evidence only. |
| GUI visual/keyboard walkthrough, R1-R11 physical demo, slides | Pending; native controls have automated evidence, but manual and physical acceptance remain necessary. |
| macOS/Linux builds | Pending; only Windows x64 was available for these tests. |
