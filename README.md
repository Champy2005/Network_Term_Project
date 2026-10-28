# Socket Chat

A C++20 computer networks term project for a multi-client chat application.
The intended system uses a reusable headless client library and a separate
server program. The assignment's required behavior is recorded in the
[requirements matrix](docs/requirements.md); the C++ stack is our team's design
choice, not an assignment mandate.

> **Current status:** Stages 0-7 provide login, private direct rooms, self-join
> groups, member-only routing, and user/group snapshots over framed TCP.
> Stage 14 now has a Windows room GUI with a portable model/network worker.
> Stage 8 adds SQLite/WAL history and explicit participant resume through the headless API.
> Stage 9 adds periodic worker heartbeats and server stale-session cleanup.
> Stage 10 adds server message IDs/sequences and explicit retry deduplication.
> Stage 11 adds bounded worker reconnect, durable identity resume, and ordered catch-up.
> Stage 12 adds verified TLS 1.3, explicit executable transport modes, and local certificate tooling.
> Stage 13 adds a persistent worker-backed chat CLI and bounded console input.
> Stage 15 adds an embedded TextRank/VADER AI assistant, an MCP JSON-RPC 2.0 external agent server (`stdio` and `--agent-port`), and on-demand GUI/CLI room history loading.
> Stage 16 adds the 20-slide implementation-explanation generator (`tools/build_slides.py`) and LAN/agent demo launcher (`tools/demo.py`).
> Manual GUI and physical-network acceptance remain pending.
> [Progress](docs/progress.md) records the verified checkpoint and next tasks.

## Start here

For an interactive visual introduction, see [Inside Socket Chat](docs/onboarding.md).
It follows a message through the system with narrated cutaways, experiments,
and source inspection. Run its browser application from `onboarding/`.

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

## Engineering workflow

Repository-local Codex skills provide a project-specific workflow based on
pstack and Matt Pocock's skills. Start with `$engineer <task>` or use
`$implement`, `$grill`, `$architect`, `$verify`, `$review`, `$refactor`,
`$swarm`, or `$arena` for a focused mode. The
[workflow guide](docs/workflow.md) covers all skills, quality gates, and
refactoring cadence. Evaluate refactoring after substantial changes; review
larger debt at milestone boundaries when it warrants cleanup.

```text
python tools/verify.py workflow
python tools/verify.py quick
python tools/verify.py full
```

`full` reuses Debug/Release/ASAN, executable smoke, clangd, and strict docs,
with evidence under ignored `.cache/verification/`. Local automation does
not finish the pending manual/physical acceptance.

## Editor and documentation setup

Zed and VS Code share `.clangd`, which reads the Debug compiler database.
On Windows, `tools/dev.ps1` discovers the toolchain and prepares generated
headers. A separate Python environment builds Doxygen XML/HTML and the
Sphinx/Breathe site, including Markdown guides and all handwritten source/test
files. See the [step-by-step tooling guide](docs/setup/tooling.md) for editor
tasks, installation, commenting conventions, and troubleshooting.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev.ps1 -Action prepare
python tools/check_clangd.py
python tools/docs.py setup
python tools/docs.py build
python tools/docs.py serve
```

## Run the Windows chat GUI

After building Debug, run the server in a terminal and open one GUI per user:

```powershell
python tools/dev_tls.py
build/debug/chat_server.exe --tls certs/development/server.pem certs/development/server.key 127.0.0.1 5555
build/debug/chat_gui.exe --tls certs/development/ca.pem
```

Generate certificates once; existing credentials are never overwritten. See the
[TLS guide](docs/tls.md) for trust files, LAN SANs, and alternate paths.
Enter a username and connect. Select a connected user for a direct room, or
select a group to see its members and join it. Each room keeps a separate
transcript and draft; Send waits for server acceptance and the sender echo.
See the [GUI guide](docs/setup/gui.md) for controls, limits, and pending checks.
The first GUI adapter is Windows-only. Its `RoomModel` and `ChatWorker` are
portable C++ and use the existing headless API.

## Run the persistent chat CLI

After starting the TLS server above, open a terminal per user:

```powershell
build/debug/chat_cli.exe --tls certs/development/ca.pem chat 127.0.0.1 5555 Alice_01
```

Use `/users`, `/groups`, `/dm Bob_02 text`, `/create Study group`, `/join group-ID`,
and `/group group-ID text`. Incoming messages, heartbeats, and recovery continue
while input is idle. `/quit` or EOF drains submitted receipts before stopping.
See the [CLI guide](docs/cli.md) for commands, output, bounds, and exit behavior.

## Try the diagnostic client

After building Debug, run these in two terminals from the repository root
(on Windows, append `.exe` to executable names):

```text
build/debug/chat_server --plaintext-dev 127.0.0.1 5555
build/debug/chat_cli --plaintext-dev 127.0.0.1 5555 42
build/debug/chat_cli --plaintext-dev 127.0.0.1 5555 42 Alice_01
```

The optional final argument claims a username first and prints the accepted
name, then `Pong nonce=42`. Names contain 1-32 ASCII letters, digits, or
underscores and are case sensitive. A rejected login exits with code one.
This one-shot diagnostic disconnects when it exits, releasing its name.
The `chat` form above provides persistent messaging through the same headless API. The server accepts
multiple clients and stays running until Ctrl+C. The CLI returns a nonzero
exit code for invalid arguments, connection failures, timeouts, or invalid
responses. Its connect and exchange deadlines each default to three seconds.

For a LAN smoke test, bind the server to its LAN address (or `0.0.0.0`) and
connect the CLI to that computer's LAN IP. Port `0` lets the server choose and
print an available port. LAN connections require `--tls`, a trusted CA, and a
certificate SAN matching the server address; see [TLS setup](docs/tls.md).
`--plaintext-dev` permits loopback connections only.
The two-physical-computer test remains pending.

## Headless chat API

Include `chat/client/chat_client.hpp` and link `chat::transport`. `ChatClient`
keeps Boost.Asio and Protobuf out of the application's interface. It supports
`connect`, `login`, `ping`, `send_direct`, `create_group`, `join_group`,
`send_group`, `next_event`, and `close`. Operations return success/resource IDs
or a typed error. `next_event` returns direct/group deliveries or complete
user/group snapshots; a quiet receive deadline returns `nullopt` and keeps the
connection open. Calls are blocking and must have one caller per instance;
the supplied `ChatWorker` owns those calls on its networking thread.

Direct messages reach the sender and recipient only. A group starts with its
creator; every additional member joins by their own request. Disconnect removes
group membership, so a client reclaiming the same name must join again. Text
is limited to 4,096 UTF-8 bytes, group names to 64 bytes, and groups to 128 per
server database (or ephemeral run). The server program stores chat in
`data/chat.sqlite3` by default. Paginated `history` and explicit `resume_login`
are available through the headless API; see the [history guide](docs/history.md).
The worker implements automatic recovery and sequence catch-up; general GUI
history browsing remains pending. Pass `ClientTlsOptions` to `ChatClient` or
`ChatWorker` for verified TLS; see [TLS configuration](docs/tls.md).

Message results, deliveries, and history expose server IDs and per-room sequences.
Save a key from `ChatClient::new_message_key()` before an uncertain attempt, then
use `send_direct_with_key` or `send_group_with_key` to retry the exact message.
Storage-enabled servers retain retries across identity resume/restart; accepted
duplicates return the original ACK without another delivery. Existing v1
databases upgrade transactionally to v2. See [message reliability](docs/reliability.md)
for authorization, migration, bounded ephemeral retries, and receipt limits.

The GUI worker sends a Ping every 10 seconds. The server expires a connection
after 30 seconds without a complete inbound request; partial bytes and outgoing
messages do not extend it. Direct users of the blocking headless API must
schedule their own `ping` calls, including read-only consumers. The server accepts
an explicit `--tls <chain> <key>` or `--plaintext-dev` followed by
`[bind-address] [port] [database-path] [idle-timeout-ms]`; zero explicitly disables
expiry. See the [heartbeat guide](docs/heartbeats.md) for limits and cancellation.

## Repository map

| Path | Purpose | Current state |
| --- | --- | --- |
| `proto/` | Versioned application message schema | Implemented |
| `include/chat/protocol/`, `src/protocol/` | Public framing API and codec | Implemented |
| `include/chat/client/`, `src/client/` | Headless client SDK | Chat operations/events, private sync, and bounded worker recovery |
| `include/chat/server/`, `src/server/` | Server transport, routing, state | Authoritative hub, bounded sessions, and transactional SQLite history |
| `apps/server/`, `apps/cli/` | Separate server and CLI entry points | Persistent chat and Ping/login diagnostics |
| `apps/gui/` | Windows client entry point and native controls | Room navigation, drafts, transcripts, create/join/send |
| `tests/unit/`, `tests/integration/` | Component, socket, and hidden native-control tests | 42 unit, 160 socket, and 9 GUI cases |
| `include/chat/ai/`, `src/ai/` | TextRank summarization, VADER sentiment, NL router, and MCP JSON-RPC 2.0 server | Implemented |
| `tools/build_slides.py`, `tools/demo.py` | 20-slide Implementation Explanation generator and live LAN/MCP demo launcher | Implemented |
| `docs/` | Setup, architecture, plan, testing, and assignment sources | Available |
| `include/chat/transport/`, `src/transport/` | Verified TLS and common socket I/O | TLS 1.3 and explicit loopback development mode |
| `certs/` | Local TLS certificate guidance | Generator output stays ignored |

## Project direction

The planned path is persistent TCP sockets with Boost.Asio, Protobuf messages
inside four-byte big-endian length frames, a single-server hub that authorizes
direct and group delivery, and the Windows GUI built on the headless client API.
SQLite/WAL history, message IDs/deduplication, and automatic worker reconnect/sync
and verified TLS 1.3 are implemented. An AI feature is required for the assignment's special-feature
section and will follow the fundamental chat requirements. NATS, Redis,
PostgreSQL, QUIC, and end-to-end encryption are outside the initial build.

See the [documentation index](docs/README.md) for the full plan, test matrix,
manual demonstration steps, and current progress.
