# Refactoring

1. Name the concrete maintenance problem and the behavior that must remain.
   Run existing contract tests; add characterization only where coverage is
   missing. Compiler success alone does not establish equivalence.
2. Define a simpler target shape and identify every caller. Prefer less hidden
   state, clearer ownership, fewer branches, and a smaller useful public API.
3. Move in small steps while preserving the tests. Migrate internal callers
   together; preserve published wire tags and documented public compatibility.
   Separate any discovered behavior change from the refactor's acceptance.
4. Exercise the same public paths before/after, run applicable gates, and review
   error behavior and lifecycle. Keep a change only if its maintenance benefit
   is concrete; update the existing architecture/API docs where needed.
