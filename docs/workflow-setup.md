# Workflow setup record

Reviewed 2026-10-07. This is developer tooling, not a graded project stage.

## Project inventory

- C++20, CMake/Ninja, vcpkg, GoogleTest/CTest, Debug/Release/ASAN presets.
  Windows x64 is verified; its native GUI builds separately from portable
  client API, worker/model, server, and protocol.
- Framed Protobuf over TCP/TLS, an authoritative hub/strand, bounded ordered
  writers, SQLite/WAL history, durable resume/receipts, bounded recovery,
  and a persistent worker-backed CLI already exist.
- Existing acceptance includes 37 unit, 157 socket, and 8 native-control cases,
  CLI/TLS process smoke, clangd, and strict Doxygen/Sphinx. Reuse that stack.
- R1-R11 and the assignment PDFs remain the grading authority. Manual GUI,
  native terminal, physical LAN, other OS, AI, snapshots, and slides stay pending.
- Setup began with substantial uncommitted/untracked product work. Preserve it;
  no reset, stash, or commit is needed. Task-start hashes stay local.

## Configuration decisions

Use `.agents/skills/`, standalone `.codex/agents/*.toml`, and a small
`.codex/config.toml` containing only agent defaults. This follows current
official docs instead of the brief's older `[agents.<name>]`/`config_file`
suggestion. Preserve primary model/permissions. Three children fit the current
four-agent runtime; usually fewer are useful.

Use one coordinator and task-specific playbooks. Keep shared quality guidance
in one conditional reference and reuse existing docs. Extra glossary/tracker/
specification/ADR/verification-map hierarchies are unnecessary. Task/handoff
notes are optional and local. No scheduled jobs, global skill installation,
CI change, or external service is introduced.

Correctness/requirements/stability/maintenance are mandatory for behavior
changes. Extra security/performance/fuzz/soak work depends on risk. Evaluate
refactoring after substantial work; broader cleanup is a milestone task.
Preserve useful Doxygen and test contracts.

## Provenance

| Primary source | Inspected revision | Used for |
| --- | --- | --- |
| [Canonical pstack](https://github.com/cursor/plugins/tree/d0ef80d86795816da932a153458c5dbe192d294e/pstack) | `d0ef80d86795816da932a153458c5dbe192d294e` | Coordinator, task playbooks, swarm/arena phases, real-surface verification |
| [Matt Pocock skills](https://github.com/mattpocock/skills/tree/f3fc5632f401156837ee3872f14fe33ccf1024ea) | `f3fc5632f401156837ee3872f14fe33ccf1024ea` | Implementation/TDD slices, grilling, architectural friction, handoff |
| [Multi-runtime pstack port](https://github.com/michael-denyer/pstack-claude/tree/552b1c85f990f1b0bb7d9806aa2e8461ce5438d0) | `552b1c85f990f1b0bb7d9806aa2e8461ce5438d0` | Codex compatibility reference; no wholesale installation |

Retained: task routing, inspect-before-edit, named state/ownership, useful
decomposition, verifiable units, actual-artifact proof, review/repair/reverify,
and supported completion. Arena retains frame, candidates, comparison, pick,
graft, and verification; swarm retains frame, fanout, aggregation, and report.

Adapted: mandatory model diversity/delegation becomes optional bounded Luna
work; automatic commits/PRs need an explicit human request; established API
boundaries replace repeated seam approval. Remove mandatory trackers/glossaries,
prose rituals, and comment deletion. These are local adaptations, not unchanged
upstream packages; no popularity ranking is claimed.

Lauren Tan and Matt Pocock's MIT copyright/permission notices are preserved in
`docs/workflow-licenses.txt`. Review upstream updates selectively instead of
overwriting project-specific instructions.

Configuration/discovery references:

- [Build skills](https://learn.chatgpt.com/docs/build-skills)
- [Subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents)
- [Configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference)

Installed CLI: `codex-cli 0.153.4`. Local validation checks our flat frontmatter,
TOML fields, names/references, Luna defaults, and absence of primary/permission
overrides. It is not a complete Codex schema validator or proof that an ongoing
chat refreshed its settings. Native discovery/config and independent exercise
are recorded in [progress](progress.md) only after observation. Normal commands
and evidence rules are in [workflow](workflow.md).

This chat exposes generic subagent tools rather than named-role selection.
The harmless smoke uses explicit Luna model/effort and scoped role instructions;
it does not claim to have exercised native named-profile selection. Native
startup inspection confirms repository guidance/skills, and native config reads
confirm the project layer and agent defaults. Profile TOML is structurally
validated; the documented fallback keeps the role contracts usable here.
