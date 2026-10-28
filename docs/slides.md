# Implementation explanation slides and live demo guide

Stage 16 (`DEMO-16A` and `DEMO-16B`) covers the graded 20-slide
Implementation Explanation deck (`docs/assignment/implementation-explanation-template.pdf`)
and the live two-computer / AI-agent demonstration workflow (`docs/assignment/socket-programming-brief.pdf`).

## Generating and verifying the 20-slide deck

Run the slide builder from the repository root:

```powershell
python tools/build_slides.py --check
python tools/build_slides.py
```

- `--check` verifies that every client/server code snapshot across the 20 slides
  resolves to existing repository files and valid non-empty line ranges.
- Without `--check`, the script generates a print-ready 16:9 HTML slide deck at
  `build/slides/index.html` with syntax-highlighted source excerpts, exact file
  and line numbers, and a keyboard/printable slide viewer (press `Ctrl+P` in any
  browser to export a 20-page landscape PDF).

## Slide map (Template Slides 1–20)

| Slide | Graded Item | Primary Client Code | Primary Server / Engine Code |
| --- | --- | --- | --- |
| 1 | Title & Team Members | — | — |
| 2 | Overview of Implemented Requirements (`R1`–`R11`) | — | — |
| 3 | Overview of Special Features (`SP1`–`SP4`) | — | — |
| 4 | Overview of Used Frameworks & Libraries | `CMakeLists.txt` | `vcpkg.json` |
| 5 | **R1** Client-Server Architecture | `src/client/chat_client.cpp` | `src/server/chat_server.cpp` |
| 6 | **R2** Underlying Protocol (`TCP` + TLS 1.3) | `include/chat/transport/tls.hpp` | `src/transport/tls.cpp` |
| 7 | **R3** Simultaneous Multiple Clients | `src/client/chat_worker.cpp` | `src/server/chat_server.cpp` |
| 8 | **R4** GUI & CLI User Interfaces | `apps/gui/window.cpp` | `apps/cli/chat_console.cpp` |
| 9 | **R5** Private (1-to-1) Messaging | `src/client/chat_client.cpp` | `src/server/chat_server.cpp` |
| 10 | **R6** Group Messaging | `src/client/chat_client.cpp` | `src/server/chat_server.cpp` |
| 11 | **R7** Discovering Online Users | `src/client/room_model.cpp` | `src/server/chat_server.cpp` |
| 12 | **R8** Creating & Joining Groups | `src/client/chat_client.cpp` | `src/server/chat_server.cpp` |
| 13 | **R9** Smooth & Responsive Message Display | `src/client/chat_worker.cpp` | `apps/gui/window.cpp` |
| 14 | **R10** Error Handling | `src/protocol/frame.cpp` | `src/server/chat_server.cpp` |
| 15 | **R11** Vanilla Socket API (`Boost.Asio` TCP) | `src/protocol/frame.cpp` | `src/server/chat_server.cpp` |
| 16 | **Special Point 1** (Mandatory AI Feature — Overview & Demo) | `apps/gui/window.cpp` | `src/ai/agent_tools.cpp` |
| 17 | **Special Point 1** (AI Implementation & Academic Models) | `include/chat/ai/ai_engine.hpp` | `src/ai/ai_engine.cpp` |
| 18 | **Special Point 2** (MCP JSON-RPC 2.0 External Agent Server) | `include/chat/ai/agent_tools.hpp` | `src/ai/agent_tools.cpp` |
| 19 | **Special Point 3** (TLS 1.3 X.509 CA & SAN Verification) | `include/chat/transport/app_options.hpp` | `src/transport/tls.cpp` |
| 20 | **Special Point 4** (SQLite/WAL Persistence & Gapless Sync) | `src/client/room_model.cpp` | `src/server/history_store.cpp` |

## Live demonstration launcher (`tools/demo.py`)

`tools/demo.py` automates TLS certificate provisioning (`--san-ip` for LAN),
server startup, GUI/CLI client launch, and live MCP JSON-RPC 2.0 agent control:

```powershell
# Print the two-computer LAN checklist and exact commands:
python tools/demo.py checklist --lan-ip 192.168.1.50

# Machine A: Generate LAN TLS certs and start the authoritative server:
python tools/demo.py server --lan-ip 192.168.1.50 --port 5555

# Machine A or B: Launch the Win32 GUI with loopback AI agent port 7777:
python tools/demo.py gui --host 192.168.1.50 --port 5555 --agent-port 7777

# Drive the running GUI or CLI live via MCP JSON-RPC 2.0 over loopback TCP:
python tools/demo.py agent-demo --agent-port 7777
```
