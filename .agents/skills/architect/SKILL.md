---
name: architect
description: "Choose a maintainable Socket Chat design for a meaningful API, state, ownership, protocol, or module-boundary change."
---

# Architect

Read root guidance and the affected architecture/callers. Map today's data
flow and invariants before proposing a new shape. Trace server authority,
socket writers, thread/strand ownership, cancellation, queue bounds, and the
client/model/GUI boundary wherever affected.

Compare the existing pattern and one or two credible alternatives. Evaluate
requirement fit, states that can be represented, failure paths, migration and
public compatibility, testing, reader effort, and actual maintenance cost.
Use a small diagram or tradeoff table only when it makes the choice clearer.
An abstraction earns its place through a concrete need; do not retrofit a
design pattern or rewrite the stack for style.

Recommend one design with affected interfaces, tests, and a small sequence
of verifiable steps. Explain when keeping the current design is best. Use
[arena](../arena/SKILL.md) only if competing approaches could resolve a material
uncertainty. A design-only request produces a proposal, not application edits;
within an authorized implementation, proceed with routine technical choices.
