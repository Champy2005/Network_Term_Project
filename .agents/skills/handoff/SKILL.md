---
name: handoff
description: "Write a compact Socket Chat handoff with current task scope, working-tree ownership, evidence, gaps, and the next action."
---

# Handoff

Create a concise record under `.cache/workflow/handoff.md` or the user's
requested path. Include the request and later corrections, baseline/uncommitted
work ownership, relevant paths/contracts, completed work and observed test
commands/artifacts, remaining acceptance gaps, and the next concrete action.

Reference existing requirements, architecture, plan, and progress rather than
duplicating them. Name only useful skills for the next task. Never include
credentials, tokens, real chat, or a full environment dump. Do not present
earlier tests as evidence for a later changed working tree.

Return the handoff path and how to resume. Writing a handoff does not authorize
messaging another chat, creating one, committing, or marking unfinished work done.
