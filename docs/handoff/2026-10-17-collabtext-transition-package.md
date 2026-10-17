# CollabText transition package

Date: 2026-10-17. Audience: a Corbomite agent.
This package is the implementation brief. The readiness signal that
authorizes the work is
[`2026-10-17-to-corbomite-ready-for-collabtext-transition.md`](2026-10-17-to-corbomite-ready-for-collabtext-transition.md).

## Candidate

Tag `collabtext-markoff-core-2026-10-13-alpha5`, commit
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, package `0.2.0-alpha.5`,
manifest v17. Install that immutable tag. The machine prefix used for the
acceptance runs is `build-collabtext-alpha5/install` in this repository.
Recreate a missing prefix from the candidate's `docs/USING_VNEXT.md`.
Do not point CMake at a CollabText checkout. A Core behavior change needs
a new tag and a new Markoff acceptance record.

## Markoff build

The Markoff sources this package describes are commit `281e3bc1`.
The readiness signal is a later commit and does not change those sources.
Standalone
configure and build, at most four jobs:

```sh
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_PREFIX_PATH=/path/to/collabtext-prefix
cmake --build build-dev -j 4
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Pass `-DMARKOFF_BUILD_APPS=ON` on a fresh tree, then configure a second
time, so `markoff-canvas-app-smoke` is registered. The app and test
options are declared after the canvas subdirectory.

A14, on parent `41544118`, recorded Debug 93/93, Release 93/93, ASan/UBSan
93/93 with `ASAN_OPTIONS=detect_leaks=0`, and installed Core plus view
consumers 2/2. The harness is
`tests/collabtext-vnext/markoff-core-install-harness/README.md`. Build the
`c4core` target before install. The harness is a parent-shaped fixture.
It is not Corbomite's package config, journal, or application acceptance.

## Provider

Header:
`libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h`.

Contract:
`docs/specs/2026-10-12-document-storage-provider.md`.

`DocumentStorageProvider::open` returns a session that owns the document
lease. Markoff calls `beginInitialImport` before the first allocation,
then `reserve`, `prepare`, and `append` for local actions. Remote actions
use `acceptRemoteAction` after Markoff has validated the MAB. Outcomes are
`committed`, `absent`, `uncertain`, or `rejected`. An uncertain result
requires close and reopen before another attempt. `readRecoveryState`
returns the accepted prefix, the local outbox, unresolved preparations
and reservations, owner history, and quarantine bytes.

`makeReferenceDocumentStorageProvider` is the in-repo known-good store.
Its files are `MREF/5`. It is not Corbomite's journal.

## A11 conformance

`tests/collabtext-vnext/tst_provider_interface_conformance.cpp` talks only
to `DocumentStorageProvider`. CTest name:
`markoff_provider_interface_conformance`. The reference provider is the
known-good implementation. A host replaces the factory in `main()` with
its provider and runs the same checks:

- A local `MAB/3` append appears in `pendingOutboundActions` with the
  exact record bytes, and those bytes are still there after the session
  is closed and opened again.
- `acknowledgeTransportReceipt` removes that action. The same receipt
  again succeeds. A different receipt for the same action conflicts and
  leaves the rest of the outbox unchanged.
- A remote accepted action stays out of the local outbox. Rebinding its
  transaction id to different bytes is rejected, and the accepted bytes
  stay as first committed.
- `storeQuarantinedOperation` keeps the rejected envelope across reopen.
  An exact retry is idempotent. A record larger than 26 MiB is refused,
  the caller's buffer is unchanged, and recovery does not gain that record.

The document and router suites still cover bounded routing. A full router
hold that returns backpressure without accepting the envelope is
`runScopedListRouter` in `tst_document_storage_provider.cpp`. The interface
harness above is the check that does not require that file.

## Calls the host makes

Open with `MarkoffDocument::open(provider, scope, mode)`. The scope is the
host's document key and epoch. Then:

- `importInitialMarkdown` once, before ordinary edits.
- `applyBlockEdit` and `applyStructural` for local edits.
- `undoD2`, `redoD2`, `undoForBlock`, and `redoForBlock` for history.
  One actor keeps 100 undoable actions. Passing that depth retires the
  oldest action inside a later accepted action. Physical collection stays
  off.
- `replaceMarkdown` for a whole-document replacement. It keeps history and
  mints new block identities.
- `snapshotForExport`, then `acknowledgeExport` on the exact snapshot the
  host wrote. Markdown is the export. Accepted actions are the recovery
  authority.
- `pendingOutboundActions` after startup and reconnect. Send those exact
  MAB bytes. After the host has authenticated a peer receipt, call
  `acknowledgeOutboundActionReceipt`.
- `routeRemoteAction` for a received MAB. Advance a transport receipt only
  after a committed result. `retryDeferredRemoteActions` after reopen.
  `quarantinedRemoteActions` lists rejected envelopes the provider retained.
- `recoverPendingAllocations` after a rejected or absent publication before
  the next ordinary edit. An uncertain publication is reconciled by closing
  the document and opening it again.

Remote carets are `MPR/1` ephemeral messages on the collaboration test
app's transport (`apps/markoff-collab-testapp/EphemeralPresence.h`). They
are not accepted actions, session JSON, or provider records. Dropping one
drops the caret.

## Limits

The reference provider, the in-memory transport, and the A14 suites are
Markoff evidence. Corbomite's journal, lease integration, application
wiring, and production network are host work after the signal. Physical
collection, log pruning, and epoch transitions stay disabled. The manual
Orca pass stays deferred. A14's Release probe missed the provisional
import and per-action budgets on the 1,000-block and 480-block inputs.
Those budgets were not tightened and were not waived.

## Prerequisites

| Task | Evidence |
| --- | --- |
| A01 | `MarkoffDocument::open` and fallible `ActionPublicationResult`. |
| A02 | Reference provider leases, reservations, reopen, and injected faults. |
| A03 | Provider-backed text, map, and list allocation. |
| A04 | Selective undo. A10 adds the 100-action depth. |
| A05 | One writer version per record family. Readers refuse every other version. |
| A06 | Local publication and recovery. |
| A07 | Installed Core, Source, Styled, and Canvas on composite `BlockId`. |
| A08 | Block, map, structural, paste, and format edits. |
| A09 | Import, replacement, reopen, and revision-bound export. |
| A10 | `kActorUndoDepth` is 100. Retirement is recorded by a later action. |
| A11 | `markoff_provider_interface_conformance`, closed with this package. |
| A12 | Ephemeral `MPR/1` carets. |
| A13 | Vendored engine, live leaf, and unbuilt legacy branches removed. |
| A14 | Debug, Release, and ASan/UBSan 93/93. Installed consumers 2/2. |

No open Markoff editing requirement is assigned to Corbomite by this package.
