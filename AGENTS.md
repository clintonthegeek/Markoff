# Agents working in Markoff

You are here to advance one task on the CollabText adoption board. The
documents below are the whole orientation. Read them in this order, then edit.

1. [`docs/STATUS.md`](docs/STATUS.md) — what is current. One screen.
2. The task marked **Now** in
   [`docs/plans/2026-10-12-collabtext-adoption-execution.md`](docs/plans/2026-10-12-collabtext-adoption-execution.md).
3. The spec that task names, if it names one. Byte layout and provider call
   meaning live in `docs/specs/`. The task board wins when they disagree
   about whether work is still open.
4. [`docs/EDITING-MANIFESTO.md`](docs/EDITING-MANIFESTO.md) when the change
   affects identity, undo, recovery, or what a person sees.

Do not start by reading the archive, the adoption-plan diary, `docs/queue.md`,
or closed arc plans. Those explain history. They will not tell you the next
edit.

## What this repository is

A Qt6/C++ markdown editor family. `markoff-core` holds the provider-backed
document. Canvas is the primary editor. Source and Styled are the other
supported views. Corbomite is the host and submodules this repo at
`libs/markoff-family`. Corbomite development is paused until task A16.

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

- Do not message Corbomite, and do not write the A16 handoff, before A15
  is done. Host storage and production network acceptance follow the signal.
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

A16 is the readiness signal. Before that, Markoff needs a single clean
engine tree (A13), bounded history retirement (A10), ephemeral presence
(A12), a signed acceptance record (A14), and a transition package a
Corbomite agent can implement against (A15). The task board has the exit
for each.
