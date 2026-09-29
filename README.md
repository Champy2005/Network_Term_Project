# Socket Chat

A C++20 computer networks term project for a multi-client chat application.
The intended system uses a reusable headless client library and a separate
server program. The assignment's required behavior is recorded in the
[requirements matrix](docs/requirements.md); the C++ stack is our team's design
choice, not an assignment mandate.

> **Current status:** Stage 0-1 is complete: build setup, a typed Protobuf
> schema, bounded length-prefixed framing, and unit tests. There is **no
> runnable chat server or client yet**. [Progress](docs/progress.md) tracks the
> remaining work; [Stage 2](docs/plan.md) starts with TCP Ping/Pong.

## Start here

1. Follow the setup guide for [Windows](docs/setup/windows.md),
   [macOS](docs/setup/macos.md), or [Linux](docs/setup/linux.md).
2. From the repository root, configure, build, and test:

   ```text
   cmake --preset debug
   cmake --build --preset debug
   ctest --preset debug
   ```

3. Read the [project overview](docs/overview.md),
   [architecture](docs/architecture.md), and [contribution guide](docs/contributing.md)
   before starting a milestone.

The first vcpkg configure downloads and builds dependencies and can take several
minutes. `debug`, `release`, and `asan` are available preset names. The
`asan` tests on Windows must run from a Visual Studio developer shell so the
sanitizer runtime is on `PATH`. Generated Protobuf code and build outputs stay
under `build/` and are not committed.

## Repository map

| Path | Purpose | Current state |
| --- | --- | --- |
| `proto/` | Versioned application message schema | Implemented |
| `include/chat/protocol/`, `src/protocol/` | Public framing API and codec | Implemented |
| `include/chat/client/`, `src/client/` | Headless client SDK | Reserved |
| `include/chat/server/`, `src/server/` | Server sessions, routing, state | Reserved |
| `apps/server/`, `apps/cli/` | Separate server and CLI entry points | Reserved |
| `tests/unit/`, `tests/integration/` | Unit and future network tests | Unit tests implemented |
| `docs/` | Setup, architecture, plan, testing, and assignment sources | Available |
| `certs/` | Future local TLS certificate guidance | Guidance only |

## Project direction

The planned path is persistent TCP sockets with Boost.Asio, Protobuf messages
inside four-byte big-endian length frames, a single-server hub that authorizes
direct and group delivery, and a GUI built on the headless client API. TLS 1.3,
SQLite/WAL history, acknowledgements, and reconnect recovery are later
milestones. An AI feature is required for the assignment's special-feature
section and will follow the fundamental chat requirements. NATS, Redis,
PostgreSQL, QUIC, and end-to-end encryption are outside the initial build.

See the [documentation index](docs/README.md) for the full plan, test matrix,
manual demonstration steps, and current progress.
