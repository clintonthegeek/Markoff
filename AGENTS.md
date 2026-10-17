# Agents working in Markoff

The CollabText adoption board is closed. There is no task marked **Now**.
The readiness signal is
[`docs/handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md`](docs/handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md).
For a later change in this repository, read these in order, then edit.

1. [`docs/STATUS.md`](docs/STATUS.md) — what is current. One screen.
2. The closed record on
   [`docs/plans/2026-10-12-collabtext-adoption-execution.md`](docs/plans/2026-10-12-collabtext-adoption-execution.md)
   when the change touches an adoption behavior.
3. The spec for that behavior. Byte layout and provider call meaning live
   in `docs/specs/`. The task board wins when they disagree about whether
   work is still open.
4. [`docs/EDITING-MANIFESTO.md`](docs/EDITING-MANIFESTO.md) when the change
   affects identity, undo, recovery, or what a person sees.

Do not start by reading the archive, the adoption-plan diary, `docs/queue.md`,
or closed arc plans. Those explain history. They will not tell you the next
edit.

## What this repository is

A Qt6/C++ markdown editor family. `markoff-core` holds the provider-backed
document. Canvas is the primary editor. Source and Styled are the other
supported views. Corbomite is the host and submodules this repo at
`libs/markoff-family`. The A16 readiness signal has been sent. Corbomite
may begin its transition. Do not edit the Corbomite checkout from this
repository.

The supported path is the installed CollabText alpha.5 candidate named in
`docs/STATUS.md`. There is no second engine, no compatibility shim, and no
deprecation window. Port in-repo consumers and delete obsolete branches.

## Session routine

1. Name the task id and the command you will use to show the slice worked.
2. Change only that task. Preserve exact accepted bytes, identities, and
   durable recovery. Keep physical collection and log pruning disabled.
3. Build with at most four jobs:

   ```sh
   cmake --build build-dev -j 4
   ```

   Run the focused offscreen test. For a public header, CMake, or document
   behavior change, also run:

   ```sh
   scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
   ```

   Offscreen is the default. Do not use `--direct` unless the user asked
   for that run. When a test fails, decide whether the contract drifted or
   the code is wrong before editing the test.
4. Update the task row and `docs/STATUS.md` in the same change: date,
   command, result, next step. A few lines. Long narrative goes in
   `docs/archive/collabtext-adoption/`, or nowhere.
5. Commit. Subject names the task, for example
   `collabtext(A13): remove the unused legacy document branch`.
   One task per commit. Leave build directories and machine prefixes
   untracked.

A header that compiles, a stub, or a passing count from an older revision
does not close a task. Record the revision you ran.

## Boundaries

- The readiness signal is
  `docs/handoff/2026-10-17-to-corbomite-ready-for-collabtext-transition.md`.
  Do not edit the Corbomite checkout from this repository. Host storage,
  lease integration, and production network acceptance are Corbomite's
  work after that signal.
- Do not swap the CollabText candidate for a local checkout. A Core behavior
  change needs a new immutable tag and a new acceptance record.
- Candidate and reference-provider tests do not prove Corbomite's journal
  or a real network. Keep those gates named and unclosed.
- The live leaf and the vendored CollabText submodule are gone. Do not
  restore them. `markoff-source` stays untouched except for a bug the current
  task owns. `markoff-styled` is bug-fix only outside the migration.
- Library guides (`libs/*/CLAUDE.md`), `docs/INVARIANTS.md`, and
  `docs/VIEW-IMPLEMENTORS-GUIDE.md` apply when you touch those leaves.
  Canvas follows its constitution in the canvas spec.

## Finish line

A16 sent the readiness signal on 2026-10-17. The adoption board has no
open task. A13, A10, A12, A14, and A15 are recorded on that board. Host
journal, lease, application wiring, and production acceptance follow in
Corbomite. Wave Q06 stays a separate release obligation.
