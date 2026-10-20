---
name: arena
description: "Compare two independent designs or isolated prototypes for a contested Socket Chat change, synthesize the strongest solution, and verify it."
---

# Arena

Use [delegation](../engineer/references/delegation.md). This workflow requests
two independent candidates when permitted. It compares solutions, not model
brands; Luna remains the worker default. Use it only for a consequential fork.

1. **Frame:** define the common task, acceptance tests, and rubric: correctness
   and compatibility first, then failure/lifecycle behavior and maintenance;
   measure performance only if it matters. Both candidates get the same contract.
2. **Fan out:** request two design proposals by default, each in its own output
   artifact. If runnable prototypes are authorized, use isolated workspaces with
   the same exact baseline, including relevant uncommitted/untracked work.
   A fresh HEAD worktree alone does not contain this project's working tree.
   Do not reset, commit, or stash human work to create a baseline.
3. **Cross-judge and pick:** after candidates finish, read both end to end and
   compare each criterion against evidence. For substantial runnable alternatives
   use an independent read-only reviewer when available; wait until candidates
   finish so the configured cap is respected. A proposal without execution is
   design evidence only. Reject candidates that violate acceptance criteria.
4. **Graft:** have one writer combine only useful ideas into a coherent design.
   Record the chosen base, borrowed ideas, and rejections; do not paste both
   implementations together. Selection alone does not authorize integration.
5. **Verify:** run common tests against each executable candidate when compared,
   then reverify the synthesized result with applicable full gates and review.

If no faithful isolated baseline or delegation is available, compare proposals
sequentially and disclose that limit. Do not claim a tested winner. Preserve
candidate evidence and clean up only disposable resources created for the run.
