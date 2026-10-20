---
name: verify
description: "Verify Socket Chat acceptance through real APIs, sockets, executables, and strict project gates with reproducible evidence."
---

# Verify

Read [quality](../engineer/references/quality.md),
[testing](../../../docs/testing.md), and the task's acceptance criteria. Map
each claimed behavior to an observable proof. Inspection and mocks cannot
establish real delivery, GUI usability, or persistence through restart.

Use the repository runner from its root:

```text
python tools/verify.py workflow
python tools/verify.py quick
python tools/verify.py full
```

`workflow` validates skill/config/reference artifacts and tests workflow tools;
`quick` adds the complete Debug suite for iteration; `full` adds Release,
ASAN, existing CLI/TLS executable smoke in every preset, clangd, and strict
Doxygen/Sphinx. Use `--dry-run` to inspect commands without executing them.
An ASAN unavailable reason can be recorded with `--skip-asan "reason"` for
`full`; the result remains partial and exits 2. Do not use it to hide a failure.

Add/run task-specific unit/socket/control or executable proof as needed;
the generic runner does not supply missing feature coverage. Existing helpers
use loopback, synthetic text, `:memory:` databases, and owned subprocess cleanup.
Full smoke needs local development certificates; use existing TLS setup and
never overwrite credentials. Use temporary data for new storage experiments.

The runner retains stage logs and `summary.json` under ignored
`.cache/verification/<run>/`. Review failures and evidence. Do not mark a skipped,
zero-test, timed-out, or interrupted run as passing. Reproduce flaky failures
and fix their cause rather than rerunning to obtain one green result.

Report observed commands/outcomes and remaining acceptance gaps. Visible GUI,
native terminal, physical-network, other-OS, and slide checks remain separate
until actually performed. Verification-only requests do not authorize product
fixes or edits to the assignment; report defects unless repair was requested.
