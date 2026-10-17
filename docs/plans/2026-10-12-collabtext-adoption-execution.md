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

1. **A10, done.** One actor keeps 100 undoable actions. The action that would
   pass that depth records retirement of the oldest one.
2. **A12, done.** Remote carets travel as ephemeral MPR/1 messages. Dropping
   one loses the caret and does not change document history.
3. **A11 and A15, done.** Provider-interface conformance is
   `markoff_provider_interface_conformance`. The transition package is
   `docs/handoff/2026-10-17-collabtext-transition-package.md`.
4. **A14, done.** Debug, Release, and ASan/UBSan each passed 93/93.
   Installed Core and view consumers passed 2/2. The responsiveness probe
   missed some provisional budgets; those budgets stay as written.
5. **A16.** Signal Corbomite. That signal is the last task, and nothing before
   it invites Corbomite to start.

## Standing rules

- Corbomite development stays paused until A16. Work in this repository with
  the reference provider and host-shaped fixtures. Do not send a readiness note
  early. Corbomite's journal, lease, and application wiring are its work after
  the signal. Wave's Q06 acceptance and any CollabText merge or publication
  stay separate release obligations.
- There is no legacy install to preserve. Each Markoff record family has one
  writer version, and readers refuse every other version. Reference-provider
  files are `MREF/5`. Quarantine records are `MQR/2`. Markdown is the
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
- The reference provider writes and reads `MREF/5` only, takes a `QLockFile`
  lease, reserves counters on the session, and has injected rename and sync
  faults. `tst_document_storage_provider.cpp` starts a child process for reopen.
- Text allocation goes through `ScopedTextStream::create(session)` and
  `session.reserve`. `ReplicaState` is only the provider's private identity
  file.
- `undoForBlock` publishes `undoGroup` for that block's action.
  `tst_provider_backed_document.cpp` undoes block A and checks block B still
  reads `Block B B` after reopen. A10 records retirement once an actor passes
  100 undoable actions.
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
  `tst_provider_backed_document.cpp`, then again after reopen. A13 removed
  `tst_block_buffer_invariant` with the other unbuilt legacy tests.

## Tasks

| Task | State | Agent action |
| --- | --- | --- |
| A01 | Done | Provider lifecycle and fallible edits are what `MarkoffDocument` uses. Host chrome and host storage are A15/A16. |
| A02 | Done | Reference provider: leases, reservations, reopen, injected sync faults. |
| A03 | Done | Provider-backed text, map, and list allocation. |
| A04 | Done | Selective undo of action A while action B remains. Depth retirement is A10. |
| A05 | Done | Canonical codecs. Do not add a second version of a record family. |
| A06 | Done | Local publication and recovery coordinator. Remote delivery stays A11/A12. |
| A07 | Done | Installed Core/Source/Styled/Canvas on composite `BlockId`. |
| A08 | Done | Provider-backed block, map, structural, paste, and format edits. A missing command is a new bug with a test. |
| A09 | Done | Import, replacement, reopen, and revision-bound export. |
| A10 | Done | Retire undo history past 100 actions per actor. |
| A11 | Done | Provider-interface conformance, closed with A15. |
| A12 | Done | Remote carets are ephemeral MPR/1 messages, not document actions. |
| A13 | Done | Retired engine, live leaf, and unbuilt legacy branches are gone. |
| A14 | Done | Acceptance recorded on `41544118`. Some probe budgets missed. |
| A15 | Done | Transition package for a Corbomite agent. |
| A16 | **Now** | Readiness signal. Requires A13–A15 done. |

Done means the recorded behavior is in the tree. Reopen a done task only when
a current test fails or a named command on its exit list is missing. Say which.

### A13 — retired engine removed

Done 2026-10-17 on top of `6e4e0acd`. The supported tree has one document
implementation.

Removed the vendored `libs/collabtext` submodule, the whole `libs/markoff-live`
tree, the unbuilt `MARKOFF_USE_COLLABTEXT_VNEXT` test branches, and the core
types those branches were the last callers of: `UndoLog`, `CrdtProxies`,
`MarkoffSerializer`, `SiblingMapOpHeader`, and `MarkoffOp`. Readers accept
`MREF/5` and `MQR/2` only. Canvas, Source, Styled, the parser, provider-backed
editing, undo through `undoGroup`, import/export, and the widget collaboration
test app stay. The live leaf's last supported snapshot is git tag
`archive/markoff-live-final`.

`cmake --build build-dev -j 4`, then
`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`: 93/93 offscreen.
The configured package was `0.2.0-alpha.5`, manifest v17, from
`build-collabtext-alpha5/install`. `/tmp/collabtext-alpha5-prefix-b` was not
on this machine. The deleted tests were not registered, so the count matches
the 2026-10-16 baseline.

### A10 — history retirement and backpressure

Done 2026-10-17. One actor keeps 100 undoable `edit` or `replacement` actions.
The initial import is not one of them. The action that would pass that depth
records `RetiredHistoryAssociation` for the oldest excess action, and for
redo groups the engines have already trimmed. The retired action can no longer
be undone. If that record cannot be attached, the new action is not appended;
a counter it already reserved stays unresolved until allocation-gap recovery.
Save, export, and garbage collection do not retire history. Engines stay at
Core's 100,000-group ceiling. The reader stays `MAI/3`. A04 stays closed.

The depth, reopen, younger-undo, and refusal checks are
`runActorUndoDepth` in `tst_provider_backed_document.cpp`. Verification is
recorded on `docs/STATUS.md`.

### A12 — ephemeral presence

Done 2026-10-17. `CollabConsumer::publishPresence` sends one `MPR/1` record
on the in-memory transport. The transport does not queue it, receipt it, or
replay it after reconnect. The collaboration test app publishes each
editor's caret and paints the peer's caret from the message. A peer edit
moves the scoped caret with the text. Removing the caret's block drops it.
Accepted actions, undo, and receipts stay unchanged. Session folds and
scoped selections are unchanged.

`QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_vnext_action_consumer` and
`./build-dev/bin/tst_canvas_vnext_session_positions` passed, including
`presenceIsNotADocumentActionAndIsNotReplayed` and
`ephemeralPresenceTracksPeerEditAndDropsWithTheBlock`.
`scopedSessionFoldsSyncAcrossViewsAndSerialization` passed in the same
canvas run.

### A11 — provider conformance, done inside A15

Done 2026-10-17 with A15.
`tests/collabtext-vnext/tst_provider_interface_conformance.cpp` uses
`DocumentStorageProvider` only. The reference provider is the known-good
implementation. The test checks exact outbox bytes, receipt clearing,
reopen of the remaining outbox, rejection of a changed remote duplicate,
and a refused quarantine write that leaves the caller's envelope in place.
CTest name `markoff_provider_interface_conformance`. It passed in the
offscreen suite, 94/94. A host replaces the factory in that file's `main()`.

### A14 — acceptance on current sources

Done 2026-10-17 on git `41544118`. Candidate
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, package `0.2.0-alpha.5`,
manifest v17, prefix `build-collabtext-alpha5/install`. Each suite was
configured twice with `-DMARKOFF_BUILD_APPS=ON` so `markoff-canvas-app-smoke`
is registered. A first configure of a fresh tree registers 92 tests because
the app and test options are declared after the canvas subdirectory.

Offscreen `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`:

- Debug, `/tmp/markoff-a14-debug`, 93/93 in 25.94 s.
- Release, `/tmp/markoff-a14-release`, 93/93 in 12.53 s.
- RelWithDebInfo with `-fsanitize=address,undefined -fno-omit-frame-pointer`,
  `/tmp/markoff-a14-asan`, 93/93 in 47.02 s.
  `ASAN_OPTIONS=detect_leaks=0` because ptrace blocks leak detection.
  `UBSAN_OPTIONS=print_stacktrace=1`.

Installed consumers, Release, offscreen, 2/2 in 0.14 s. The harness README
commands, plus a `c4core` build before install, because the four library
targets do not produce the RapidYAML archive the install step copies.
Prefix `/tmp/markoff-core-install-harness/prefix`. This is not Corbomite's
package or production storage.

`markoff_provider_backed_document_test` passed in each suite. It covers
untouched setext, blockquote, CRLF, trailing whitespace, and a missing final
newline through export and reopen; `runTwoWriterConvergence`; a two-block
paste; absent-append rollback through `recoverPendingAllocations`; and
uncertain-append reopen.

Release `perf_load_bench` on the same three inputs as 2026-10-14, reference
ephemeral provider, Manjaro 6.18.49, GCC 16.2.1. Provisional budgets stay
import ≤1 s, token resolution ≤250 ms, each local action ≤100 ms, reopen
≤2 s, RSS ≤128 MiB. This run:

- Typing corpus, 60,836 bytes, 1,000 blocks, SHA-256
  `9d9c827a4eaaa9f7d7924f53819f86abea45a58764414556afafbb0f17e4a18d`:
  import 2455 ms, spans 265 ms, edit/paste/structure 329/331/318 ms,
  reopen 3289 ms, RSS 75,908 KB.
- Table dogfood, 6,723 bytes, 69 blocks, SHA-256
  `4597fe9ff1e1f2e2647c7c49d239bdcfc16081014600a5904ddae4cfaf3ef92b`:
  import 85 ms, spans 7 ms, actions 23/25/27 ms, reopen 84 ms, RSS 46,316 KB.
- Canvas production plan, 187,514 bytes, 480 blocks, SHA-256
  `8f43f49ccc76813bb284f681120531209508ad706b031ca09e3d993cde9835a9`:
  import 1469 ms, spans 221 ms, actions 204/208/202 ms, reopen 1648 ms,
  RSS 96,892 KB.

The two larger inputs miss import and per-action budgets. The typing corpus
also misses span resolution and reopen. RSS stayed inside the budget. The
probe binary SHA-256 is
`9267a702c1e362d60ebd0fc055b9434ba9a0392a5db1729558751deb333c7d47`.
The two deleted live fixtures were read from git `6e4e0acd` into `/tmp` and
were not restored.

Known limits: reference provider, physical collection disabled, manual Orca
pass deferred, host journal and production network unbuilt. The missed probe
budgets are recorded and are not waived.

### A15 — transition package

Done 2026-10-17.
`docs/handoff/2026-10-17-collabtext-transition-package.md` names the
candidate, the Markoff build, the provider header and spec, the A11
harness, and the open/import/edit/undo/replace/export/route/receipt calls.
It lists A01–A14 and the evidence that closed each. Corbomite's journal,
lease, and production network stay host work after A16. The A14 probe
budget misses stay recorded there and are not reassigned.

Offscreen `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` on
`build-dev` passed 94/94 in 28.09 s, including
`markoff_provider_interface_conformance`. Package `0.2.0-alpha.5`,
manifest v17, prefix `build-collabtext-alpha5/install`.

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
