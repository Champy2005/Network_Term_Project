---
name: diagnose
description: "Investigate a Socket Chat runtime bug through faithful reproduction, competing hypotheses, and ownership/path evidence."
---

# Diagnose

Read [bug fix](../engineer/playbooks/bug-fix.md). Match the actual surface:
socket/API, persistent CLI, native controls, or visible GUI. Prefer existing
tests/helpers and synthetic data with disposable storage.

Reproduce first, identify the path and relevant ownership/state, then eliminate
hypotheses with observations. Inspect timeout, partial/coalesced reads, pending
writes, disconnect cleanup, and recovery when implicated. Avoid speculative
changes or logging real text/capabilities. Preserve the reproduction evidence.

If the user requested diagnosis only, return cause/evidence or precise remaining
unknowns without product edits. If a fix is authorized, hand the established
cause to [implement](../implement/SKILL.md), rerun the original reproduction,
and complete the shared quality gates. Missing equipment or an unreachable
surface is a limitation, not proof that the bug is absent.
