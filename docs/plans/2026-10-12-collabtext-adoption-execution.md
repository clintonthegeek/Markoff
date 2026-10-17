# CollabText adoption — task board

Updated 2026-10-17. This is the task list. Keep it short enough to read
before editing. The day-by-day diary through 2026-10-16 is archived at
[`../archive/collabtext-adoption/2026-10-16-execution-progress-log.md`](../archive/collabtext-adoption/2026-10-16-execution-progress-log.md).
Design rationale, not status:
[`2026-10-11-collabtext-vnext-adoption.md`](2026-10-11-collabtext-vnext-adoption.md).

## Picture

Markoff's document and its Source, Styled, and Canvas views already run on
one provider-backed CollabText engine. Root CMake requires the installed
alpha.5 candidate for standalone and embedded builds. The reference provider,
record codecs, Markdown import and replacement, revision-bound export,
selective cross-stream undo, scoped positions, and in-memory remote routing
with receipts are in the tree.

The remaining Markoff work is a short tail:

1. **A13, now.** Remove retired engine branches from the libraries the root
   build compiles, so the next agent does not port code the build no longer uses.
2. **A10, next.** Give local history an explicit retirement bound and enforce
   it when a new action would exceed that bound.
3. **A12.** Carry remote carets as ephemeral presence, separate from document history.
4. **A11 with A15.** Turn the reference provider's outbox, quarantine, and
   receipt behavior into checks a host provider must pass, and assemble the
   transition package.
5. **A14.** Re-run acceptance on the sources you are finishing and record it here.
6. **A16.** Signal Corbomite. That signal is the last task, and nothing before
   it invites Corbomite to start.

## Standing rules

- Corbomite development stays paused until A16. Work in this repository with
  the reference provider and host-shaped fixtures. Do not send a readiness note
  early. Corbomite's journal, lease, and application wiring are its work after
  the signal. Wave's Q06 acceptance and any CollabText merge or publication
  stay separate release obligations.
- There is no legacy install to preserve. Each Markoff record family has one
  writer version. Readers refuse other versions, with two recovery exceptions
  already in the tree: reference-provider files write `MREF/5` and still read
  `MREF/4`; quarantine writes `MQR/2` and still reads `MQR/1`. Markdown is the
  import/export format. Accepted actions are the recovery history.
- Keep D2: one text stream per block, one structural IdList, Markoff-owned
  maps, and Markoff-owned cross-stream actions. Physical collection, log
  pruning, and epoch transitions stay disabled.
- Candidate, immutable: tag `collabtext-markoff-core-2026-10-13-alpha5`,
  commit `e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, package `0.2.0-alpha.5`,
  manifest v17. A behavior change in Core needs a new tag and a new acceptance
  record. Do not point CMake at an arbitrary CollabText checkout.
- The machine-local prefix last used was `/tmp/collabtext-alpha5-prefix-b`.
  Recreate it from that candidate's `docs/USING_VNEXT.md` when it is missing.
  Do not commit prefixes or build directories.
- Classify a failing test before changing it. A product-behavior change needs
  an explicit decision recorded in the task.

## Session routine

Do this every time you start in this directory.

1. Read [`docs/STATUS.md`](../STATUS.md) and the task marked **Now** below.
   Open the archive diary only when this board cites a decision in it.
2. Work on that one task. Leave the later tasks alone.
3. Before editing, name the task id and the command that will show the slice worked.
4. Build with at most four jobs: `cmake --build build-dev -j 4`.
   Run the focused offscreen test for the slice. When the change touches
   public headers, CMake, or document behavior, also run:

   ```sh
   scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
   ```

   Never use `--direct` without an explicit user request for that task.
5. In the same change, update this board and `docs/STATUS.md`: date, commands,
   pass/fail counts, and the next concrete step. Keep the note to a few lines.
   Put a long evidence write-up in the archive log, or leave it in the commit.
6. Commit the slice. The message names the task, for example
   `collabtext(A13): remove the unused legacy document branch`.
   One task per commit. Do not commit generated build output.

A code stub, a compiling header, or an old passing count is not a closed task.
Record the revision you actually tested.

## Build

```sh
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_PREFIX_PATH=/tmp/collabtext-alpha5-prefix-b
cmake --build build-dev -j 4
```

The root build requires that prefix. There is no pinned-engine switch and no
Live switch.

Contracts to read when the task touches them:

- [Persistence formats](../specs/2026-10-11-collabtext-vnext-persistence.md) — byte layouts.
- [Provider contract](../specs/2026-10-12-document-storage-provider.md) and
  `libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h`.
- [Shared Editing Manifesto](../EDITING-MANIFESTO.md).
- [Provider reply to Corbomite](../handoff/2026-10-12-to-corbomite-provider-contract.md),
  as the host's accepted direction. Corbomite stays paused.

If a spec's opening status disagrees with this board about whether a task is
open, this board wins for task status. The spec wins for byte layout and
call meaning.

## Last recorded evidence

2026-10-16, recorded in the archive log: a fresh candidate Debug build and
offscreen CTest passed 93/93. The installed Core consumer and the
Source/Styled/Canvas consumer passed 2/2. The install omits `TextAnchor.h`,
`Cursor.h`, and `AnchorJson.h`. Treat that count as the baseline at
`6fbe26f0`. Any later source change needs a new run.

2026-10-17 code check of the done rows, by reading the current sources. The
suite was not re-run. A01–A09 stay done. What the tree actually contains:

- `MarkoffDocument::open` is the only constructor. Edits return
  `ActionPublicationResult`. `providerStateChanged` updates Canvas
  `publicationPending`, and that flag gates `isReadOnly()`.
- The reference provider writes `MREF/5`, reads `MREF/4` and `MREF/5`, takes
  a `QLockFile` lease, reserves counters on the session, and has injected
  rename and sync faults. `tst_document_storage_provider.cpp` starts a child
  process for reopen.
- Text allocation goes through `ScopedTextStream::create(session)` and
  `session.reserve`. `ReplicaState` is only the provider's private identity
  file.
- `undoForBlock` publishes `undoGroup` for that block's action.
  `tst_provider_backed_document.cpp` undoes block A and checks block B still
  reads `Block B B` after reopen. `MarkoffDocumentVNext.cpp` does not emit
  `RetiredHistoryAssociation`. That remains A10.
- `ActionBundleCodec` accepts MAB version 3 only. Cross-stream history accepts
  version 3 only. The publication coordinator's `recoverAndRebuild` and
  `publishGap` are what the document lifecycle calls.
- `BlockId` is a 16-byte actor plus counter. Root `CMakeLists.txt` requires
  `find_package(collabtext CONFIG REQUIRED COMPONENTS core)` and rejects the
  pinned-engine and Live switches. Core, Source, Styled, and Canvas have
  install rules. The compiled document is `MarkoffDocumentVNext.cpp`.
- Block edits, merge, split, `changeBlockKind`, `swapBlockPayload`,
  `pasteMarkdownReplacingRange`, and `FormatOpsVNext` are the editing path.
  `tst_format_ops_vnext` is the format test the current CMake registers.
- Import, `replaceMarkdown`, and `save()` (revision-bound `QSaveFile`, then
  `acknowledgeExport`) are implemented. Untouched setext, blockquote, CRLF,
  trailing whitespace, and missing-final-newline bytes are asserted in
  `tst_provider_backed_document.cpp`, then again after reopen. The older
  `tst_block_buffer_invariant` target is inside the CMake branch the root
  build does not take.

## Tasks

| Task | State | Agent action |
| --- | --- | --- |
| A01 | Done | Provider lifecycle and fallible edits are what `MarkoffDocument` uses. Host chrome and host storage are A15/A16. |
| A02 | Done | Reference provider: leases, reservations, reopen, injected sync faults. |
| A03 | Done | Provider-backed text, map, and list allocation. |
| A04 | Done | Selective undo of action A while action B remains. Retirement is A10. |
| A05 | Done | Canonical codecs. Do not add a second version of a record family. |
| A06 | Done | Local publication and recovery coordinator. Remote delivery stays A11/A12. |
| A07 | Done | Installed Core/Source/Styled/Canvas on composite `BlockId`. |
| A08 | Done | Provider-backed block, map, structural, paste, and format edits. A missing command is a new bug with a test. |
| A09 | Done | Import, replacement, reopen, and revision-bound export. |
| A10 | Next | History retirement and backpressure. Starts when A13 is done. |
| A11 | Later | Host-provider conformance checks. Fold them into A15. |
| A12 | Later | Ephemeral remote presence. |
| A13 | **Now** | Delete dead legacy branches in the libraries the root build compiles. |
| A14 | Later | Acceptance record on the sources you are finishing. |
| A15 | Later | Transition package for a Corbomite agent. |
| A16 | Later | Readiness signal. Requires A13–A15 done. |

Done means the recorded behavior is in the tree. Reopen a done task only when
a current test fails or a named command on its exit list is missing. Say which.

### A13 — remove dead legacy source

The single-engine cutover left inactive branches, tests, and comments that
still describe the pinned engine, numeric replica ids, and the deleted anchor
types (`TextAnchor`, `Cursor`, `AnchorJson`, `AnchorConversion`).

**Do this.** In the libraries the root build compiles (`markoff-core`,
`markoff-parser`, `markoff-source`, `markoff-styled`, `markoff-canvas`,
`markoff-collabtext-vnext`), delete source that the candidate build does not
compile, and delete references to those removed types. The `else()` branches
of `MARKOFF_USE_COLLABTEXT_VNEXT` still contain tests that call removed APIs
such as `d2UndoLog()` and `MarkoffDocument(replicaId)`. Those files are not
part of the current suite. Search for the old engine switch and the deleted
type names after each batch. Build and run the focused tests, then the
offscreen suite excluding `tst_realistic` and `tst_benchmark`.

Leave `libs/markoff-live` and the vendored CollabText tree in place. Both are
already outside the supported root build. Deleting either is a separate
explicit cleanup.

**Exit.** The supported build has one document implementation. A search of
those libraries finds no editable legacy engine branch and no use of the
deleted anchor types. The offscreen suite passes, and this board records the
command and count.

### A10 — history retirement and backpressure

Selective undo, redo, reopen, and compensation failure rollback are already
covered. `MarkoffDocument` restores local ancestry and does not emit
`RetiredHistoryAssociation` records. The MAI/MAH codecs can validate a
retirement. Nothing in the document decides when a group becomes eligible.

**Do this.** Read the retirement rules in the persistence spec. Choose an
explicit per-actor undo depth, write that number into the spec and into this
task, and only then implement it. When a newly committed action would exceed
the depth, that same action records retirement of the oldest undoable group,
and the retired group can no longer be undone. If the retirement record cannot
be attached, the new action does not commit. Add a regression for the bound,
for refusal when retirement cannot be recorded, and for reopen after a
retirement. Do not retire history from save, export, or garbage collection.

**Exit.** A test crosses the recorded depth, proves the oldest group is gone
after reopen, proves a younger group still undoes, and proves a failed
retirement leaves the previous accepted state in place. A04 stays closed.

### A12 — ephemeral presence

Canvas can rebase and paint a remote caret when a test supplies a scoped
position, including detachment when the block disappears. There is no
production path that carries one peer's caret to the other.

**Do this.** Send presence as an ephemeral message on the existing
collaboration consumer. Dropping it loses a caret and leaves document bytes,
block identities, undo, and receipts unchanged. Cover two replicas: a caret
tracks a peer edit, and a removed block drops the caret. Do not store carets
in accepted MAB history.

**Exit.** The two-replica test passes without a new document action for the
caret update. Session folds and scoped selections stay on their current path.

### A11 — provider conformance, done inside A15

Reference-provider routing already accepts remote MABs, bounds deferred
actions, persists quarantine, returns receipts after projection acceptance,
and retries the outbox on reconnect. A real network stack is the host's job
after A16.

**Do this, when you reach A15.** Extract the checks a second provider must
pass: exact outbox bytes, receipt clearing, reconnect replay, quarantine of a
changed duplicate, and backpressure that leaves the envelope with the caller.
Point those tests at the provider interface, with the reference provider as
the known-good implementation.

**Exit.** A host author can run those checks against a new provider without
reading Markoff's document implementation. A11 closes in the same change as
that part of A15.

### A14 — acceptance on current sources

Debug 93/93, earlier Release and ASan/UBSan runs, installed consumers, and
repository-corpus timings are in the archive log. They belong to the dates
written there. This doc reset did not re-run them.

**Do this after A13, A10, and A12.** On the revision you are finishing, record:

- Debug offscreen CTest, command and count.
- Release configure, build, and CTest.
- An ASan/UBSan run, or a one-line statement of the runner limit and the
  command you used instead. Leak detection may stay off when ptrace blocks it.
- Installed Core and view consumers.
- These behaviors in `tst_provider_backed_document`: untouched setext,
  blockquote, CRLF, trailing whitespace, and missing final newline survive
  export and reopen; two-document text convergence; multi-block paste;
  absent-append rollback; uncertain-append reopen. Do not look for
  `tst_block_buffer_invariant`. The current root build does not register it.
- Responsiveness. Re-run the existing probe when publication changed.
  Otherwise cite the 2026-10-14 archive figures and the provisional local
  budgets already recorded there (import ≤1 s, token resolution ≤250 ms,
  each local action ≤100 ms, reopen ≤2 s, RSS ≤128 MiB on that machine).
  Do not tighten the budgets in this task.

**Exit.** This section lists the commands, counts, git revision, candidate
commit, and prefix identity. Known limits are written down: reference
provider, collection disabled, Orca pass still deferred, host storage unbuilt.

### A15 — transition package

**Do this after A14.** Leave a package a new Corbomite agent can use without
reconstructing this repository's history:

- Candidate tag, commit, package version, manifest, and the install steps.
- Markoff revision and the build line above.
- Provider header, provider spec, and the A11 conformance checks.
- The mutation and save paths the host must call: open, import, edit, undo,
  replace, export, route, receipt.
- Limits: reference provider versus host journal, collection disabled,
  presence behavior as A12 left it.

**Exit.** The package names every prerequisite task and the evidence that
closed it. No open Markoff requirement is moved onto Corbomite to make the
list look finished.

### A16 — signal Corbomite

**Do this last.** After A15, write
`docs/handoff/YYYY-MM-DD-to-corbomite-ready-for-collabtext-transition.md`
and give the user the handoff path in the completion message. Update
`docs/STATUS.md`, `CLAUDE.md`, and this board to say the signal was sent.

The handoff states that Corbomite may begin its transition. It includes the
revisions, build commands, provider API, acceptance results, conformance
harness, and the host tasks that follow (storage, lease, application wiring,
production acceptance). A draft file with no delivery note does not close A16.

## Keeping this board usable

When a task moves, edit its row and its section. Delete play-by-play from
this file. The archive log is append-only; add a dated heading there only
when a decision would be lost without the detail. `docs/STATUS.md` stays
one screen: the current task, the candidate pin, and the latest test line.
