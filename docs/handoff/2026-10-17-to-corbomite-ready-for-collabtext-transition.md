# Ready for the CollabText transition

Date: 2026-10-17. This file is the delivered A16 readiness signal.
Corbomite may begin its transition. The implementation brief is
[`2026-10-17-collabtext-transition-package.md`](2026-10-17-collabtext-transition-package.md).
That brief describes Markoff sources at `281e3bc1`. This signal does not
change those sources.

Do this work in the Corbomite checkout. This repository does not edit
Corbomite.

## Revisions

| Piece | Revision |
| --- | --- |
| CollabText candidate | tag `collabtext-markoff-core-2026-10-13-alpha5`, commit `e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, package `0.2.0-alpha.5`, manifest v17 |
| Markoff sources under test | `281e3bc1` (`collabtext(A15): add the provider checks and transition package`) |
| Acceptance record | `74b85493` on code `41544118` |
| Machine prefix for those runs | `build-collabtext-alpha5/install` in this repository |

Install the immutable tag. Recreate a missing prefix from the candidate's
`docs/USING_VNEXT.md`. Do not point CMake at a CollabText checkout. A Core
behavior change needs a new tag and a new Markoff acceptance record.

## Build

Standalone configure and build, at most four jobs. On a fresh tree, pass
`-DMARKOFF_BUILD_APPS=ON`, then configure a second time so
`markoff-canvas-app-smoke` is registered. The app and test options are
declared after the canvas subdirectory.

```sh
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_PREFIX_PATH=/path/to/collabtext-prefix
cmake --build build-dev -j 4
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Offscreen is the default. The full suite on `281e3bc1` is 94 tests.

## Provider API

Header:
`libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h`.

Contract:
`docs/specs/2026-10-12-document-storage-provider.md`.

`DocumentStorageProvider::open` returns the session that owns the document
lease. Markoff calls `beginInitialImport`, then `reserve`, `prepare`, and
`append` for local actions. Remote actions use `acceptRemoteAction` after
Markoff has validated the MAB. Outcomes are `committed`, `absent`,
`uncertain`, or `rejected`. An uncertain result requires close and reopen.
`readRecoveryState` returns the accepted prefix, the local outbox,
unresolved preparations and reservations, owner history, and quarantine
bytes.

The host call sequence, record versions, and the 100-action undo depth are
in the transition package. Remote carets are ephemeral `MPR/1` messages.
They are not accepted actions.

## Acceptance already recorded

A14, on code `41544118`, package `0.2.0-alpha.5`, manifest v17:

- Debug, Release, and ASan/UBSan each passed 93/93 offscreen. ASan used
  `ASAN_OPTIONS=detect_leaks=0`. A fresh tree needs the second configure
  above before the canvas smoke test is in that count.
- Installed Core and view consumers passed 2/2. Harness:
  `tests/collabtext-vnext/markoff-core-install-harness/README.md`. Build
  the `c4core` target before install. The harness is a parent-shaped
  fixture. It is not Corbomite's package config, journal, or application
  acceptance.
- The Release responsiveness probe missed provisional import and per-action
  budgets on the 1,000-block and 480-block inputs. The typing corpus also
  missed span resolution and reopen. RSS stayed under 128 MiB on all three
  inputs. Those budgets were not tightened and were not waived. Numbers are
  on the task board under A14.

A15, on `281e3bc1`: offscreen
`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` passed 94/94 in
28.09 s. The added test is `markoff_provider_interface_conformance`.

## Conformance harness

`tests/collabtext-vnext/tst_provider_interface_conformance.cpp` talks only
to `DocumentStorageProvider`. The reference provider is the known-good
implementation. Replace the factory in `main()` with the host provider and
run the same checks: exact local outbox bytes across reopen, receipt
acknowledgement, remote acceptance that stays out of the outbox, and
quarantine of a rejected envelope. A record larger than 26 MiB is refused
and the caller's buffer stays unchanged.

`makeReferenceDocumentStorageProvider` writes `MREF/5`. It is not
Corbomite's journal.

## Host tasks that follow

These stay in Corbomite. This signal starts them. It does not do them.

- Storage: a journal that implements `DocumentStorageProvider` and passes
  `markoff_provider_interface_conformance` with the host factory.
- Lease: `open` owns the document lease for the host's scope and epoch.
- Application wiring: `MarkoffDocument::open`, `importInitialMarkdown`,
  local edits, undo and redo, `replaceMarkdown`, `snapshotForExport` then
  `acknowledgeExport`, `pendingOutboundActions`,
  `acknowledgeOutboundActionReceipt`, `routeRemoteAction`,
  `retryDeferredRemoteActions`, `quarantinedRemoteActions`, and
  `recoverPendingAllocations`. Uncertain publication is close and reopen.
- Production acceptance: a real network, the host journal, and application
  acceptance. The reference provider and the in-memory transport do not
  prove those. Wave Q06 and any CollabText merge or publication stay
  separate release obligations.

Physical collection, log pruning, and epoch transitions stay disabled.
The manual Orca pass stays deferred until someone asks for it. No open
Markoff editing requirement is assigned to Corbomite.
