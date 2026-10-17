# Instructions for agents working in Markoff

Start with [`docs/STATUS.md`](docs/STATUS.md) for the current state, then use
[`docs/plans/2026-10-12-collabtext-adoption-execution.md`](docs/plans/2026-10-12-collabtext-adoption-execution.md)
as the authoritative A01–A16 task list. The dated adoption design plan is
background, not a second status source. Keep Corbomite paused until A16 is
complete; do not treat historical evidence as current verification.

The supported product path is the provider-backed CollabText vNext integration.
There are no legacy installs to preserve, no legacy API consumers, and no
deprecation window is required. Port all in-repository consumers and remove
obsolete API/build branches directly. Do not add compatibility shims or restore
a legacy build mode unless the user changes this direction.

Before changing provider, identity, persistence, undo, or transport behavior,
read the applicable contracts in `docs/specs/` and the implementation invariants
in `docs/INVARIANTS.md`. Preserve exact accepted bytes/identities and durable
recovery semantics. Candidate/reference-provider tests do not establish
production-host storage or real network acceptance; keep those gates explicit.

Use the exact CollabText alpha.5 package tag and commit recorded in the live
status board. Never substitute an arbitrary local CollabText checkout. The old
temporary package prefix may have been removed; rebuild/install the pinned
candidate package in a workspace-local directory and point CMake at it. Do not
commit generated build directories or machine-specific prefixes.

After source changes, run the narrow relevant tests and the configured full
build/test when practical. Record the exact command, result, dependency prefix,
and date in the execution plan. If a build or test is interrupted or fails,
label evidence incomplete and state the observed cause; do not carry forward an
older passing count as proof for new sources.

Keep current status concise. Put active decisions and remaining gates in
`docs/STATUS.md` and the A01–A16 table; move superseded progress narratives to
`docs/STATUS-LOG.md` or `docs/archive/`. When finishing a task, update both the
task row and the live status so a new agent can resume without reconstructing
conversation history.
