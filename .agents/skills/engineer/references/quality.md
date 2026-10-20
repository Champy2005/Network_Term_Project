# Quality contract

Read [testing](../../../../docs/testing.md), [requirements](../../../../docs/requirements.md),
and the relevant architecture contract. Existing project invariants are
correctness obligations, even when they involve security or resource use.

## Always for behavior changes

- Map every acceptance criterion to an observable public-interface assertion.
  Add success and the relevant boundary/rejection/failure tests. Use literal or
  independently specified expectations. Show that the new regression detects
  the defect/missing behavior where practical; do not assert implementation shape.
- Test intended and unintended delivery for privacy/routing changes. Assert
  the correct payload/identity and bounded quietness of unauthorized receivers.
- Test failure and lifecycle for async work: deadlines, cancellation, pending
  operations, disconnect cleanup, ordering, and bounded queues as applicable.
  Use synchronization and deadlines rather than arbitrary sleep-based passes.
- Run focused tests during work; finish with Debug and Release suites, ASAN
  where supported, clangd for C++, and strict source/guide documentation builds.
  `python tools/verify.py full` aggregates those checks and existing executable
  smoke tests. Add feature-specific proof; the runner does not invent coverage.
- Review correctness, requirement fit, explicit ownership, readable state,
  appropriate boundaries, and the need for any new abstraction. Repair actual
  findings. Never relax expectations to obtain green output.

Docs/skill/tooling-only work uses `python tools/verify.py workflow`, plus
`python tools/docs.py build` for guides; test changed scripts' failure behavior.
Run full when build commands/gates change, as they can invalidate acceptance.

## Choose additional work from the change

| Trigger | Additional evidence worth selecting |
| --- | --- |
| Wire parser, input bounds, schema | Malformed/boundary/fragmented/coalesced input; targeted fuzzing if valuable |
| Identity, routing, TLS, history, AI context | Forged identity, unauthorized access/delivery, trust failure, context isolation |
| Threads, timers, queues, reconnect | Repeated race/cancellation/recovery and resource-retention checks |
| Storage or migrations | Restart, rollback, lock/error injection, no ACK before successful commit |
| Hot loop, serialization, fanout, SQL | Bounded workload baseline and after measurement with the same method |
| AI provider or other external dependency | Deterministic adapter tests, timeout/cancellation/secret handling; live proof when available |

Select heavier fuzzing, soak, performance, or broad security audits only when
the risk or user request warrants them. No blanket coverage percentage or
benchmark threshold. Do not skip authorization or bounds merely by calling
them optional security/performance work. Record a real reason for selected or
omitted substantial checks, not a checklist for every tiny edit.

## Refactoring cadence

Evaluate after each substantial implementation: did it introduce duplication,
unclear ownership, hidden state, leaky layers, awkward testing, or needless
indirection? Fix small, directly related problems while tests are green. A
sound structure needs no refactor. Record larger debt with affected files,
impact, and a suggested trigger in `docs/progress.md`.

At a completed milestone or before the final demo, review accumulated debt and
the frequently changed areas. A calendar job is unnecessary unless the user
requests one. Broad rewrites remain a separate scoped task. Preserve useful
Doxygen/API/test comments; prose cleanup must not delete contract documentation.

## Evidence and completion

Record commands, outcomes, artifact paths, and limitations for the current
working tree. A skipped, empty, interrupted, or inconclusive check is not a
pass. Fix failures and rerun; do not retry until luck hides a flaky result.
After repeated repairs fail the same acceptance condition, revisit the design.
If new information or unavailable equipment is essential, report the smallest
remaining dependency and the work already verified.

Local socket/native-control tests do not prove a visible GUI walkthrough,
native console editing/Ctrl+C, another OS, physical LAN, or final slides.
Keep their existing pending status. Use synthetic chat and temporary databases;
retain evidence under ignored `.cache/verification/`, never real chat or secrets.
