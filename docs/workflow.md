# Engineering workflow

Use `$engineer <task>` for substantial work. It routes to a feature, bug-fix,
refactoring, or investigation playbook, then selects design and verification
for the requested outcome. Clear requests proceed to implementation. The
existing assignment requirements, architecture, plan, tests, and progress
tracker remain the project records.

This is a local adaptation of [pstack](https://github.com/cursor/plugins/tree/main/pstack)
and [Matt Pocock's skills](https://github.com/mattpocock/skills). It preserves
pstack's coordinator/playbook structure, investigation, verifiable steps,
bounded parallelism, review, and repair, with Matt's focused interviewing,
behavioral test slices, architecture assessment, and handoffs. The
[setup record](workflow-setup.md) identifies sources and adaptation choices.

## Everyday use

```text
$engineer Implement <feature and intended behavior>
$engineer Fix <bug and reproduction>
$implement <clear local specification or small task>
$verify Check <feature> against <acceptance criteria>
$review Review the changes from this task
```

Skills live in `.agents/skills/` and travel with this repository without
changing global settings. Open this project in Codex and use the `$` skill
selector. If new skills do not appear, start a fresh chat/restart Codex;
ongoing chats can retain older skill/config snapshots. Without skill selection,
ask the agent to read `.agents/skills/engineer/SKILL.md` directly. Names shared
with an installed collection are separate entries: select the repository path.
No wholesale upstream installation is needed for these commands. See
[official skill discovery](https://learn.chatgpt.com/docs/build-skills).

| Skill | Use it for |
| --- | --- |
| `$engineer` | One coordinated task through verified handoff |
| `$implement` | A clear feature/fix/specification in tested vertical slices |
| `$grill` | Consequential unresolved behavior/design questions |
| `$architect` | A meaningful boundary, ownership, state, or API choice |
| `$diagnose` | Reproduction and evidence for a runtime bug |
| `$verify` | Acceptance checks through real APIs/sockets/executables |
| `$review` | Read-only review of the exact task artifact |
| `$refactor` | Scoped behavior-preserving cleanup or maintenance assessment |
| `$swarm` | Independent parallel investigation/test/review slices |
| `$arena` | Competing designs/prototypes for a material uncertainty |
| `$handoff` | A continuation record with evidence and remaining work |
| `$teach` | Understand the code and prepare assignment explanations |

One coordinator owns the task and reads other skills for relevant phases;
you do not need to chain every command. Review, design, diagnosis, or teaching
alone does not authorize implementation. Commits, pushes, merges, deployment,
submission, and external messages need an explicit human request.

## Mandatory and conditional quality

Every behavior change needs independently specified observable acceptance,
meaningful success and relevant rejection/failure tests, requirement coverage,
and evidence through its intended interface. Stability includes lifecycle,
ownership, cancellation, ordering, and bounds where affected. Privacy,
server-side authorization, TLS trust, and bounded input are correctness
requirements; they cannot be skipped as optional security work.

Always review maintainability: clear state/ownership, appropriate boundaries,
consistent project patterns, readable code, useful public tests, and justified
abstractions. Maintain short API/file/test explanations. Test-first is preferred
for behavior changes; when impractical, explain why and provide equivalent
regression evidence. Never weaken expectations to make broken code pass.

The agent chooses extra fuzzing, soak, broad security analysis, profiling, and
benchmarks from actual risk. A parser change needs malformed/fragmented/coalesced
boundary tests; retry work needs lost ACK, reconnect, deduplication, cancellation,
and unauthorized-delivery proof. An unrelated prose edit needs no repository
security audit. There is no blanket coverage percentage or benchmark quota.

Record unavailable/failed checks honestly. Loopback cannot prove R1's physical
computers; hidden native controls cannot prove visual/keyboard usability.
The current manual GUI, native CLI, physical LAN, other-OS, AI, and presentation
gaps remain as recorded in [progress](progress.md).

## Verification commands

Python 3.11+ is sufficient for the standard-library workflow tools. From the
repository root:

```text
python tools/verify.py workflow
python tools/verify.py quick
python tools/verify.py full
python tools/verify.py full --dry-run
```

| Profile | Checks | Intended use |
| --- | --- | --- |
| `workflow` | Skill metadata/links, scoped TOML, workflow-tool regression tests | Skills/tooling-only edits |
| `quick` | Workflow plus complete Debug configure/build/CTest | Iteration, not final C++ acceptance |
| `full` | Workflow; Debug/Release/ASAN builds/tests; CLI/TLS process smoke per preset; clangd; strict Doxygen/Sphinx | Final automated behavior/build-gate acceptance |

On Windows the runner reuses `tools/dev.ps1` to discover Visual Studio/vcpkg
and retain its environment for executable smoke, especially ASAN. Elsewhere
configure the documented CMake/vcpkg toolchain first. Full checks also need
clangd, the existing documentation environment/Doxygen, and certificates from
[TLS setup](tls.md). The runner installs nothing and overwrites no credentials.
Add new feature-specific proof; this command alone cannot prove missing
coverage. Guides changed without C++ also need `python tools/docs.py build`.

Runs retain stage logs and `summary.json` under ignored
`.cache/verification/<UTC timestamp>-<profile>-<id>/`. The summary records Git
revision, dirty status, source fingerprint before/after, commands, durations,
exit codes, and omissions, without file contents or an environment dump.
Existing tests use synthetic data. Local logs may contain machine paths;
review them before sharing.

A failed stage stops the run and leaves later stages marked not run.
Timeout/interruption cleans up the owned process tree. Per-stage timeout
defaults to 1,200 seconds; use `--timeout <seconds>` to change it. Verbose
CTest records actual GoogleTest execution. Acceptance test children clear
inherited GoogleTest filters and sharding; removed variable names are recorded
without their values. Empty, skipped (including Python skips), or disabled
suites cannot pass. A tree changed during verification is incomplete.

Exit codes: `0` for the passing selected scope, `1` for failure, `2` for
partial/incomplete evidence, `130` for interruption. Dry-run returns `0` labeled
`dry_run` and creates no evidence. For genuinely unsupported ASAN, use
`full --skip-asan "specific reason"`; it stays partial and exits `2` even if
other checks pass. Report the limitation against the conditional ASAN requirement;
never conceal a sanitizer failure. A passing `workflow` or `quick` result proves
only that scope.

## Refactoring cadence

After substantial implementation evaluate duplication, tangled state/ownership,
leaky boundaries, awkward testing, and needless indirection. Fix small related
problems while tests are green. Sound structure requires no refactor.

Record broader debt in the existing progress tracker with files, impact, and
a useful trigger. Review accumulated debt at milestone boundaries or before
the final demo. This follows actual changes better than calendar cleanup;
no scheduling is needed. Use `$refactor` for a concrete structural task with
behavior-preserving evidence.

## Parallel work and models

The primary model/effort stays selected in the composer. Local defaults are
Luna/high. Profiles under `.codex/agents/` are `code_mapper` and `implementer`
at high, `test_engineer` and `reviewer` at xhigh. Readers default to read-only;
writers default to workspace-write. Live permissions still apply; scope
instructions remain necessary. The cap is three children plus the primary,
subject to a lower live limit. See
[official subagent configuration](https://learn.chatgpt.com/docs/agent-configuration/subagents).

Ordinary work uses one writer and zero or one reviewer. Swarm usually uses
two independent slices; arena compares two designs. No worker recursively
spawns a team. Give disjoint file ownership, coordinate shared builds, and
inspect results. Skills explain the equivalent role/model contract when named
profiles are unavailable. If delegation or Luna is unavailable, disclose that
limit and work sequentially; never silently escalate the model.

Runnable arena candidates need an identical faithful baseline and isolated
outputs. HEAD omits this checkout's uncommitted/untracked product work.
Default to independent proposals unless isolation can preserve that baseline.
Use the same acceptance tests for executable candidates; proposals are not
tested winners. Integration still needs an implementation request.
