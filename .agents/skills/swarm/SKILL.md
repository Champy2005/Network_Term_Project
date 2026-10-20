---
name: swarm
description: "Use bounded parallel agents for independent Socket Chat investigation, test design, or review slices, then return one evidence-based result."
---

# Swarm

Follow [delegation](../engineer/references/delegation.md). This workflow requests
subagents when permitted by the active runtime. Default to two distinct slices
and read-only work; do not spawn agents simply to fill the cap.

1. **Frame:** state the outcome, coverage slices, artifact/baseline identity,
   write ownership, and what evidence each slice must return.
2. **Fan out:** give each child a complete scoped brief and Luna role/effort.
   Partition questions or disjoint output files. Shared protocol/build/docs
   changes remain with one owner; children do not fan out recursively.
3. **Aggregate:** wait, inspect every report/artifact, reconcile disagreements,
   and check that all required slices have evidence. Dropouts remain gaps.
4. **Report:** return one concise result with findings, proof, and unresolved
   coverage. If code was authorized and changed, integrate with one writer and
   run the same final gates as [engineer](../engineer/SKILL.md).

Without delegation tools, perform the slices sequentially and say so. Do not
create new user-owned chats, silently switch models, or infer agreement from
two agents that merely repeated the same unsupported assumption.
