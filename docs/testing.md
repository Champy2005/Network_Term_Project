# Test strategy and demonstration

Tests should prove behavior at the layer where a bug can occur. Byte-frame
unit tests are useful, but cannot prove socket communication or the
assignment's privacy rules. The [progress tracker](progress.md) records which
tests have actually run; the checks below include planned coverage.

## Current automated tests

The [engineering workflow](workflow.md) defines task-specific acceptance and
optional checks. `python tools/verify.py full` aggregates the existing preset,
executable smoke, clangd, and documentation gates with retained evidence.
Workflow-tool tests separately cover malformed artifacts/config, failure and
empty-test rejection, partial results, interruption, and owned-process cleanup.
These checks do not replace new feature tests or manual/physical acceptance.

On Windows the suite has three CTest targets: `chat_tests` with 37 unit cases,
`chat_integration_tests` with 157 real-socket cases, and `chat_gui_tests` with
8 hidden native-control cases (202 total). The 12 original unit tests
cover big-endian frame encoding, fragmented input, multiple
frames per feed, size bounds, EOF truncation, Protobuf round-trip, and malformed
serialized payload rejection. Debug, Release, and ASAN each passed locally on
Windows x64. The TCP integration suite covers two persistent clients, nonce
and request-ID correlation, byte-at-a-time writes in both directions,
coalesced request ordering, an exact 1 MiB frame, invalid lengths/Protobuf/
versions/payloads, unsupported chat requests, truncated EOF, half-close,
shutdown with pending reads, refusal and explicit reconnect, a silent-server
deadline, invalid Pong responses, and the 64-connection limit with slot reuse.
Eight session cases add concurrent producers, FIFO writes, byte/frame queue
bounds before dispatch (including the active write), cancellation during reads
and large writes, peer reset, and ownership release. Twelve login cases add
username validation and boundaries, duplicate/retry and simultaneous claims,
case sensitivity, rename denial, disconnect/protocol-failure cleanup,
unauthenticated and rejected-client denial, a bounded no-delivery check for
forged chat metadata, and headless login response validation. Session and login
tests run with two networking workers to exercise the shared strand.
Twenty-six chat cases cover three-client direct privacy, reverse-pair room
reuse and pair separation, self-message echo, absent recipients, every forged
metadata field, exact text/name bounds, creator-only groups, self-join,
member-only delivery, unknown/duplicate joins, duplicate group names,
all-client and late-login snapshots, disconnect/reclaimed-name cleanup,
empty-group reuse, split/coalesced requests, the headless operation/event API,
the 128-group cap, soft receive deadlines and partial-frame retention,
partial events following replies, both client event queue bounds, and slow
member overflow with consistent presence/membership cleanup. Privacy checks
wait after successful delivery/ACK and inspect the nonrecipient's socket.
Tests use isolated ephemeral loopback ports and a bounded receive helper; CTest also
caps the integration executable at 45 seconds. A split write may still be
coalesced by TCP; deterministic partial-read coverage remains in the codec
unit tests. Physical-network tests have not been run.

After configuring and building a preset, run:

```text
ctest --preset debug
ctest --preset release
ctest --preset asan
```

The `asan` preset is `RelWithDebInfo` with AddressSanitizer, not an ordinary
Debug build. On Windows, run it from an x64 Visual Studio developer environment
so the sanitizer runtime DLL is on the search path. See the root README and
the [OS setup guides](README.md) for prerequisites.

No manual acceptance was run for Stages 3-7. The CLI login walkthrough,
LAN smoke test, live username-uniqueness demonstration, GUI acceptance, and
physical-computer demonstration remain pending in [progress](progress.md).

The 13 room-model cases check per-room routing, independent drafts, matching-ACK
clearing without duplicate lines, membership snapshots, disconnect/new-login
retention, reclaimed peers, invalid state, and line/byte/view bounds. Group
views remain usable after many direct-room generations. Eight worker cases
cover three-client direct/group privacy, nonmember rejection, duplicate-name
retry, count/byte update overflow, command admission and count bounds, timeout,
and cancellation while an operation waits on a silent server.

Eight Windows-only tests create actual hidden native controls and drive their
commands against loopback sockets. They exercise user/group/member lists,
Unicode direct text, separate group/direct transcripts and drafts, create and
explicit GUI join, oversized Unicode draft retention during snapshots, rejected
login/retry, disconnection, heartbeat failure status/reconnect controls, and
window-message processing during a slow login or heartbeat exchange, and selected
group/draft/transcript retention during automatic recovery, verified TLS login/send,
and certificate failure without blocking controls or falling back to plaintext.
The third nonmember's socket is checked for unintended delivery. These tests
do not inspect visual layout, DPI, focus/keyboard behavior, or three visible GUI
clients. Those checks and the physical LAN run remain pending. Native-control
CTest timeout is 30 seconds. Non-Windows builds omit this target (194 portable
cases); their build/test results are still unverified.

Fourteen heartbeat cases check registered/unregistered expiry, name/member and
slot cleanup, partial-frame trickling, complete Ping/chat activity, outbound-only
traffic, cancellation with pending read/write/timer work, periodic correlated
Pings, missing/incorrect Pongs, stop during heartbeat, sustained commands/events,
disabled timers, and invalid budgets. Short configured deadlines keep these
real-socket checks bounded; default timer values are documented in
[heartbeats](heartbeats.md). Physical network interruptions remain untested.

Seventeen Stage 12 socket cases verify TLS 1.3 using isolated generated CAs/keys,
DNS/IP SANs, wrong identity, untrusted roots, expired/future/missing-SAN/bad-signature
certificates, mismatched/missing keys, TLS 1.2/plaintext rejection, encrypted
fragmented/coalesced/max-size frames, three-client DM/group privacy, saved-key
retry/sync, handshake deadlines/ownership/cancellation, loopback-only plaintext,
and encrypted worker recovery including an invalid replacement certificate.
Three unit cases check explicit application modes and argument rejection; two
hidden-control cases cover verified GUI login/send and name rejection.
`python tools/check_tls_apps.py` verifies real executable startup flags, independent
Python TLS 1.3 negotiation, login/Ping, wrong-name rejection, and explicit loopback
development without visible windows. See [TLS](tls.md) for configuration.

Nine CLI unit cases cover literal syntax, required arguments, exact byte limits,
control-character denial, terminal escaping, fragmented/coalesced CRLF input,
UTF-8 backspace, overflow discard/resynchronization, EOF, and chunk bounds.
Eleven integration cases cover real TLS DM/group commands/events, three-client
privacy, creator-only discovery, nonmember rejection, explicit join, initial
login/certificate failure, quit receipt draining, malformed input recovery,
unknown worker enums, encrypted sequence replay, quit during backoff, and a silent
operation exceeding the exit drain deadline. `python tools/check_cli_apps.py`
drives three actual CLI processes in each transport mode, including Unicode,
idle open stdin, EOF without a final newline, later input after quit, and failure
without input-thread hangs. Native terminal editing and Ctrl+C remain pending;
see [CLI](cli.md) for commands and limits.

## Editor and documentation checks

Fifteen Stage 11 socket cases cover stable replay watermarks, contiguous sequence
pages, private directory pagination, offline-peer access, fresh namesake and
nonmember denial, current membership after resume, deleted-row gaps, lost committed
ACK recovery, 120 missed group messages, live gap repair, file-backed server
restart, retry exhaustion, stop-aware backoff, invalid capabilities, bounded room
tracking, uncertain group creation, request limits, ephemeral denial, and malformed
sync/directory pages. A real TCP proxy interrupts or selectively drops frames.
Two model cases verify cursor/draft retention, replay suppression after eviction,
and original-peer room restoration. One hidden-control case verifies temporary
offline status, responsive controls, selected group/draft retention, and two-line
catch-up without duplicate display. The [recovery guide](recovery.md) defines the
policy and limits; visible GUI and physical-network interruption checks remain
pending.

Eighteen Stage 10 cases cover ACK/echo/delivery/history metadata, independent room
sequences, concurrent group senders, direct/group duplicates without fanout,
content/target/type conflicts, sender-key isolation, restart/resume, same-name
replacement, unread ACK recovery, group rejoin authorization, failed direct/group
counter transactions, legacy request correlation, bounded ephemeral cache
eviction, key validation, successful/failed v1 migration, and malformed metadata.
History response scenarios also reject absent IDs and nonincreasing sequences.
The [reliability guide](reliability.md) states what ACKs and retries guarantee.

Thirteen history cases add file-backed restart/resume, fresh same-name denial,
group rejoin/read policy, commit-before-ACK, pagination and room separation,
Unicode/NUL text, lock/insert/read failures, failed login/join admission,
invalid files/future schemas, ephemeral compatibility, malformed pages, and
coalesced responses with snapshots/partial events. Storage failures are checked
for no ACK/delivery and no partial rooms or granted memberships. Isolated
databases are created under the ignored build subtree and removed after shutdown.
Native-control flows use a storage-enabled in-memory SQLite server. A server/CLI
subprocess smoke also verifies file startup, login/Ping, and reopening after
process termination; it does not simulate hardware power loss.

After building Debug, `python tools/check_clangd.py` checks all 52 handwritten
C++ sources/headers with the same database/configuration as Zed and VS Code.
It treats printed compiler/clang-tidy warnings as failures too, stores logs
under `build/tooling/clangd`, and skips unrelated refactoring-location tests.
Missing databases or generated Protobuf headers produce a setup error.
On non-Windows platforms it skips the four native GUI files.

Run `python tools/docs.py setup` once, then `python tools/docs.py build`.
Doxygen comment errors and Sphinx warnings fail this build. The generated
site combines Markdown guides, C++ API declarations, and a source catalogue
that includes tests and other handwritten code/configuration. The build and
preview-server HTTP response have been verified locally. Editor navigation,
formatting, and browser rendering remain pending manual checks. See the
[tooling guide](setup/tooling.md) for dependencies and commands.

## Planned automated coverage

| Layer | Essential cases | Earliest stage |
| --- | --- | --- |
| Framing and Protobuf | Partial header/payload, coalesced frames, zero/oversized lengths, EOF truncation, malformed Protobuf, unsupported version and missing payload validation. | Frame/parser cases in Stage 1; Ping semantics in Stage 2 |
| TCP transport | Server accepts several connections; matching Ping/Pong nonces; intentionally split writes; connection refusal and clean EOF. | 2, implemented |
| Session safety | Multiple queued sends preserve order; disconnect during read/write; bounded pending queue; no operation uses a destroyed session. | 3, implemented |
| Login | Unique active usernames, invalid/empty names, rejected duplicate, unregistered-operation denial, release on disconnect. | 4, implemented; live demo pending |
| Direct messaging | Correct pair room, sender/recipient delivery, unknown recipient failure, forged-sender rejection, no payload to third client. | 5, implemented; manual GUI/demo pending |
| Groups | Creator is sole initial member, no creator-added member path, explicit self-join, all-group/member discovery, nonmember send denied, no payload to nonmember. | 6-7, implemented; manual GUI/demo pending |
| Presence | User snapshot includes self; joining and disconnecting refresh all clients; group snapshots reflect create/join. | 7, implemented; manual GUI/demo pending |
| Persistence | Authorized history read, server restart, ordering, nonmember history denial, SQLite failure behavior. | 8, implemented through headless API |
| Heartbeat | Stale-session cleanup, complete-request activity, correlated periodic Ping/Pong, and timer cancellation. | 9, implemented |
| Message reliability | Stable IDs, room sequences, ACK correlation, saved-key duplicates/conflicts, migration and commit failure. | 10, implemented |
| Recovery | Automatic reconnect, sequence catch-up, no gaps or duplicate display. | 11, implemented |
| TLS | Trusted server certificate works; untrusted CA, wrong hostname, and invalid/expired certificate fail. | 12, implemented; LAN acceptance pending |
| Client API/CLI | Plain typed networking API, persistent commands/events, bounded input/quit, receipt status, and recovery. | 13, automated core flows implemented; interactive acceptance pending |
| Client UI | Separate room histories, sent/incoming rendering, text input and send control, responsive UI while network is idle or slow. | 14, Windows automated controls implemented; visual/keyboard/manual flows pending |

For privacy tests, run **three clients** and inspect protocol events received
by the third/nonmember client. A hidden UI message is insufficient evidence if
the server still sent the bytes. Negative assertions need a bounded wait after
the recipient's successful delivery or ACK; do not treat an immediate empty
receive buffer as conclusive.

Each new integration test should start an isolated server on an ephemeral
localhost port, close its sockets, and avoid timing assumptions where an event
or barrier can be used. Localhost tests establish software behavior but do not
replace the physical-computer requirement.

## Early LAN smoke test after Stage 2

As soon as the CLI can exchange framed Ping/Pong, run its server on computer A
and its client on computer B using A's LAN IP address. Record the two machines,
network, address/port, executable revision, and observed nonce match.
Use verified TLS and a SAN matching the LAN address; [TLS setup](tls.md) documents
certificate generation and distributing the public CA. Plaintext development
permits only loopback connections. Confirm
that the server firewall permits the selected TCP port. If this fails, fix
reachability before adding chat features. Ping/Pong proves the socket path
across machines; it does not yet prove the assignment's chat requirements.

## Manual two-computer acceptance run

This run is for the final chat application; Ping/Pong is only an early smoke test.
Prepare two physical computers on the same reachable network. Run the server
on either one or a third machine. Run Alice's client on computer A and Bob's
client on computer B; run a third process, Charlie, on either computer. Record
which executable version, server address and port, and computers were used.

1. Show the architecture and explain why the application uses Protobuf,
   length framing, TCP, IP, and the local Wi-Fi/Ethernet link. Show the separate
   client and server entry points and socket send/receive path.
2. Connect Alice, Bob, and Charlie. Each should see all three connected names,
   including their own. Try a duplicate username and show rejection.
3. Open a direct room for Alice and Bob. Send a message and show it in their
   room views. Verify Charlie's protocol events contain no delivery and his
   UI receives nothing. Open another direct room to show separate histories.
4. Alice creates a group. Show all clients the new group and its member list
   containing only Alice. Bob explicitly joins it; Charlie remains a
   nonmember. Show the updated member list on all clients.
5. Alice sends group text. Alice and Bob see it in the group room; Charlie
   receives no group payload. Show the room's message display and text
   input/send control. Attempt a nonmember send and show server rejection.
6. Disconnect Bob. Verify remaining clients receive a connected-user list
   without Bob. Demonstrate history, reconnect, and TLS only if those stages
   have been implemented and tested.
7. Demonstrate each claimed special feature, including the AI feature, after
   the Part 1 and Part 2 fundamentals are complete.

The [slide template](assignment/implementation-explanation-template.pdf)
expects architecture/protocol explanation, a socket-code snapshot for R2,
mandatory code snapshots for R3-R11, and snapshots for special features. If
generative AI was used, keep the corresponding prompt record. Build slides
from the final code so the snapshot line numbers and behavior agree with the
demo.
