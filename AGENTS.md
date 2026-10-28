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
- Keep short Doxygen file/API/test explanations current. Zed and VS Code share
  `.clangd` and the Debug compiler database; do not commit personal include paths.
  Use `python tools/check_clangd.py` and `python tools/docs.py build` to verify
  C++ diagnostics and source documentation (see `docs/setup/tooling.md`).

## Engineering workflow

For substantial tasks read `.agents/skills/engineer/SKILL.md` and only its
matching playbook/references. Focused skills live in `.agents/skills/`; the
[workflow guide](docs/workflow.md) explains scope. Clear requests proceed
without mandatory interviews, tracker setup, or repeated test-boundary approval.

Correctness, requirements, stability, and maintainability are mandatory.
Select heavier security/performance/fuzz/soak work from actual risk; server
authorization, privacy, TLS trust, and bounds remain correctness. Evaluate
refactoring after substantial work: fix related small problems while green,
record larger debt, and review it at milestone boundaries. No timed rewrites.

Use `python tools/verify.py full` for final behavior/build-gate acceptance,
`quick` for iteration, and `workflow` for skill/tooling-only changes, plus a
documentation build for guides. Add feature-specific observable proof. Report
skips/failures/partial checks honestly; never infer manual/physical acceptance.
Preserve task-start uncommitted/untracked work. Commit, push, merge, deployment,
submission, and external messaging need explicit human authorization.

Parallel work is optional under applicable skills: Luna workers, normally one
or two, at most three children plus the primary and within live tool limits.
Give disjoint ownership and one writer for coupled code/shared builds. Preserve
the primary model selected by the human. `.codex/agents/` contains scoped
contracts; defaults do not replace live runtime permissions.

## Current commands

Set `VCPKG_ROOT` as described in `docs/setup/`, then run from the repository
root:

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The current code implements Stages 0-13: protocol, TCP, bounded sessions,
server-validated login, direct rooms, groups, and presence/discovery snapshots.
A GUI-independent chat/event API, portable worker/model, and Windows room GUI
are available. SQLite/WAL history supports explicit durable identity resume
and authorized bounded pages; active membership remains connection-scoped.
Periodic worker Ping/Pong and complete-request server idle expiry clean up stale sessions.
Stable message IDs/sequences and explicit sender-scoped saved-key retries are implemented.
Schema v2 upgrades v1 atomically; ephemeral retries use a 256-key connection window.
The worker retains durable identity and message keys during bounded reconnect,
self-rejoins only chosen groups, and replays contiguous private room sequences.
TLS 1.3 verifies the server CA and DNS/IP SAN before application I/O. Executables
require `--tls` or explicit `--plaintext-dev`; plaintext permits loopback only.
The persistent CLI uses ChatWorker for core flows, heartbeats, and recovery;
stdin polling/input lines and pending receipts are bounded. Unknown command enums
are rejected. Stage 15 adds TextRank/VADER AI (`chat::ai::AiEngine`), MCP JSON-RPC 2.0
agent tools (`McpDispatcher`, `LoopbackAgentServer`), and on-demand history loading;
Stage 16 adds `tools/build_slides.py` and `tools/demo.py`. The Windows suite has 42 unit,
160 socket integration, and 9 hidden native-control GoogleTest cases (211 total).
Manual GUI/editor walkthroughs, physical-network verification, and native CLI editing/Ctrl+C remain pending.
