# Team workflow

This is a staged course project. The team should finish and verify one
checkpoint at a time, then update [progress](progress.md). The
[plan](plan.md) states each stage's exit criteria; the
[requirements checklist](requirements.md) defines the graded outcome. Setup
instructions for each operating system are linked from the
[documentation index](README.md). Coding agents should also read the root
[AGENTS.md](../AGENTS.md).

## Suggested workstreams

These are **roles to assign**, not current member assignments. A network-core
owner can handle the server, transport, protocol handlers, persistence, and
wire-level privacy tests. A client/GUI owner can handle the reusable client API,
room views, and interaction tests. An AI/presentation owner can research and
implement the required AI feature, gather code snapshots, and rehearse the
two-computer demonstration. All roles should review integration behavior
together; a GUI check cannot substitute for server-side authorization tests.
Record the actual owner for each task in the [progress task board](progress.md).

## Making a change

1. Pick a small task from the current stage. Agree on file ownership before
   parallel edits so two people do not rewrite the same files.
2. Read the current implementation and its tests. State the invariant being
   added: for example, “only a registered session may send a direct message.”
3. Change the smallest coherent part, add a meaningful test, and build it.
   Keep application handlers, transport framing, and UI logic in their own
   layers.
4. Run the relevant unit or integration tests, then the full CTest suite.
   Use Debug and Release builds; run ASAN where the toolchain supports it.
5. Review the diff for correctness, naming, error paths, and accidental
   secrets. Update architecture, testing, progress, and requirement evidence
   when behavior changes.
6. Record limitations honestly. Commit a working checkpoint with a focused
   message when the human requests it; coding agents do not automatically commit.

The [engineering workflow](workflow.md) provides local Codex skills and a
verification runner for these steps. Evaluate maintainability after substantial
changes, fix related small problems, and review broader debt at milestones.

Use the repository's `.clang-format` style and keep compiler warnings enabled.
Both editors share `.clangd` and the Debug compilation database. Follow the
[tooling guide](setup/tooling.md) for short API/test comments and documentation
builds. Run `python tools/check_clangd.py` after C++ changes and
`python tools/docs.py build` after comment/guide changes.
Prefer small functions, explicit error handling, and clear ownership of async
operations. A session must not outlive work that still references it, and each
socket must have one ordered write path. The server must derive identity from
its connection state and check group membership itself. These are design
invariants, not client-side UI checks.

## Protocol changes

`proto/chat.proto` is the shared contract between future client and server.
Never reuse or renumber a published Protobuf field tag. Add new fields or
payload variants with distinct tags, update `Envelope.protocol_version` rules
if compatibility changes, and document message direction and validation in
[architecture](architecture.md). Add serialization and malformed-input tests.
Keep frame size and EOF behavior consistent across client and server.

When a future stage adds persistence, define the durability point for an ACK
before accepting messages. When it adds reconnect, state the retry and
duplicate-handling policy. Do not call the system “exactly once” without a
proven protocol and tests that establish that property.

## Definition of done

A task or stage is done when:

- The behavior works through its intended public interface, not just a stub or
  isolated helper.
- New tests cover the success path and the important failure or privacy path.
- Existing tests pass; Debug and Release builds pass; ASAN passes where
  available in the documented developer environment.
- User-visible changes work manually in the CLI or GUI at the appropriate
  stage. A physical-network requirement is verified on separate computers,
  rather than inferred from loopback tests.
- Docs accurately describe implemented behavior, tests, limits, and pending
  work. The progress tracker and requirement evidence are updated.
- No private keys, credentials, build outputs, or machine-specific paths are
  committed.

For the final graded submission, “done” also means the two-computer demo
works and the implementation-explanation slides include code snapshots from
the final code. The slide template provides space for generative AI prompts
when used; keep a record of the prompts and review any generated code before
presenting it.
