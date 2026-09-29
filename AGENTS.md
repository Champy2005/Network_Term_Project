# Project guidance for coding agents

Read [README.md](README.md), [docs/progress.md](docs/progress.md),
[docs/plan.md](docs/plan.md), [docs/architecture.md](docs/architecture.md), and
[docs/requirements.md](docs/requirements.md) before changing behavior. The
assignment PDFs in `docs/assignment/` define the graded requirements; the
C++ stack and milestone order are project decisions.

## Working rules

- Follow the user's requested scope. Keep `docs/progress.md` honest: only mark
  behavior done after its exit criteria and tests pass.
- Preserve existing Protobuf field numbers and the four-byte big-endian frame
  format. A frame payload must be 1 to 1,048,576 bytes. Treat TCP as a byte
  stream, including partial and coalesced reads.
- Keep the server authoritative for username, sender identity, rooms, group
  membership, routing, and later persistence. A GUI must not enforce privacy
  on behalf of the server.
- Keep the future client networking API independent of the GUI. Keep client
  and server entry points in separate files under `apps/`.
- Give each future connection one ordered write path and bounded buffering.
  Define ownership and cancellation for every asynchronous operation.
- Add tests for new behavior, especially unauthorized or unintended delivery.
  Use the `debug` and `release` presets; run `asan` where the environment
  supports it. Update architecture, tests, and progress docs when semantics
  change.
- Do not commit generated build files, private keys, secrets, or real chat
  content in logs.

## Current commands

Set `VCPKG_ROOT` as described in `docs/setup/`, then run from the repository
root:

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The current code implements only the Stage 0-1 protocol foundation. The
current test suite has 12 GoogleTest cases; socket behavior remains pending.
