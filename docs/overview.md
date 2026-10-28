# Project overview

## Goal and current state

Build a multi-client chat application for the computer-network term project.
The finished application must have separate server and client programs that
communicate through sockets. At least two clients must run on different
physical computers for the demonstration. Users need distinct names, a live
connected-user list, separate private and group chat rooms, and group discovery
with voluntary joining and member-only delivery.

**Current checkpoint: Stages 0-13, automated acceptance.** This repository has
the C++20 build foundation, framed Protocol Buffers TCP, separate server/CLI
programs, bounded sessions, unique active login names, private direct rooms,
voluntary group membership, and presence/group snapshots. A minimum headless
chat API exposes operations and typed events. The Windows GUI now adds user/group
navigation, room transcripts/drafts, and create/self-join/send controls through
a portable networking worker/model. SQLite/WAL stores messages and provides
bounded authorized pages plus explicit durable participant resume through the
headless API. Periodic worker heartbeats and server idle timers clean up stale
sessions. Stable message IDs/sequences and explicit saved-key retries are also
implemented. Bounded worker recovery resumes identity and replays missing room
sequences while preserving GUI drafts/transcripts. The suite has 211
unit/socket/native-control cases. Verified TLS 1.3, the persistent worker-backed CLI, Stage 15 AI/MCP/history features, and Stage 16 slide/demo tools are implemented. Manual GUI
walkthroughs and the physical-network
demonstration remain pending; verified results are in [progress](progress.md).

The [assignment brief](assignment/socket-programming-brief.pdf) defines the
graded requirements; the [implementation-explanation template](assignment/implementation-explanation-template.pdf)
shows the presentation evidence expected. The assignment's parts are gated:
Part 1 architecture must pass before Part 2 is graded, and all Part 2
fundamentals must pass before Part 3 special features are graded. See the
[requirement-by-requirement checklist](requirements.md).

## What the finished application should let a user do

1. Connect from a client computer and choose an unused username.
2. See all connected users, including their own name, and all existing groups
   with member lists.
3. Open a private room with another connected user, send text, and see only
   that room's messages. A third user must not receive those messages.
4. Create a group as its sole initial member. Other users choose to join it
   themselves; creating a group does not add anyone else.
5. Open a group room, send text, and see messages only if they belong to that
   group. A nonmember must not receive the group's messages.

The Windows GUI provides a message view and text input/send control for each
private and group room; see the [GUI guide](setup/gui.md). The special-feature part of the assignment is implemented with an embedded TextRank + VADER AI assistant, an MCP JSON-RPC 2.0 external agent server, verified TLS 1.3, and transactional SQLite/WAL history with on-demand loading; see [AI & MCP Agent](ai.md).

## Chosen engineering direction

The team selected C++20, CMake and vcpkg, Boost.Asio TCP sockets, Protocol
Buffers, and a four-byte big-endian length prefix. The planned server owns
session identity, room and group state, membership checks, and routing. The
client now has a minimum headless networking library so a GUI can work
without handling sockets or Protobuf directly. SQLite/WAL history is implemented;
heartbeats and automatic worker reconnect/sync and verified TLS 1.3 are implemented. These technology choices
come from the team's planning discussion; the assignment requires the visible
behavior and socket communication, not these particular libraries.

## Documentation map

| Document | Use it for |
| --- | --- |
| [Requirements](requirements.md) | R1-R11, special-feature rules, and expected evidence. |
| [Architecture](architecture.md) | Protocol, message flow, state ownership, reliability, and security assumptions. |
| [Heartbeats](heartbeats.md) | Complete-request liveness, timers, settings, and failure cleanup. |
| [Message reliability](reliability.md) | Receipts, explicit retries, schema migration, and acceptance limits. |
| [TLS](tls.md) | Verified transport, explicit modes, certificate generation, and LAN setup. |
| [CLI](cli.md) | Persistent commands, receipt output, input bounds, shutdown, and API ownership. |
| [AI & MCP Agent](ai.md) | TextRank summarization, VADER sentiment analysis, natural-language agent router, and MCP JSON-RPC 2.0 server. |
| [Slides & Demo](slides.md) | 20-slide Implementation Explanation deck generator and two-computer LAN/agent demo launcher. |
| [Plan](plan.md) | Stage-by-stage tasks and exit criteria. |
| [Progress](progress.md) | What has been verified and what remains pending. |
| [Testing](testing.md) | Automated tests and the eventual two-computer demonstration. |
| [Contributing](contributing.md) | Team workflow and definition of done. |

The assignment PDFs live in `docs/assignment/`. The [TLS guide](tls.md) covers
certificate handling; generated development credentials remain ignored.
