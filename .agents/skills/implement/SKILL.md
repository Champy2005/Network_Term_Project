---
name: implement
description: "Implement a scoped Socket Chat feature, fix, or local specification in tested vertical slices."
---

# Implement

Read root `AGENTS.md`, the relevant public contract, and
[quality](../engineer/references/quality.md). If scope is already clear, act on
it; a ticket tracker, extra specification, and approval of test boundaries are
not prerequisites. Read local specs when supplied; fetch external tickets only
when requested and tools are available.

1. Identify acceptance criteria and existing public test boundaries from the
   requirement and callers. State the chosen state/data shape and file ownership.
2. For a behavior change, add one meaningful failing test, confirm its failure
   is caused by missing behavior, implement that slice, and rerun. Repeat for
   important success, rejection, and failure criteria. If test-first cannot be
   used, state why and demonstrate equivalent behavioral regression evidence.
3. Keep test expectations independent of the implementation. Prefer real
   sockets for delivery/authorization; use a controllable peer or external
   adapter for timeout/malformed-provider behavior. Do not mock away routing.
4. Keep the writer scope coherent, reuse established patterns, and preserve
   ownership, buffering, frame, protocol-tag, and privacy contracts.
5. Run focused checks, then applicable full gates, review the final artifact,
   and repair/reverify. Evaluate small related refactors while green; defer
   broad cleanup using the shared cadence. Update affected project docs.

Return the change and acceptance evidence with limits. Do not automatically
commit. Loading this phase from `$engineer` does not start a second coordinator.
