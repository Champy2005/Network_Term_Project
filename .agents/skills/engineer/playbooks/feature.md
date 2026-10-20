# Feature

1. Identify the existing requirement or explicit user behavior and its public
   test surface. Write success, important rejection, and failure/recovery criteria.
2. Map callers and select the data/state shape before editing. For a meaningful
   boundary change, compare a small number of designs with
   [architect](../../architect/SKILL.md). Ordinary local choices need no debate.
3. Decompose only where useful: blocking prerequisites, independent slices,
   shared mutable state, and the smallest safe writing scope. If one writer is
   best, proceed with one writer. Do not introduce parallelism for ceremony.
4. Build one vertical slice at a time through
   [implement](../../implement/SKILL.md), with independently derived expectations.
5. Verify the intended entry point and the relevant failure paths. Review the
   resulting diff, repair findings, and reverify changed areas and final gates.
6. Evaluate maintainability; record behavior/limits/evidence in existing docs.
   Report which acceptance criteria passed and which remain unproved.
