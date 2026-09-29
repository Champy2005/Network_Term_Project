# Progress tracker

**Last reviewed:** 2026-09-29. This page records verified work, while the
[implementation plan](plan.md) describes intended future work. A status changes
to **Done** only after its exit criteria and tests pass. The
[requirements checklist](requirements.md) separately tracks what the graded
assignment still needs.

## Current checkpoint

**Stages 0 and 1 are complete; Stage 2 has not started.** The repository has
no server or client executable, socket integration test, chat routing, GUI,
database, TLS connection, or AI feature. No two-physical-computer demonstration
has been run. All R1-R11 end-to-end assignment criteria remain pending.

| Stage | Status | Verified evidence |
| --- | --- | --- |
| 0. Toolchain | Done | C++20 CMake project, vcpkg manifest, warnings, `debug`/`release`/`asan` presets, and GoogleTest target are present. |
| 1. Protocol and framing | Done | Generated Protobuf schema and standalone frame codec compile. Debug, Release, and ASAN builds each passed the CTest target `chat_tests`: 12 GoogleTest cases in two suites. |
| 2. Plain TCP proof | Not started | No server/client entry points or real-socket tests. |
| 3. Session ownership | Not started | No `ClientSession` or async write queue. |
| 4. Login | Not started | Schema only; no registration handler or unique-name enforcement. |
| 5. Direct rooms | Not started | Schema only; no routing or privacy enforcement. |
| 6. Groups | Not started | Schema only; no creation, membership, join, or group routing. |
| 7. Presence and discovery | Not started | Schema only; no connected-user or group update broadcasts. |
| 8. History | Not started | SQLite is a declared dependency but no database code exists. |
| 9. Heartbeats | Not started | Ping/Pong messages are defined but no timer or timeout handler exists. |
| 10. IDs and ACK | Not started | Fields are defined but not assigned or processed. |
| 11. Reconnect and sync | Not started | No reconnect logic or sync messages. |
| 12. TLS | Not started | OpenSSL is a declared dependency but no TLS code exists. |
| 13. Client API/CLI polish | Not started | No headless client implementation or CLI. |
| 14. Room GUI | Not started | No GUI or room controls. |
| 15. Special features | Not started | No AI or other special feature selected or implemented. |
| 16. Demonstration and slides | Not started | No physical-computer demo or completed slide deck. |

## Stage 1 verification details

The current unit suite checks big-endian length encoding, incremental header
and payload reads, several frames in one read, the maximum 1 MiB payload,
zero and oversized lengths, truncated frames at EOF, Protobuf round-trip, and
malformed Protobuf rejection. CTest runs one executable, `chat_tests`, which
contains 12 GoogleTest cases. The local Debug, Release, and ASAN test logs each
report 12 passed cases. The ASAN preset uses `RelWithDebInfo` with sanitizer
instrumentation; on Windows its tests need an x64 Visual Studio developer
environment so the sanitizer runtime DLL is available.

These tests exercise bytes and serialization in process. They do **not** prove
TCP communication, privacy, authorization, persistence, or assignment R1-R11.
There is no cross-platform CI result yet; the verified builds were run in the
local Windows x64 Visual Studio environment.

## Actionable task board

Owners are **TBD** until the team assigns them. All tasks below are pending;
the stage table above remains the summary of verified status. Record observed
evidence in the stage summary or task row before changing a task to Done.
`INFRA-01A` improves portability and is non-blocking; it is not a graded
assignment stage.

| ID | Stage / dependency | Deliverable and required evidence | Owner | Status |
| --- | --- | --- | --- | --- |
| NET-02A | 2, after Stage 1 | Separate server and CLI entry points; integration test with two TCP clients. | TBD | Not started |
| NET-02B | 2, after NET-02A | Framed Protobuf Ping/Pong; nonce, fragmented-read, failure, and shutdown tests. | TBD | Not started |
| NET-03A | 3, after Stage 2 | Safe `ClientSession` ownership and cancellation; disconnect-under-load test. | TBD | Not started |
| NET-03B | 3, after NET-03A | One ordered bounded write queue per socket; concurrent-send ordering test. | TBD | Not started |
| NET-04A | 4, after Stage 3 | Server-validated login, unique active names, and disconnect cleanup; duplicate and unauthenticated tests. | TBD | Not started |
| NET-05A | 5, after Stage 4 | Pair-specific direct rooms and delivery; two-room separation test. | TBD | Not started |
| NET-05B | 5, after NET-05A | Direct privacy enforcement; three-client test asserts Charlie receives no payload. | TBD | Not started |
| NET-06A | 6, after Stage 5 | Group create and self-join operations; creator-only and explicit-join tests. | TBD | Not started |
| NET-06B | 6, after NET-06A | Member-only group delivery; three-client nonmember and forged-sender tests. | TBD | Not started |
| NET-07A | 7, after Stage 6 | Connected-user and all-group/member snapshots; login/create/join/disconnect update tests. | TBD | Not started |
| DATA-08A | 8, after Stage 7 | SQLite/WAL room history; restart, authorization, and failure tests. | TBD | Not started |
| NET-09A | 9, after Stage 8 | Heartbeat timeout and stale-session cleanup; healthy and unresponsive client tests. | TBD | Not started |
| NET-10A | 10, after Stage 9 | Message IDs, room sequences, ACK correlation, and dedup policy; retry/order tests. | TBD | Not started |
| NET-11A | 11, after Stage 10 | Reconnect and catch-up protocol; gap and duplicate-display tests. | TBD | Not started |
| SEC-12A | 12, after Stage 11 | TLS 1.3 with chain and hostname validation; valid/invalid certificate tests. | TBD | Not started |
| API-13A | 13, after Stage 12 | Stable headless client API and diagnostic CLI; API event and reconnect tests. | TBD | Not started |
| UI-14A | 14, after Stage 7 and minimum client API | User/group navigation and separate direct/group message views with text send controls; manual R5-R6 walkthrough. | TBD | Not started |
| UI-14B | 14, after UI-14A | Three-client GUI acceptance for R3-R11, backed by wire-level privacy tests. | TBD | Not started |
| FEAT-15A | 15, after Part 2 | Select and implement an AI special feature; model reference, test, live demo, and code snapshot. | TBD | Not started |
| FEAT-15B | 15, after Part 2 | Select up to three other special features, then implement and verify each claimed one with a live demo and code snapshot. | TBD | Not started |
| DEMO-16A | 16, after UI-14B | Server plus clients on two physical computers; record machines, network, version, and R1-R11 results. | TBD | Not started |
| DEMO-16B | 16, after DEMO-16A | Final implementation-explanation slides with required code snapshots and feature evidence. | TBD | Not started |
| INFRA-01A | Non-blocking, after Stage 1 | Verify macOS and Linux configure/build/test, and add CI when a remote is available; record actual platform results. | TBD | Not started |

## Next checkpoint: Stage 2

Create the separate server and CLI programs under `apps/server/` and
`apps/cli/`. Reuse the frame codec and generated `Envelope` to exchange
`Ping`/`Pong` across TCP. Add integration tests for multiple clients,
fragmented input, connection failure, and shutdown. Keep login, chat, groups,
and TLS out of this checkpoint. Once the Stage 2 exit criteria in the
[plan](plan.md) pass, update this page with the exact test results.
