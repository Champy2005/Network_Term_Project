# AI agent, NLP models, and MCP control protocol

Stage 15 (`FEAT-15A` and `FEAT-15B`) integrates an embedded C++20 NLP engine,
a natural-language and optional HTTP(S) LLM tool-calling agent, and a Model
Context Protocol (MCP) JSON-RPC 2.0 external agent connector into the client
stack (`chat::ai::AiEngine`, `chat::ai::AgentController`,
`chat::ai::McpDispatcher`, and `chat::ai::LoopbackAgentServer`).

All AI and external-agent operations run strictly above the client's
`RoomModel` and `ChatWorker` boundary. The server remains 100% authoritative
for username uniqueness, pair-room and group routing, membership checks, and
SQLite history authorization.

## AI models and academic references (Slide 17)

`AiEngine::model_reference()` records the two embedded NLP models used for
deterministic offline inference and automated CTest verification:

1. **TextRank Extractive Summarization**:
   - **Reference**: Rada Mihalcea and Paul Tarau, *"TextRank: Bringing Order into Texts"*, Proceedings of EMNLP 2004, pp. 404–411.
   - **How it works**: `AiEngine::summarize_lines()` tokenizes room transcript lines, filters English stop words, computes TF-IDF cosine similarity across message pairs, and executes 30 power iterations of PageRank centrality ($d = 0.85$). Top-ranked lines are sorted back into chronological room order so the summary reads naturally.
2. **VADER-Inspired Valence Lexicon & Rule Sentiment Analysis**:
   - **Reference**: C.J. Hutto and Eric Gilbert, *"VADER: A Parsimonious Rule-based Model for Sentiment Analysis of Social Media Text"*, Proceedings of the 8th International AAAI Conference on Weblogs and Social Media (ICWSM-14), 2014.
   - **How it works**: `AiEngine::analyze_sentiment()` evaluates token valence against a curated sentiment lexicon, applies 3-token lookback negation inversion ($\times -0.74$), booster/dampener weights ($\pm 0.293$), ALL-CAPS emphasis ($\pm 0.733$), and exclamation scaling, normalizing the compound score via $\frac{x}{\sqrt{x^2 + 15}} \in [-1, +1]$ into `Positive` ($\ge 0.05$), `Negative` ($\le -0.05$), or `Neutral`.
3. **Optional OpenAI-Compatible / Ollama HTTP(S) Endpoint**:
   - `AiEngine::query_llm_endpoint()` accepts `LlmEndpointOptions` to query an external or local LLM completion server (`/v1/chat/completions`) over `Boost.Asio` within a bounded deadline, falling back cleanly to the deterministic C++ engine when disabled or offline.

## External AI agent control (Model Context Protocol / JSON-RPC 2.0)

External AI agents, IDE plugins, and automation scripts can inspect and control
a live Socket Chat client in two ways:

### 1. Standard MCP `stdio` server mode

```powershell
build/debug/chat_cli.exe --tls certs/development/ca.pem mcp 127.0.0.1 5555 mcp_bot
```

In `mcp` mode, `chat_cli` suppresses human console banners and speaks pure
newline-delimited JSON-RPC 2.0 (`protocolVersion: "2024-11-05"`) over
`stdin`/`stdout`. Supported JSON-RPC methods are `initialize`,
`notifications/initialized`, `ping`, `tools/list`, and `tools/call`.

### 2. Live GUI / CLI loopback `--agent-port` connector

Both `chat_gui` and `chat_cli` accept `--agent-port <0..65535>` after the
transport mode flag:

```powershell
build/debug/chat_gui.exe --tls certs/development/ca.pem --agent-port 7777
python tools/demo.py agent-demo --port 7777 --peer bob
```

`LoopbackAgentServer` binds strictly to `127.0.0.1`, accepts at most 4
concurrent local connections, bounds each JSON-RPC line to 65,536 bytes, and is
polled non-blockingly on the owning UI/console thread during `Window::pump()`
and `ChatConsole::poll()`. No second socket thread touches `RoomModel` or native
Win32 controls.

## Exposed MCP tools (`tools/list` and `tools/call`)

| Tool name | Arguments | Action |
| --- | --- | --- |
| `get_status` | `{}` | Return connection status, active username, selected room, counts, and AI model citation. |
| `list_users` | `{}` | Return all currently connected usernames from the latest server snapshot. |
| `list_groups` | `{}` | Return all discoverable groups (`group_id`, `name`, `members`). |
| `list_rooms` | `{}` | Return local `RoomView` states (`key`, `wire_id`, `title`, `group`, `joined`, `available`, `line_count`). |
| `open_room` | `{"target": "..."}` | Select and display a direct user room or group room in the client UI. |
| `get_room_messages` | `{"room": "..."}` | Return retained transcript lines and sequences for a room. |
| `load_room_history` | `{"room_id": "..."}` | Queue `CommandKind::history` to fetch authorized SQLite history from the server. |
| `create_group` | `{"name": "..."}` | Queue `CommandKind::create_group` (creator is the sole initial member). |
| `join_group` | `{"group_id": "..."}` | Queue `CommandKind::join_group` (voluntary self-join). |
| `send_direct_message` | `{"recipient": "...", "text": "..."}` | Queue `CommandKind::direct` to a connected user. |
| `send_group_message` | `{"group_id": "...", "text": "..."}` | Queue `CommandKind::group` to a joined group. |
| `summarize_room` | `{"room": "...", "max_sentences": 3}` | Run TextRank + VADER summarization on the room and update the UI output panel. |
| `analyze_sentiment` | `{"text": "..."}` | Run VADER sentiment analysis on text or the active room transcript. |
| `run_agent_prompt` | `{"prompt": "..."}` | Execute a natural-language instruction (`summarize`, `sentiment ...`, `dm ...`, `create group ...`, `join ...`, `load history ...`). |

## GUI and CLI controls

- **Windows GUI (`chat_gui`)**:
  - **Load history**: Fetches stored SQLite room history for the selected room via `CommandKind::history`.
  - **Summarize room**: Runs TextRank + VADER on the active room and displays the summary in the AI output panel.
  - **Sentiment**: Evaluates sentiment of the AI prompt box (or the active room transcript when the prompt box is empty).
  - **Run AI**: Executes natural-language instructions from the AI prompt box through `AgentController::execute_prompt()`.
- **Console (`chat_cli ... chat`)**:
  - `/history <room_id>`
  - `/summarize [room_id_or_key]`
  - `/sentiment <text>`
  - `/ai <instruction>`
