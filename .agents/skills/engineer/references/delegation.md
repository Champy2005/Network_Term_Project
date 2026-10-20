# Optional delegation

Use this only when independent investigation, test design, or review helps
enough to justify another context. An applicable skill can request delegation;
respect higher-priority authorization/tool limits. Do not create user-owned
Codex chats to perform subtasks.

Prefer one or two children; the configured cap is three children plus the
primary, and a lower live runtime limit wins. All children use `gpt-6-luna`:
`code_mapper` and `implementer` at `high`; `test_engineer` and `reviewer` at
`xhigh`. Preserve the human's primary model and do not escalate models without
an explicit request. If Luna is unavailable, report it; use a permitted
same-agent pass rather than silently choosing a more expensive model.

On native custom-agent tooling, use `.codex/agents/` profiles. On a runtime
without named-role selection, read the relevant profile, give its instructions
and the scoped task to the available subagent tool, and explicitly select its
model/effort if supported. Use a fresh, narrow context when setting overrides.
Do not assert that a TOML file changed a running child's model unless observed.

Each brief contains the goal, acceptance criteria, paths/symbols, baseline or
artifact identity, allowed writes, relevant invariants, verification, and return
contract. Independent reviewers get the request and artifact rather than a
suggested answer. Shared checkout edits need disjoint ownership. Assign one
owner for `proto/`, CMake, shared headers, docs, and build directories; never
run competing configure/build processes on the same preset directory.

Wait for required evidence, inspect the actual output/diff, and synthesize it.
Missing or failed worker results are gaps, not passes. Do not endlessly respawn.
For unavailable delegation, do separate analysis/test/review passes yourself
and disclose that they were not independent. Stop only owned worker/process
activity; never reset or delete the human's existing working tree.
