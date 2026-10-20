---
name: review
description: "Review a Socket Chat diff for concrete correctness, requirement, stability, testing, and maintainability defects without editing it."
---

# Review

Read root guidance, the task request, and
[quality](../engineer/references/quality.md). Review the exact task artifact,
including named untracked files. In a dirty checkout distinguish task changes
from earlier work using the task-start baseline; `git diff HEAD` alone may
omit untracked changes and include unrelated implementation.

Trace public callers, state transitions, error paths, and tests. Always assess
requirement fit, delivery/privacy, ownership/cancellation, buffering, stability,
and readability where relevant. Select deeper security/performance scrutiny
when the changed trust boundary or workload justifies it; do not launch a
whole-repository audit by default. Compare claimed proof with what tests
actually observe. Evaluate refactoring without imposing a rewrite.

For substantial protocol/concurrency/storage/auth changes, request one
independent read-only Luna reviewer if delegation is available/authorized,
following [delegation](../engineer/references/delegation.md). Otherwise perform
a separate review pass and disclose its lack of independence.

Return actionable findings in priority order, each with file/line, a concrete
trigger, impact, and useful verification. Distinguish defects from optional
suggestions and evidence gaps. State when no actionable finding exists; never
invent a quota of findings. This skill stays read-only. Within `$engineer`,
the coordinator handles repairs and reruns the affected checks.
