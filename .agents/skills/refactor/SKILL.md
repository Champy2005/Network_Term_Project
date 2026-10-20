---
name: refactor
description: "Perform a scoped, behavior-preserving Socket Chat refactor, or evaluate whether maintenance problems justify one."
---

# Refactor

Read [refactoring](../engineer/playbooks/refactoring.md) and
[quality](../engineer/references/quality.md). If the user asks for assessment,
stay read-only and rank concrete problems with files, impact, and scope. If
implementation is requested, pin observable behavior before changing structure.

Prefer the recently changed area or specified maintenance problem. Improve
ownership, domain state, boundaries, duplication, or public-test accessibility;
do not equate more classes, fewer lines, or a named pattern with better design.
Preserve public compatibility and async/protocol/privacy contracts.

Move in verified steps; inspect callers and docs, then run applicable full
gates. Separate behavior changes from equivalence claims. Explain the actual
maintenance benefit and proof of preserved behavior. A short post-feature
evaluation may conclude that no refactor is warranted. Broader cleanup is a
milestone task, not a required rewrite after every implementation.
