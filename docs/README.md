# Project documentation

Start with the [overview](overview.md) for the goal, Stage 7 network foundation,
Stage 8 history, Stage 9 heartbeats, Stage 10 message receipts/retries, Stage 11 recovery, Stage 12 TLS, Stage 13 CLI, and Stage 14 Windows GUI. The table below separates the assignment's requirements from the
team's chosen design and future work.

| Document | Contents |
| --- | --- |
| [Overview](overview.md) | Goal, user journeys, current state, and document map. |
| [Requirements](requirements.md) | R1-R11, grading gates, special features, and evidence plan. |
| [Architecture](architecture.md) | Wire format, message rules, runtime design, ownership, and security assumptions. |
| [Persistent history](history.md) | Database operation, participant capabilities, authorized pages, and acceptance semantics. |
| [Heartbeats](heartbeats.md) | Idle expiry, periodic Ping/Pong, timer ownership, configuration, and failure handling. |
| [Message reliability](reliability.md) | Stable IDs/sequences, explicit retry keys, migration, acceptance, and deduplication bounds. |
| [Recovery](recovery.md) | Identity resume, private sequence paging, bounded retries, GUI retention, and recovery limits. |
| [TLS](tls.md) | Certificate generation, explicit modes, verified DNS/IP identity, and transport ownership. |
| [CLI](cli.md) | Persistent commands/events, input limits, receipt draining, and API ownership. |
| [AI & MCP Agent](ai.md) | TextRank summarization, VADER sentiment analysis, NL agent router, and MCP JSON-RPC 2.0 server. |
| [Slides & Demo](slides.md) | 20-slide implementation explanation deck generator (`tools/build_slides.py`) and LAN demo launcher (`tools/demo.py`). |
| [Interactive onboarding](onboarding.md) | Cinematic browser journey, deterministic experiments, source atlas, authoring, tests, and production acceptance. |
| [Plan](plan.md) | Ordered milestones and exit criteria through GUI and presentation. |
| [Progress](progress.md) | Verified status, test evidence, next tasks, and task ownership. |
| [Testing](testing.md) | Automated coverage and the two-physical-computer demo script. |
| [Contributing](contributing.md) | Team workflow and definition of done. |
| [Engineering workflow](workflow.md) | Local skills, strict evidence, optional parallel work, and refactoring cadence. |
| [Workflow setup](workflow-setup.md) | Project inventory, source revisions, and adaptation decisions. |

## Setup

Choose the guide for your development machine: [Windows](setup/windows.md),
[macOS](setup/macos.md), or [Linux](setup/linux.md). The root
[README](../README.md) gives the quick build commands. Coding agents should
also follow the root [AGENTS.md](../AGENTS.md).

The [editor and documentation guide](setup/tooling.md) sets up clangd for Zed
and VS Code, explains source comments, and builds Doxygen/Sphinx/Breathe HTML.
The [GUI guide](setup/gui.md) explains running clients, room controls, and limits.

## Assignment sources

- [Socket programming project brief](assignment/socket-programming-brief.pdf)
- [Implementation-explanation slide template](assignment/implementation-explanation-template.pdf)

The PDFs are the course requirements and presentation template. The C++ stack,
wire format, and stage order in these docs are the team's implementation plan.
