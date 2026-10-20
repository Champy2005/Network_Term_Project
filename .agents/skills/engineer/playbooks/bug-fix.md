# Bug fix

1. Reproduce the symptom through its CLI, GUI, socket, or public API surface.
   Preserve a failing regression test when practical. If reproduction is
   unavailable, record the precise gap and seek the nearest faithful trigger.
2. Trace competing explanations to evidence. Use targeted instrumentation with
   synthetic data; do not ship guesses or unrelated defensive code.
3. Fix the cause in the owning layer. A contract change is a feature decision,
   not a way to silence the failing test.
4. Rerun the original reproduction and regression tests, then applicable full
   gates. Review neighboring error, privacy, ownership, and cancellation paths.
5. Remove temporary instrumentation. Report the cause, fix, failing-to-passing
   evidence, and any surface that could not be exercised.
