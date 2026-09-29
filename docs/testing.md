# Test strategy and demonstration

Tests should prove behavior at the layer where a bug can occur. Byte-frame
unit tests are useful, but cannot prove socket communication or the
assignment's privacy rules. The [progress tracker](progress.md) records which
tests have actually run; the checks below include planned coverage.

## Current automated tests

Stage 1 has one CTest target, `chat_tests`, containing 12 GoogleTest cases in
two suites. They cover big-endian frame encoding, fragmented input, multiple
frames per feed, size bounds, EOF truncation, Protobuf round-trip, and malformed
serialized payload rejection. Debug, Release, and ASAN each passed locally on
Windows x64. There are no integration tests or physical-network tests yet.

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

## Planned automated coverage

| Layer | Essential cases | Earliest stage |
| --- | --- | --- |
| Framing and Protobuf | Partial header/payload, coalesced frames, zero/oversized lengths, EOF truncation, malformed Protobuf, unsupported version and missing payload validation. | Frame/parser cases present in Stage 1; semantic validation later |
| TCP transport | Server accepts several connections; matching Ping/Pong nonces; intentionally split writes; connection refusal and clean EOF. | 2 |
| Session safety | Multiple queued sends preserve order; disconnect during read/write; bounded pending queue; no operation uses a destroyed session. | 3 |
| Login | Unique active usernames, invalid/empty names, rejected duplicate, unregistered-operation denial, release on disconnect. | 4 |
| Direct messaging | Correct pair room, sender/recipient delivery, unknown recipient failure, forged-sender rejection, no payload to third client. | 5 |
| Groups | Creator is sole initial member, no creator-added member path, explicit self-join, all-group/member discovery, nonmember send denied, no payload to nonmember. | 6-7 |
| Presence | User snapshot includes self; joining and disconnecting refresh all clients; group snapshots reflect create/join. | 7 |
| Persistence | Authorized history read, server restart, ordering, nonmember history denial, SQLite failure behavior. | 8 |
| Heartbeat and recovery | Stale connection removal, live connection survival, sequence/ACK correlation, duplicate retry policy, catch-up after reconnect. | 9-11 |
| TLS | Trusted server certificate works; untrusted CA, wrong hostname, and invalid/expired certificate fail. | 12 |
| Client UI | Separate room histories, sent/incoming rendering, text input and send control, responsive UI while network is idle or slow. | 14 |

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
network, address/port, executable revision, and observed nonce match. Confirm
that the server firewall permits the selected TCP port. If this fails, fix
reachability before adding chat features. Ping/Pong proves the socket path
across machines; it does not yet prove the assignment's chat requirements.

## Manual two-computer acceptance run

This run is for the final application; it cannot be performed at Stage 1.
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
