# Project overview

## Goal and current state

Build a multi-client chat application for the computer-network term project.
The finished application must have separate server and client programs that
communicate through sockets. At least two clients must run on different
physical computers for the demonstration. Users need distinct names, a live
connected-user list, separate private and group chat rooms, and group discovery
with voluntary joining and member-only delivery.

**Current checkpoint: Stage 1 only.** This repository has the C++20 build
foundation, a Protocol Buffers schema, a bounded length-frame codec, and unit
tests. It cannot connect clients or send chat messages yet. The planned design
and future milestones in these docs are not implemented behavior.

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

A later GUI must provide a message view and text input/send control for each
private and group room. The special-feature part of the assignment also
requires at least one AI feature. The specific AI feature has not been chosen.

## Chosen engineering direction

The team selected C++20, CMake and vcpkg, Boost.Asio TCP sockets, Protocol
Buffers, and a four-byte big-endian length prefix. The planned server owns
session identity, room and group state, membership checks, and routing. The
client will eventually use a headless networking library so a GUI can work
without handling sockets or Protobuf directly. SQLite/WAL history, heartbeat,
reconnect support, and TLS 1.3 are later design goals. These technology choices
come from the team's planning discussion; the assignment requires the visible
behavior and socket communication, not these particular libraries.

## Documentation map

| Document | Use it for |
| --- | --- |
| [Requirements](requirements.md) | R1-R11, special-feature rules, and expected evidence. |
| [Architecture](architecture.md) | Protocol, message flow, state ownership, reliability, and security assumptions. |
| [Plan](plan.md) | Stage-by-stage tasks and exit criteria. |
| [Progress](progress.md) | What has been verified and what remains pending. |
| [Testing](testing.md) | Automated tests and the eventual two-computer demonstration. |
| [Contributing](contributing.md) | Team workflow and definition of done. |

The assignment PDFs live in `docs/assignment/`. Certificate handling for the
future TLS stage is outlined in the root
[certificate notes](../certs/README.md).
