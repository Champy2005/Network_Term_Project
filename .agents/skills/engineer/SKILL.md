---
name: engineer
description: "Route a substantial Socket Chat feature, bug fix, refactor, or investigation through project-specific design, implementation, verification, and review."
---

# Engineer

Own the requested outcome. Use pstack's task-specific playbook structure;
keep one coordinator and one coherent writer for coupled code. Read root
`AGENTS.md` and the five project documents it names before changing behavior.
The assignment PDFs and existing R1-R11 matrix remain authoritative.

1. Capture the task-start Git status, including untracked work. Define observable
   acceptance criteria and the public interfaces that prove them. Classify the
   task and read **only** its playbook below.
2. Inspect the current execution path and tests. State the data/state shape,
   ownership, failure behavior, and smallest useful change. Resolve observable
   questions from code or experiments; use [grill](../grill/SKILL.md) only for
   unresolved product decisions.
3. Apply [quality](references/quality.md). Correctness, requirements, stability,
   and maintainability are mandatory. Choose additional analysis from real risk
   and give a short reason for consequential omissions.
4. Implement in verifiable slices. Use [implement](../implement/SKILL.md) for
   behavior changes. Parallel work is optional: follow
   [delegation](references/delegation.md) when independent work earns its cost.
5. Verify the actual user/API path, inspect the final diff, repair supported
   findings, and rerun affected checks. Use [verify](../verify/SKILL.md) and
   [review](../review/SKILL.md); passing existing tests alone is insufficient
   evidence for a new acceptance criterion.
6. Evaluate refactoring, update only affected docs, and reconcile acceptance
   against observed evidence. Keep manual/physical checks pending until done.
   Report changes, verification, remaining limits, and useful deferred cleanup.

| Task | Playbook |
| --- | --- |
| New or changed behavior | [Feature](playbooks/feature.md) |
| Reproducible defect | [Bug fix](playbooks/bug-fix.md) |
| Behavior-preserving structure change | [Refactoring](playbooks/refactoring.md) |
| Explanation or investigation | [Investigation](playbooks/investigation.md) |

Small edits need a small plan. For a multi-session task, keep a short acceptance
and evidence record using [this template](references/task-template.md).
Do not load the entire skill collection, create a tracker/glossary hierarchy,
or start recurring jobs merely to follow the workflow. Preserve the human's
selected primary model; use Luna workers as described in delegation. Commit,
push, merge, deploy, and coursework submission require an explicit user request.
