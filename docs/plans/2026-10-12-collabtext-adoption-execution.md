# CollabText adoption — Markoff execution roadmap

Updated 2026-10-13. This is the authoritative implementation task board for
fresh agents. The [adoption plan](2026-10-11-collabtext-vnext-adoption.md) retains
M0–M12 milestone summaries, rationale and dated discovery evidence. Update task
status and evidence here, then summarize milestone changes there and in STATUS.

## Coordination and finish line

User direction: **Corbomite development stays paused until the final task A16
signals that Markoff is ready for Corbomite's transition.** Complete Markoff work
using reference providers and host-shaped fixtures inside this repository.
Earlier suggestions that Corbomite should implement storage in parallel are
superseded. Do not make Corbomite implementation or host production acceptance a
prerequisite for this signal; that would prevent the transition from starting.

The finish line is a verified Markoff engine/document path, implementable host
provider contract, reproducible exact dependency, and complete migration handoff.
Corbomite then implements its own service/storage/lifecycle transition. Its crash,
file-sync and application acceptance gates follow that work. Wave's external
same-candidate acceptance and CollabText merge/publication remain separate release
obligations; track them without claiming that Markoff tests completed them.

A16 is the last Markoff implementation task. Only at A16 issue the readiness
signal. A draft provider document, passing isolated probe, or upstream suite is
insufficient evidence of readiness.

## Compatibility policy

There is no deployed Markoff data or existing user base to preserve. New
Markoff-owned persistence and APIs have no backward-compatibility requirement:
each durable record family has one canonical writer and reader version, and
older experimental forms are rejected rather than migrated or dual-read.
Markdown remains the explicit import/export format. CollabText operation
formats remain governed by the selected Core candidate's capabilities.
User direction (2026-10-15): there are no legacy installs or users to preserve;
consumers will port, and retired Markoff APIs are removed without a deprecation
period. Root CMake now requires the installed Core candidate for standalone and
embedded builds and has no pinned-engine or retired-Live build switch.

## Read before editing

1. Repository `CLAUDE.md`, relevant library guides and
   [Shared Editing Manifesto](../EDITING-MANIFESTO.md).
2. This execution board and the [adoption plan](2026-10-11-collabtext-vnext-adoption.md),
   particularly the composite identity inventory and selective-undo audit.
3. [Persistence formats](../specs/2026-10-11-collabtext-vnext-persistence.md),
   [provider contract](../specs/2026-10-12-document-storage-provider.md), and
   [provider header](../../libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h).
4. Corbomite's [host response](../../../Corbomite/docs/handoff/2026-10-12-to-markoff-collabtext-host-response.md)
   and Markoff's [provider reply](../handoff/2026-10-12-to-corbomite-provider-contract.md).
   Use these as integration requirements; Corbomite remains paused.
5. In the candidate worktree, `docs/specs/markoff-core-adoption.md`,
   `docs/handoff/markoff-core-response.md`, `docs/handoff/consumer-adoption.md`,
   `libs/collabtext/include/collabtext/ALPHA_CAPABILITIES.md`, and
   `docs/reports/2026-10-11-markoff-core-candidate.md`.

Current candidate: `collabtext-markoff-core-2026-10-13-alpha5`,
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, package `0.2.0-alpha.5`, manifest
v17, at `/home/clinton/dev/collabtext/.worktrees/reliability-v2`. This candidate
adds selective historical-group undo/redo for text and IdList. The relocated
Markoff consumer build uses `/tmp/collabtext-alpha5-prefix-b`; preserve immutable
candidates and require a new candidate plus renewed acceptance for upstream
semantic changes.

## Authoritative starting evidence

- Markoff HEAD at preparation was `28e9a734bf5d383ddac9a132653c3691e5e72bd1`.
  Migration files are modified/untracked on `master`; preserve this work and
  inspect it before relying on any summary. A clean HEAD alone does not capture
  the current implementation. Record the actual tested sources/revision.
- The normal root build still uses the pinned CollabText implementation at
  `8b9e780bc9324faa5e7de166f7a181d1e8fbd546`. Production `MarkoffDocumentPrivate`
  still owns private Buffer/IdList types. There is no production provider path.
- `libs/markoff-collabtext-vnext` owns the isolated adapter target. Codecs,
  provider-backed text/map/list streams, bounded routers, ephemeral and
  restartable local reference providers, action recovery and lifecycle replay
  exist. Production `MarkoffDocument` still has no provider path.
- The relocated alpha.5 Release consumer build and CTest pass **3/3** on
  2026-10-13. The provider test covers action preparation/append/recovery and
  accepted-prefix replay; the scope test covers full-route allocation, document
  and epoch separation, provider reopen and rejection of a mismatched session.
- Text and map adapters have one public allocation path: an open
  `DocumentStorageSession`. Direct `ReplicaState` constructors and the temporary
  MPSK/1 private-token registry have been removed, so every operation identity
  comes from the provider's exact reservation journal. Do not treat the passing
  isolated consumer as production acceptance.
- Earlier alpha.3 probe passes and historical full-suite counts are dated
  evidence. They do not establish that current sources or the production port pass.

This adoption is a clean start for CRDT persistence: existing Markdown imports
into new history. The current candidate's reader obligations cover its supported
vNext formats; they do not promise migration of earlier experimental stores or
raw anchors. If a new requirement to retain such history appears, scope it
explicitly and reassess the plan before cutover.

Working Release package prefix:
`/tmp/collabtext-alpha5-prefix-b`. This is a local convenience,
not a distributable dependency. If absent, use the exact candidate's
`docs/USING_VNEXT.md` and package verification tooling to recreate the install;
record producer SHA, format manifest and hashes. Do not substitute root master.

Reproduce the current narrow baseline with:

```sh
cmake -S tests/collabtext-vnext -B /tmp/markoff-host-boundary-alpha5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/collabtext-alpha5-prefix-b
cmake --build /tmp/markoff-host-boundary-alpha5 --parallel 4
ctest --test-dir /tmp/markoff-host-boundary-alpha5 --output-on-failure --no-tests=error
```

## Execution tasks

Status: ☐ open; ◐ implemented partly or evidence incomplete; ☑ complete with
recorded evidence. Dependencies are prerequisites for closing a task. Advance
independent work where useful; settle A04's undo semantics before broad view work.

| Task | Work | Parent milestones | Dependencies | Status |
| --- | --- | --- | --- | --- |
| A01 | Finish provider and durable edit API contract | M1/M3 | — | ◐ provider lifecycle owns import and ordinary publication with typed outcomes, recovery and rejection rollback; candidate `MarkoffDocument::open` and block/structural/map edits return publication results, and durable tests cover tentative/commit/revert, definite-absence rollback and uncertain-state write blocking; explicit in-session and fresh-open recovery now finalize exact outstanding allocation gaps after replay; installed public-header consumers verify durable reference-provider reopen/history and instantiate Source, Styled and Canvas editors on a vNext document; Canvas exposes `publicationPending`, gates every mutation path on pending/provider readiness, and emits effective read-only changes; provider lifecycle state transitions now emit `providerStateChanged`, with a durable absent-append regression proving Canvas remains pending/read-only until reservation recovery completes; host status presentation and production host-provider acceptance remain open for the A16 handoff |
| A02 | Reference providers, leases and scoped allocation storage | M1/M3 | A01 | ✓ MREF/5 canonical writes with MREF/4 recovery compatibility, scoped leases/reservations, absent-preparation retirement, remote accepted records, durable transport receipts, process reopen and injected sync-failure recovery |
| A03 | Provider-backed text/map/list allocation and green baseline | M4/M5/M10 | A02 | ✓ relocated alpha.5 baseline; provider-backed adapters, replay, archive/gap and reservation-failure checks |
| A04 | Resolve selective and cross-stream undo semantics | M8 | A01/A03 | ◐ durable A/B trace and one three-stream undo MAB; MAI validates parent kinds, group ancestry and retirement ancestry; MarkoffDocument selectively undoes/redoes separate block text and paired list/kind-map actions across reopen, and a mid-compensation reservation failure rolls back the complete tentative action; production merge MAB is decoded and asserted to contain text, IdList and kind-map operations, then selectively undone/redone across provider reopen; retirement/backpressure and broader group policy remain open |
| A05 | Complete shared/private codecs and reader contracts | M3 | A01/A04 | ◐ canonical MAB/3, MAH, MAI/3, MOH/1, MMP/value, MTG/1, MMG/1 and MDS/1 codecs plus text/map gap projection application; MAI/3 binds replacement source restoration; MOH enforces canonical routes and complete indexed-stream history; broader production recovery integration open |
| A06 | Action publication/recovery coordinator | M3/M6/M8 | A02/A03/A05 | ◐ MAB/MOH validation, prepare/append/inspect, uncertain action/history/gap recovery, revision-bound accepted prefix and MTG/1/MMG/1 gaps, canonical gap routes and full local action ancestry checked against accepted MABs; MarkoffDocument rebuilds accepted state after proven absence and offers explicit in-session/fresh-open publication of exact terminal records for unresolved local reservations; remote integration remains open |
| A07 | Production package build and composite structural identity | M1/M5 | A03/A05/A06 | ◐ candidate Core, Source, Styled and Canvas now install through the parent-shaped harness; separate installed consumers compile and run the public editors against a vNext document, and `ldd -r` resolves the complete view graph; the Core consumer also passes durable reference-provider import/edit/fresh-provider reopen/undo/redo/export, revalidated after the binary identity-index fix; current-source Release rebuild/install and both installed consumers pass after Canvas fold/session synchronization; a provider-backed Canvas regression verifies remote structural insertion uses a distinct composite identity and creates the matching accessibility child; Markoff production ownership remains open, while Corbomite host storage/package acceptance follows A16 |
| A08 | Production block text and sibling-map port | M4/M5 | A04/A06/A07 | ◐ candidate MarkoffDocument publishes per-block UTF-8 edits, atomic non-overlapping edit batches, structural list/kind actions, typed maps, import/export and durable undo/redo; committed structural insert/remove now emit block signals with rows and advance the structural edit sequence; candidate list and blockquote display markers read replicated attributes; Markdown format verbs publish through atomic text batches; `Cmd::changeKind` now publishes kind and optional attributes in one accepted action; candidate Styled binding format and round-trip tests use the candidate document fixture; structured paste replaces a same- or cross-block selection as one action, including into the blank paragraph created by empty Markdown import, and restores the final block tail; an uninitialized provider scope without registered initial import remains unable to accept ordinary actions; behavioral completeness and production ownership remain open |
| A09 | Import, reopen, replacement and revision-aware export | M3/M4/M9 | A05/A06/A08 | ◐ candidate initial import stores exact source, rejects malformed UTF-8, and reopens from replay; pristine setext/blockquote/CRLF/whitespace/final-newline bytes round-trip, and revision-bound export saves through QSaveFile; text-only and frontmatter-only edits preserve untouched source bytes; tree-sitter-derived link-reference fields cover multiline labels/destinations/titles, CommonMark raw-label Unicode case-fold/ASCII-whitespace matching, escaped/entity-spelled label distinction, the 999-character limit and overlong-label paragraph fallback, destination/title escape and entity decoding, and first-definition duplicate semantics while export preserves source spelling; Markdown import into a separate document scope starts without the source document's text undo history; `replaceMarkdown()` publishes one replacement MAB with fresh block identities and exact source snapshot; MAI/3 retains the exact pre-replacement source so undo/redo restore source and identities across reopen; candidate `replaceMatches()` stages all valid matches into one accepted edit action, including multi-block undo/redo after reopen; definite-absence/uncertain-append coverage passes; peer initial import and replacement MABs register through MarkoffDocument, reconstruct exact source/export state, and support local-history replay after reopen; a new reference-provider close/reopen test confirms exact block identities and content survive across provider instances |
| A10 | Implement durable document/per-block undo | M8 | A04/A05/A06/A08 | ◐ candidate block and document entry points publish linked text/map/list-kind undo/redo, including untargeted document maps; `applyBlockEdits()` publishes non-overlapping edits across blocks as one MAB action with per-stream owner history; atomic `applyBlockMerge()` groups survivor text, list removals and kind-map tombstones; payload swaps, Markdown paste with tail restoration, and block/multi-block formatting have provider-reopen undo/redo coverage; production candidate MarkoffDocument tests accept peer text/map MABs, preserve them through local undo, and apply peer-authored undo and redo MABs while retaining a later local edit; independent text and mixed text/list/map actions converge in opposite delivery orders and retain local undo after fresh durable provider opens; mixed compensation now covers definite append absence, partial reservation failure, retry and redo, including selective undo/redo across all groups of one mixed merge action with per-block and structural sequence notifications; retirement/backpressure and broader fault acceptance remain open |
| A11 | Complete production remote routing and receipts | M6 | A06/A07/A08 | ◐ isolated routers enforce canonical text/list/map routes on offer and quarantine restore, compare duplicates with accepted MAB bytes and persist/rehydrate bounded quarantine; MREF/5 atomically accepts exact remote MAB bytes and cross-stream identities without receiver reservations or local-history replacement; empty documents can register peer initial-import/replacement MABs, and later receiver-local history survives reopen; `MarkoffDocument::routeRemoteAction()` bounds complete causally deferred MABs, drains them after dependency commits, and returns action-ID/revision receipts only after projection acceptance; persisted waits can be retried explicitly after reopen or external stream registration, and retry returns receipt-eligible commits; lifecycle preflight rejects unresolved candidates and targets absent from both pre-action and candidate IdLists; dependency drain, caller-retained backpressure/retry, peer text/map/replacement/undo/redo, and opposite-order replica convergence are tested, including durable opposite-order text/list/map actions after fresh provider opens; `MarkoffDocument` emits exact local MAB bytes only after local provider acceptance/projection refresh; the reference-provider-backed durable outbox exposes locally accepted unacknowledged MABs, persists host-verified receipts, and `CollabConsumer` retries pending actions on startup and reconnect; production provider outbox conformance and real network transport remain open |
| A12 | Scoped positions, session/view integration and API migration | M7 | A07/A08/A11 | ◐ candidate Source/Styled bindings preserve view selections through scoped tokens; Session and FoldRef JSON round-trip scoped positions; Canvas caret/selection, folding refs, remote-presence resolution, typing/deletion/IME, selection collapse, block promotion, format verbs, table row/column edits and insertion, atomic line-move payload swaps, smart paste and accessibility edit tokens have candidate routes. Provider-backed Source binding multiline paste replaces selected text through one structured-paste action and restores its caret; Core tests cover same/cross-block replacement and undo/redo. Canvas tests cover session-position updates, payload-swap undo, typed smart paste with tail restoration, one-action table insertion undo, provider-backed remote-presence rebasing/rendered tint/detachment, accessibility child creation after a peer structural MAB, and durable document plus serialized caret-session restoration across separate writer/reader processes. Per-block/multi-block format undo and Core paste/swap reopen undo are covered. Selection, FoldRef and Session now expose scoped positions only and no longer contain legacy TextAnchor/AnchorJson branches. Production presence transport remains open; candidate fold-state projection and session reattachment are covered by shared-session, JSON-restore and detach/reattach regressions. The retired Live leaf is excluded from the supported root build; its unused source remains in the tree pending cleanup |
| A13 | Remove obsolete persistence/compaction and finish build surfaces | M9 | A09/A10/A12 | ◐ obsolete save/GC and duplicate APIs removed; `TextAnchor.h`, `Cursor.h`, `AnchorJson.h/.cpp` and the now-unreferenced `AnchorConversion` adapter are physically deleted; Selection, FoldRef and Session now expose scoped positions only; the candidate position test covers scoped tokens; fresh installed Core and view consumers pass 2/2 with none of those headers in the package; root CMake now requires the installed candidate for standalone and embedded builds, with no pinned-engine or Live build switch; candidate Debug build and CTest pass 93/93; the pinned-engine selection can no longer be restored by passing the former OFF option; other retired implementation files, tests and dead compatibility branches still need source cleanup |
| A14 | Actual Markoff consumer, fault and responsiveness acceptance | M10 | A09/A10/A11/A12/A13 | ◐ the provider-backed collaboration app builds and the current full Debug candidate CTest passes 93/93; configured ASan/UBSan candidate checks and prior Release suites pass; installed Core and Core/Source/Styled/Canvas consumers pass; synthetic and repository-authored performance probes and reproducible local candidate-prefix digests are recorded; repository-corpus performance baselines, three-run distributions for 392/1,000-block inputs and a verified 54-MAB reopen probe are recorded against provisional local budgets; broader user-document and production-provider timing remain open; Corbomite production-provider and application/package acceptance are separate post-A16 host gates, not Markoff readiness prerequisites |
| A15 | Freeze dependency/interface and assemble transition evidence | M11 readiness | A14 | ☐ |
| A16 | Signal Corbomite to begin its transition | M12 | A15 | ☐ final task |

## Immediate execution order and current risks

The single-engine production cutover is now verified for standalone and
embedded builds. A02/A03 are complete at their stated boundaries; A04–A06
still need retirement/backpressure policy and broader production recovery
evidence. Continue with those semantic gaps, complete A11/A12 transport and
presence paths, remove dead legacy source branches under A13, then close A14
acceptance before A15/A16 readiness.

**Current cutover boundary and risks (2026-10-14):** standalone Markoff now
defaults to the provider-backed engine, while an embedded Markoff subproject
defaults to the pinned engine until its host opts in. Fresh standalone Release
and embedded-parent configures verify those defaults, and the full configured
standalone provider-backed suite passes. Candidate builds and the provider-backed
Canvas regression also pass, including remote structural insertion and its
accessibility child; the October 13 numeric-ID failure below is historical
evidence from before the strict rejection fixture replaced it. A full installed
Markoff application consumer, production host storage, and Corbomite package
acceptance remain open. Legacy-path numeric buffer serialization and
load-sensitive Canvas timings remain cutover risks until checked against the
integrated production path. The synthetic candidate performance probe is
recorded below and is not a user-facing responsiveness budget.

**Historical root-suite evidence (2026-10-13):** `cmake --build build-dev
--parallel 2` completed successfully. `QT_QPA_PLATFORM=offscreen ctest
--test-dir build-dev --output-on-failure --parallel 2` reached 215/216 before
the final benchmark exceeded the observed wait window; the run was stopped and
is incomplete. `tst_realistic` passed in 215.80s. `tst_canvas_accessibility`
failed, and a direct isolated rerun reproduced
`structure_remote_edit_insert_remove_create_destroy()` at line 1816: the remote
inserted block reuses an existing numeric ID from the receiving document's
local counter, so the accessibility ID diff cannot announce it as new. This
confirmed the then-current A07 blocker; no numeric compatibility allocation
patch was applied. The candidate provider-backed route now has separate passing
coverage recorded in the A07/A12 progress below; the default legacy route has
not thereby been cut over.

**Isolated benchmark evidence (2026-10-14):** the current Release
`build-dev/bin/tst_benchmark` was run alone under a 600-second timeout. It
passed the reported throughput, edit-pattern/size, tombstone-degradation,
multi-replica-convergence/hotspot, undo/redo, and undo-stack assertions. The
reported 50%-tombstone cases fell to 5–10 ops/sec at 5K–8K fragment documents;
10-replica convergence reported about 6 ops/sec. The process timed out during
`memory_growth` after reporting 2,000 of 5,000 edits, before the rest of that
slot or the suite completed. Exit 124 means this is incomplete A14 evidence,
not a passing benchmark. The slowness reproduces the broad-run stall and should
be isolated against the candidate ownership path after A07/A08.

**Checkpoint and evidence discipline:** the migration currently spans modified
and untracked files. Before accepting a slice, record a reproducible revision or
source digest covering both, the exact candidate/package and nonempty checks.
Record failing checks as well as passing ones. Run a controlled offscreen suite
for the current sources before claiming a regression baseline; record test
parallelism, isolate budget failures and retain semantic failures as blockers.
`scripts/run-tests.sh` currently defaults to eight test jobs; this is separate
from the maximum four build jobs and must be explicit in performance evidence.
Do not extend API cleanup unless it removes a cutover dependency or satisfies
an existing exit condition. No dated delivery commitment is recorded here;
readiness is determined by acceptance evidence, not task-count percentages.

### A01 — finish the provider and durable edit contract

Use the existing header/spec as the starting draft. Specify validation and
reader versions, session/lease/provider lifetime, owner-thread calls, reentry
refusal, and the fallible document construction/recovery boundary. Define how
existing synchronous edits expose tentative, committed, failed and uncertain
states, including notifications and UI writability. Select an explicit ephemeral
provider for previews/tests; numeric constructors cannot silently create durable
authority. Define first-import intent, complete recovery snapshots, private-history
completion, exact transaction retries and export acknowledgments without requiring
Corbomite to implement or answer before Markoff can proceed. Record genuine
unresolved product choices explicitly.

**Exit:** usable Qt/C++ API and normative call/state contract, compiled from a
host-shaped consumer using installed public headers. Every provider operation
has an error/recovery meaning; implementation tests can exercise the whole sequence.

**Progress (2026-10-13):** `ProviderBackedDocumentState::publishGap()` now
requires the lifecycle to be in `recoveryRequired`. It refuses terminal
coverage before initial import and after recovery has returned the document to
`ready`, with a typed `awaiting_state` diagnostic. The relocated installed-
package provider consumer verifies both refusals; its Release CTest passes
3/3. Production `MarkoffDocument` wiring and its fallible edit outcomes remain
open.

**Contract clarification (2026-10-13):** the provider spec and lifecycle header
now state that every access is confined to the opening thread, including
read-only accessors and use of the borrowed mutable storage reference. Session,
recovery and projection references are explicitly non-transferable and must
not outlive the owner. Mutation calls still return typed off-thread/reentry
refusals. This documents the actual synchronous, unsynchronized API boundary;
thread-safe snapshots remain part of the production document integration.

**Progress (2026-10-13):** `DocumentStorageSession::beginInitialImport()` now
durably binds the first transaction ID and exact source Markdown before
reservation. Exact retries are idempotent, source changes are rejected, and a
fresh transaction ID is allowed only after proven absence and complete terminal
gap coverage. The coordinator requires the initial MAB's decoded MDS bytes and
action ID to match the intent. While an intent is pending, ordinary edits are
refused. The intent clears atomically with successful initial owner-history
persistence; a fault trace proves it survives reopen and the original
transaction finishes after recovery. A second trace proves an absent attempt
can be gap-covered and then successfully republished after reopen with the same
source under a fresh ID, including its exact private history. This closes the
provider-level API hole; production document construction/import orchestration
remains open. Absence resolution now also durably tombstones an exact transaction
whose preparation failed before atomic rename, provided all its reservations
remain unresolved; this permits safe initial-import rebind after terminal gap
coverage without allowing the retired transaction ID to be prepared later.

**Progress (2026-10-13):** the installed-package adapter now includes
`ProviderBackedDocumentState`, a fallible lifecycle owner that opens and retains
the provider session, reads one recovery snapshot, and replays the accepted
prefix before exposing a projection. It reports explicit ready,
awaiting-import, import-authorized and recovery-required states. Ordinary action
permission stays closed while the first import is pending or reservations and
transactions need recovery; exact import authorization is retriable, while a
changed retry is refused. A consumer regression verifies the empty-document
gate and that ephemeral storage does not silently satisfy a durable open. The
owner rejects lifecycle mutation off its opening thread. Reentrant mutation
also returns a typed refusal. The existing `MarkoffDocument` constructors and
edit APIs are not wired to this owner yet, so A01 remains partial.

**Contract clarification (2026-10-14):** the provider spec now fixes the
synchronous editor outcome sequence for committed, committed-with-history-
pending, proven-absent/rejected, and uncertain actions. Tentative view updates
are distinct from committed document notifications; rollback follows accepted-
prefix rebuild, and uncertainty keeps the tentative view visible while closing
local edit/undo until reconciliation. The production document still must expose
the typed outcomes and enforce those notification/write-barrier rules.

The installed-package lifecycle regression now also asserts that
`canStartOrdinaryAction()` is false while committed private history is pending,
and true again after exact recovery. The absent-action/gap-recovery trace now
asserts the same barrier until the scoped reservation is covered. Release build
and CTest pass 3/3. The uncertain terminal-gap/reopen trace also verifies the
barrier remains closed through uncertainty and opens after replay. This verifies
the provider-owner barrier only; production document/view behavior is still
unimplemented.

**Progress (2026-10-14):** the lifecycle owner now exposes owner-thread-only
staging accessors for the list, per-block text and map streams in its own
accepted projection. Definite absent/rejected publication replays the accepted
prefix into the existing projection before returning, clears tentative state,
and preserves the recovery/write barrier for unresolved reservations. Uncertain
publication keeps the tentative projection and closes writes. A regression
stages an initial text seed, rejects malformed MAB bytes and verifies the
tentative text is cleared while the import intent and barrier remain. The
durable three-stream import consumer now stages through these owner-held stream
handles. `publishStagedAction` builds the exact storage transaction from the
staged projection and owner history; incomplete history rejects before changing
the import intent. Production `MarkoffDocument` still must call this boundary
and map its outcomes to view notifications. The installed consumer suite passes
3/3.

Opening this lifecycle owner now inspects and recovers every unresolved exact
transaction before replaying the accepted projection. Committed shared actions
finish their prepared owner-history write automatically; absent or uncertain
transactions remain behind the recovery-required write barrier. A consumer
regression reopens a document after shared commit/private-history failure and
verifies automatic recovery, exact history and ready state.

The lifecycle retains that same publication coordinator for recovery gaps.
After reopen it rebuilds the accepted prefix, keeps ordinary actions disabled
while reservations remain, and publishes a terminal map gap through the owner;
the initial-import retry trace then verifies the gap clears the reservation
without changing the pending import source. This still precedes and does not
replace `MarkoffDocument` integration.

A second lifecycle trace simulates interruption after a preparation is durable
but before append. Reopen proves absence, rebuilds the previous accepted map
value, retains the rebuild gate, and publishes the exact terminal gap through
the retained coordinator. An injected post-rename uncertainty is then resolved
by another lifecycle open, which finishes gap history and returns ready.

**Progress (2026-10-13):** `ProviderBackedDocumentState` now owns ordinary MAB
publication as well as terminal-gap publication. `publishAction()` enforces
owner-thread/non-reentrant calls, accepts only an authorized initial import or
a ready document, and refreshes the accepted projection and lifecycle state
after commit. The relocated consumer tests publish both a complete empty
initial import and a staged list/text/map import through this API. The latter
checks structural identity, exact block routing, replayed text/map values,
private history completion and the transition to ready. It then publishes an
ordinary text edit while ready and verifies restart replay of both MAB records,
the changed text, both restored text undo groups and the updated private-history
transaction. A malformed attempt preserves import authorization. The installed
alpha.5 consumer build and all three tests pass. This closes a lifecycle adapter gap; production
`MarkoffDocument` construction, edit staging and tentative/committed/uncertain
UI outcomes remain open under A01/A06/A08.

**Progress (2026-10-14):** an injected post-rename uncertainty through
`ProviderBackedDocumentState::authorizeInitialImport()` returns
`commit_uncertain`, leaves the owner non-writable, poisons the live provider
session against further recovery reads, and preserves the exact source bytes and
transaction identity across a fresh lifecycle open. The focused provider test
passes. This closes the lifecycle uncertainty trace for import authorization;
the fallible `MarkoffDocument` factory and edit outcomes remain open.

**Production edit fault progress (2026-10-14):** the provider-backed
`MarkoffDocument` consumer now injects durable append failures through its
ordinary edit API. A pre-rename failure returns `absent`, emits tentative then
revert without commit, restores the prior text, and leaves new writes blocked
for reservation recovery. A post-rename uncertainty retains tentative text,
emits neither revert nor committed-content notification, and blocks another
action; destroying and reopening the document reconciles the accepted edit and
its undo history. The candidate consumer CTest passes these traces alongside
the existing import/export/edit/undo cases. Full installed host-shaped
consumer acceptance remains open.

**Canvas publication-state integration (2026-10-14):** `Canvas::View` now
exposes a Qt `publicationPending` property. Tentative block changes set it;
commit and confirmed rollback clear it unless provider recovery is still
required. Attaching to an already-recovery-required document initializes it
from provider state. `isReadOnly()` now combines the explicit host read-only
flag, unresolved publication and provider action readiness, and all Canvas
mutation gates use that effective value. A provider-state notification after
in-session allocation recovery refreshes the view; `readOnlyChanged` and the
accessibility read-only state track effective writability. Candidate Canvas
coverage verifies initial-import gating and pending/commit/revert transitions;
the focused `tst_canvas_vnext_session_positions` passes. The host's visual
status treatment remains open.

**Installed-consumer check (2026-10-14):** rebuilt and installed current-source
Release Core, Source, Styled and Canvas targets through the parent-shaped
harness, then rebuilt the installed public-header consumers. The durable Core
consumer and the consumer that constructs all three editors against a vNext
document pass `2/2`. The view test now sets Qt's offscreen platform explicitly,
so the harness runs headlessly under CTest. This verifies the host-shaped
installed API boundary; production host storage and status presentation remain
for the A16 handoff.

### A02 — implement reference providers and scoped allocation storage

Provide explicit ephemeral support and a restartable local reference provider
for integration/fault tests. Keep reference storage separate from Corbomite's
production journal. Preserve the full document/epoch/stream tuple in counter
allocation. If backed by ReplicaState, persist an injective private-token registry
before allocation, compare full keys on reopen and fail on corruption/collision.
Journal full reservation intervals; reconcile a crash between counter persistence
and journal completion. Retain outstanding reservations without preparation.
Implement exact transaction binding, immutable accepted prefixes, revision tokens,
identity-claim uniqueness, and recovery of committed-but-incomplete private history.
Lease ownership must survive multiple views and exclude independent processes.

**Exit:** tests for scope separation across documents/epochs, registry reopen,
duplicate/conflicting IDs and identity claims, stale revisions, lease exclusion,
authority loss/corruption and reservation/journal fault boundaries. Distinguish
simulated crash evidence from real process/filesystem restart evidence.

**Progress (2026-10-12–13):** `makeEphemeralDocumentStorageProvider()` now
implements explicit non-durable sessions, document-wide in-process leases,
scoped counter reservations, exact prepared transactions, identity-claim
uniqueness, committed-prefix revisions, recoverable private-history completion
and export acknowledgments. On 2026-10-13, text and map adapters also gained an
MPSK/1 durable full-scope/private-token registry. The explicit
`makeReferenceDocumentStorageProvider(root)` also persists its local reference
state in a versioned MREF/3 file, including reservations, exact preparations,
accepted records, private history, export watermark and the writer actor in the
header, so even an empty document detects lost private identity state. The
consumer test covers provider-object reopen between preparation, append and
accepted-prefix recovery; a child process also reserves an unattached interval
and exits with `_Exit`, then a fresh process recovers its actor and exact
interval. Corrupt MREF bytes are refused, duplicate same-process document
sessions conflict, and a competing process sharing the reference root receives
`writer_conflict` while authority is held. Deleting private identity state is
refused against an existing document, and a forced pre-commit reservation I/O
failure preserves the prior counter state. A pre-rename preparation failure
preserves the exact unattached reservation and accepted revision across reopen,
then permits an identical transaction retry. The installed alpha.4 consumer build
and all three CTest cases pass. The relocated alpha.5 consumer injects failures
after atomic rename, at file sync, and at directory sync. Reopen recovers the
reserved interval, while uncertain appends inspect and retry the exact committed
action without duplication. This closes A02's reference-provider exit: MREF writes
the counter and its full reservation in one atomic state replacement, so there is
no separate counter/journal crash window. The Markoff action/recovery coordinator
and Corbomite's production journal remain A06/A16 work. Unsupported platforms
refuse provider creation before writing state.

Validation used candidate package producer `c0a0ef55ea9b6a3e448b9ef5c258f09933ebb127`
from tag `collabtext-markoff-core-2026-10-11` (tag commit
`1399e9533c9cfc1006854487af4e71b8b79989fd`) at
`/tmp/collabtext-alpha5-prefix-b`:

```sh
cmake -S tests/collabtext-vnext -B /tmp/markoff-host-boundary-alpha5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/collabtext-alpha5-prefix-b
cmake --build /tmp/markoff-host-boundary-alpha5 --parallel 4
ctest --test-dir /tmp/markoff-host-boundary-alpha5 --output-on-failure --no-tests=error
```

Result on 2026-10-13: build passed; CTest **3/3 passed**. The source set has
continued to change since the earlier alpha.4 aggregate hash, so that stale hash
is intentionally omitted; the current worktree and command result are the
evidence for this run.

### A03 — move allocation behind the provider

Replace direct ReplicaState allocation/leases in `ScopedTextStream` and
`ReplicatedMapStream` with provider-backed authority. Add a public-facade scoped
list adapter with full-identity ordered projection and exact-identity anchors.
Preserve allocator observation/exhaustion errors and exact successful reservation
intervals. Canonical public stream names remain unchanged. Restore probe coverage
for text, Unicode, tokens, replay, history reopen and bounded routing; add installed
identity/gap/archive tests for the alpha.4 APIs actually used by Markoff.

**Exit:** current lease-key failure resolved through scoped allocation, probe
passes against relocated alpha.4, actor/counter authority remains stable after
restart, repeated list values stay distinct and literal archives preserve identity.

**Progress (2026-10-13):** `ScopedTextStream` and `ReplicatedMapStream` accept
an open `DocumentStorageSession`; both request counters through its full
canonical stream scope and use the session actor. The explicit ephemeral
provider test covers text and map allocations and their reservation records.
The restartable local reference provider now has consumer checks for durable
reservations (including unattached intervals), exact preparations, immutable
accepted-prefix recovery and document lease exclusion. Text, map and structural
IdList adapters accept an open provider session and allocate through the full
canonical stream name. The list test verifies repeated application values keep
distinct structural BlockIdentity values, exact identity anchors and targeted
removal. An additional durable-provider consumer test publishes one MAB action
containing list, text and map operations, closes the provider, reopens it, and
replays all three into fresh projections while checking the original list
identity and resulting text/map state. Consumer checks also round-trip a literal
CTGP archive with a fixed actor and prove committed gap application prevents
counter reuse. The installed consumer probe covers Unicode, scoped position
tokens, history restore, replay, duplicate handling and bounded routing against
the relocated alpha.5 package. Text and map stream construction is now
provider-only; the former direct ReplicaState path and its private-token
registry are removed. These checks satisfy A03's
isolated adapter/baseline exit; production package integration is tracked by
A07/A08 and end-to-end acceptance remains under A14. The relocated alpha.5
consumer suite now also injects a pre-rename durable reservation failure through
`ScopedTextStream::replaceUtf8()` and confirms tentative text, stream counter and
prior reservations remain unchanged before a successful retry; all three
alpha.5 CTest cases pass.

**Progress (2026-10-13):** the direct `ReplicaState` constructors for text and
map streams are removed, along with the transient MPSK/1 scope-token registry.
The installed probe and scope consumer now construct every stream through an
open document session; scope mismatch is rejected, and reopen tests replay the
accepted operation before allocating the next identity. This leaves the
provider's reservation journal as the only Markoff allocation authority.

### A04 — settle undo semantics before implementing broad integration

Audit `UndoLog::undoForBlock` and engine group inspection/coalescing. A block's
selected action may be older than another action on the shared order/map stream;
stack undo cannot safely select it. Specify action-to-stream-group mapping,
compensation order, redo policy and failure recovery. Implement the intended
behavior through supported public semantics or request a concrete selective-group
API upstream. Any proposed change to user-visible undo behavior needs an explicit
decision; do not silently substitute document-wide LIFO or discard another block's
action. Apply replicated map compensation with fresh stamps, not local restoration.

**Exit:** accepted behavior and executable regression design for action A on block A,
then action B on block B through the same shared engine: undo A preserves B and
targets exactly A's groups. Record upstream dependencies/new candidate if required.

**Progress (2026-10-13):** Source inspection confirms that `UndoLog::undoForBlock`
selects a prior document action but `MarkoffDocument`'s dispatcher ignores the
recorded `opId` and calls stack-based `undo()`/`redo()` on shared IdList and map
engines. The pinned alpha.5 Core candidate `collabtext-markoff-core-2026-10-13-alpha5`
(`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`) exposes additive
`try_undo_group` and `try_redo_group` operations for text and IdList. The isolated
Markoff adapters now expose those operations and local-history restore for the
structural stream. A consumer regression creates action A (block A's text plus
shared order group), then B (block B's text plus the same order stream), selectively
undoes A while preserving B, rebuilds fresh projections from the operation prefix,
restores each stream history, and redoes A then B with their original block
identities. The relocated consumer suite passes 3/3. Core Debug and Release suites
pass 100/100. The affected tests pass under ASan/UBSan with leak detection disabled;
LeakSanitizer is blocked by this runner's ptrace restriction, and a full
instrumented build exceeded the temporary disk cap. A durable-provider
regression now publishes separate A and B actions, then one undo MAB covering
list, text and map compensation; it reopens, replays the accepted bundles and
verifies B remains. The coordinator validates that the final owner-history event
maps every MAB operation identity exactly once to its stream and primitive.
The undo append injects uncertainty after atomic rename; the live provider
session is poisoned and recovery after reopen inspects the exact undo transaction
and persists its owner history before replay. A subsequent write is rejected
while recovery is pending. A second durable trace now restores owner history,
selectively redoes A as one fresh list/text/map MAB, commits it, and reopens a
second time; exact A/B block identities and order, both text streams, map values
and redo owner history all survive replay. Group retirement is attached to the
redo action so the undo remains redoable. MAI validation now requires each
retirement to name a redo action on that group's original action ancestry; an
ordinary edit cannot retire an undoable group. The negative regression and
relocated provider consumer pass.
MAI now also requires redo events to name an undo parent and prevents undo from
targeting another undo event. Negative cases cover redo-of-edit and undo-of-undo;
undo/redo list and text groups must occur on their parent ancestry, and map groups
must stay on an ancestor map stream while allowing their fresh compensation
identity. A negative case rejects undo of a sibling action's valid group. The
relocated provider consumer passes 3/3.
Production MarkoffDocument action capture/undo
dispatch and coordinator-level allocation fault traces remain open. A separate
trace injects definite pre-rename append absence, then retires the preparation,
rebuilds from the accepted prefix and commits the terminal map gap. The
initial-import regression now also injects pre-rename preparation failure
through the coordinator, verifies the live write barrier and retry refusal,
reopens, rebuilds the accepted prefix, and commits the terminal gap before
rebinding the same source to a fresh import ID. A provider-backed text adapter
allocation fault now verifies a pre-rename reservation write failure leaves the
tentative text empty, preserves the next counter, and adds no text reservation;
the already-held structural reservation remains unchanged. Production action
staging and `MarkoffDocument` undo dispatch still need this allocation barrier
wired into real edits. See [the A04 handoff](../handoff/2026-10-13-a04-selective-history.md).

**Progress (2026-10-13):** added `ActionTransactionBuilder` as the canonical
adapter boundary for binding one MAB/3 action to its accepted revision, exact
durable reservation intervals, operation identity claims and complete owner
history. It reads the recovery snapshot from the live session itself and refuses
mismatched document/epoch/owner/transaction metadata, stale
history, absent reservation coverage, unresolved prior transactions, and a
missing or duplicate matching history event. Each adapter-produced local
operation carries the exact range(s) it reserved; the builder requires one
binding per MAB operation and an exact match with the provider's unresolved
reservations. This allows a text operation to own a multi-counter run without
letting an omitted staged operation's interval enter the bundle. The durable
multi-stream initial import consumer now builds its transaction through this
API; regressions verify stale-history and omitted-operation refusal. `ActionStager`
is now the one typed conversion path from adapter-produced list, text and map
operations into the MAB bundle and its reservation-index bindings; the consumer
test no longer constructs those operation envelopes by hand. Local text
and list operation records carry allocator reservation ranges, and map operations
carry their exact single-counter reservation. Map encoding now runs before
reservation, so a rejected oversized value cannot consume writer authority. The
provider suite verifies that refusal leaves the next map counter unchanged.
`MarkoffDocument` still does not stage and publish edits through this path.

### A05 — complete durable shared/private record formats

Extend existing MAB/MAH/MMP and typed value codecs with scoped terminal-gap records,
cross-stream action/history metadata, retirement association and source-preservation
data needed to reconstruct exact Markdown after fresh replay. Account for separators,
initial source snapshots and parser-derived caches explicitly. Gap records bind
document, stream, primitive, epoch, actor, full interval and accepted prior. Define
map terminal evidence/coverage for Markoff-owned maps rather than inventing a Core
map primitive. Keep exact original bytes and define versions/readers/limits.

**Exit:** literal fixtures plus round-trip/refusal tests for all used record families,
bad scopes/identities, unknown versions, truncation, overflow, malformed Unicode,
duplicate claims, conflicting intervals and typed values. A recovery prefix contains
everything needed for identities, exact exports and owner undo; no C++ layout is wire data.

**Progress (2026-10-14):** MOH/1 now validates owner-local history routes against
Markoff's canonical streams: the list history must use `markoff/order/v1`, and
text history must name a valid full block-identity stream. Encode and decode both
reject forged routes. MOH also requires an MAH snapshot for every list/text
stream referenced by its MAI action index, preventing replay from silently
dropping local undo state. The provider consumer covers forged routes and an
omitted indexed stream; the relocated suite passes 3/3 with the installed
candidate package. Production recovery integration remains open.

**Progress (2026-10-13):** `ReplicatedMapStream` now applies an accepted MMG/1
record to its projection, validates the exact document/map/epoch and prior
revision, treats exact replay as idempotent, refuses overlapping/conflicting
intervals and rejects any later MMP identity covered by committed gap metadata.
For a gap owned by the local actor, the adapter advances its provider counter
past the interval before allowing a new map write. `ScopedTextStream` now
applies accepted MTG/1 records through Core's checked text counter-gap API,
binds the exact position scope and prior revision, and treats gap-bearing state
as touched. Consumer checks cover wrong-prior refusal and a same-actor text
edit beyond the interval. Production accepted-prefix replay remains open.

**Progress (2026-10-13):** MAB/3 is now the only emitted and accepted action
envelope. Ordinary edit/undo/redo records carry a mandatory zero-length source
field; initial imports carry the exact MDS/1 snapshot in the same envelope.
MAB/3 now selects CTBO/0003 as the only text operation codec; it rejects both
MAB/1 and the abandoned MAB/2 envelope. Consumer coverage checks import bytes,
ordinary round trip, and unsupported envelope/codec refusal. Production action
construction and replay integration remain open. The relocated alpha.5 Release
consumer build and CTest pass 3/3 after this format change.

### A06 — implement the action and recovery state machine

Own accepted-prefix replay and the writable barrier in Markoff. Capture accepted
per-stream prior before tentative edits. Produce one canonical action, persist its
preparation with reservations/intended owner history, append atomically and inspect
the original transaction after uncertainty. Only proven absence permits rebuilding
fresh engines from accepted history and committing full unused intervals as gaps.
The coordinator's `recoverAndRebuild()` callback boundary requires the host to
report successful rebuild against the complete accepted prefix before gap
publication; concrete engine-factory/replay integration remains open.
Restore accepted undo metadata; reject abandoned tentative groups. Finish private
history after shared commit, including crash recovery. Expose typed outcome/status
and coalesce document notifications around the accepted action boundary.

**Exit:** fault traces at allocation, encoding, prepare, append, gap commit and
private-history completion preserve the accepted action prefix and original identities.
No phantom receipt, subset action publication, counter reuse, fabricated edit or
continued affected-stream writes during unresolved uncertainty.

**Progress (2026-10-13):** the provider test now injects a definite failure
before the private-history atomic rename after shared MAB commit. The coordinator
returns `committedHistoryPending`, keeps the write barrier, and leaves one
accepted action plus its exact preparation recoverable. After provider reopen,
`recover()` stores the original MOH bytes, removes the pending preparation and
returns committed without duplicating the shared record. The relocated alpha.5
consumer build and CTest pass 3/3. Production accepted-prefix replay and startup
integration remain open.

The absent-action gap trace also injects uncertainty after MMG atomic rename.
After reopen, the coordinator recovers the same gap transaction, records its
private completion, verifies there is still exactly one gap record and confirms
that the whole reserved map interval is covered. This also passes in the 3/3
relocated consumer suite.

The recovery snapshot now decorates every accepted record with the provider's
opaque prior and committed prefix tokens, derived from its transaction index.
This lets replay verify that each MTG/1 or MMG/1 record names the exact prefix
immediately before that record, while preserving tokens as equality-only values.
The durable gap-recovery regression verifies the first action's committed token
equals the gap's prior token and the gap's committed token equals the current
accepted revision after reopen. The alpha.5 consumer suite passes 3/3; production
engine-factory replay and startup wiring remain open. The coordinator now
validates nonempty tokens, unique transaction IDs and revision transitions
before action publication, gap publication or recovery; regressions reject a
broken chain, reused token and snapshot-head mismatch before writes proceed.

**Progress (2026-10-14):** accepted-prefix replay now reconciles every
owner-authored MAB through the owner-history committed revision with its MAI
event order, action kind and exact stream/primitive/actor/counter operation set.
Remote MAB actions remain outside the owner-local undo index. Map-history
associations across the reconciled ancestry must retain reversible prior state.
A durable selective-undo test mutates an earlier event while keeping MOH/MAI
well-formed and verifies replay rejects it; the relocated consumer suite passes
3/3. Production startup wiring remains open.

**Progress (2026-10-14):** `ActionPublicationCoordinator::validateGap()` now
rejects MTG records and reservations outside the canonical block-text and order
streams before provider preparation. A regression encodes a valid MTG/1 record
for an arbitrary text route and verifies publication returns `wrong_stream`;
the relocated consumer suite passes 3/3.

`AcceptedPrefixProjection` now rebuilds fresh structural, per-block text and
Markoff map streams from a complete `StorageRecoveryState`. It verifies MAB
transaction IDs, operation identities and canonical routes, applies MTG/MMG
records against each adjacent prior revision, refuses duplicate/deferred Core
operations, validates that the sole initial import is MAB/3 and matches the
durable source intent, rejects edits before that import, and exposes no partial
projection on failure. After shared replay it validates the committed MOH/MAI
action association against that MAB, restores actor-local list/text undo stacks,
and retains the cross-stream index and map prior values. The durable A/B restart
trace now uses this complete builder instead of handwritten replay/restore. A mixed list/text/map
MAB replays after provider reopen; the absent-action recovery regression now
uses this replayer to rebuild accepted map state and then replays the committed
MMG prefix after reopen. Wiring this builder into production MarkoffDocument
startup/action publication remains open. `ProviderBackedDocumentState::open`
now uses the coordinator to inspect/recover unresolved transactions before it
exposes the replayed projection. The committed-history fault trace now verifies
that this provider-backed lifecycle seam finishes exact private history and only
then marks the document ready. It retains the coordinator across absence
rebuild and terminal-gap publication, with an initial-import retry regression
covering the complete lifecycle sequence. Real MarkoffDocument engine/action
wiring remains open. A separate durable-preparation interruption trace opens
through the lifecycle owner, confirms the accepted map projection was rebuilt,
publishes the reserved interval's terminal gap through the retained coordinator,
injects post-rename uncertainty and verifies a subsequent owner open completes
the gap and restores ready state. Alpha.5 consumer CTest passes 3/3.

A new cross-stream append-absence trace starts from a committed empty initial
import, tentatively edits the shared list, one block-text stream and a map, then
injects failure before the action append rename. The coordinator proves the
action absent and resolves its exact preparation, but refuses counter-gap
publication until `recoverAndRebuild()` successfully replays the accepted
prefix. After reopen, the accepted projection contains none of the tentative
edits and all three exact reservations remain unresolved. An injected uncertain
post-rename append on the first terminal gap is recovered after another reopen.
The two remaining list/text/map gaps publish against successive accepted-prefix
revisions, and the document remains unwritable until the final gap commits.
Fresh replay contains the initial import and all three gaps only. The relocated
provider regression passes; production Markoff action staging and startup
wiring remain open.

The lifecycle publication regression now also covers the success path: it
publishes a staged three-stream initial MAB, then a ready-state text-edit MAB,
completes owner history for both and verifies fresh replay reconstructs the
edited text. This exercises coordinator publication and recovery as one lifecycle
surface; constructing those staged actions from actual `MarkoffDocument` edits
remains the A08 integration gap. The relocated alpha.5 consumer suite passes
3/3.

A zero-operation initial import exposed an absence-recovery edge case: definitive
append absence skipped preparation retirement when the transaction had no
reservations, and recovery left the coordinator blocked despite there being no
counter gaps. The coordinator now retires every proven-absent preparation,
holds the barrier through accepted-prefix rebuild, and releases it after rebuild
when no reservations or transactions remain. The new empty-Markdown import
regression verifies both the held and released barrier states; the relocated
alpha.5 consumer suite passes 3/3. The lifecycle owner now allows the provider
to validate a fresh transaction ID for the exact same source after proven
absence; an end-to-end zero-operation import trace commits that retry and
reaches ready state after closing and reopening the lifecycle owner, and rejects
rebinding to different source bytes. Production Markoff action staging remains
open.

`ProviderBackedDocumentState::recoverActionAndRebuild()` now exposes the live
absence path through the lifecycle owner. It retains the owner's coordinator,
replays the accepted prefix into a fresh projection before allowing gaps, and
refreshes lifecycle readiness after the rebuild. A production-seam regression
tentatively writes a Markoff map value, proves the MAB append absent, invokes the
owner API, confirms the rebuilt projection has no tentative value, commits the
exact MMG/1 reservation, and becomes ready only afterward. A second trace fails
private-history persistence after a real map MAB commits; lifecycle recovery
inspects that same transaction, stores its owner history and refreshes the map
projection with the accepted value. Relocated consumer CTest passes 3/3.
MarkoffDocument has not yet been wired to this owner.

A separate allocation-failure trace creates a tentative list insertion, then
fails the following block-text reservation before its atomic rename. Reopen
shows no tentative list/text projection and retains only the earlier list
reservation. The lifecycle owner commits its exact MTG/1 gap before import can
proceed; the list counter advances past that interval while the failed text
route remains at counter one. This closes adapter/provider evidence for
cross-stream allocation failure, but production Markoff action staging remains
open.

**Production fresh-open gap follow-up (2026-10-14):** candidate
`MarkoffDocument::open()` now checks the recovered reservation journal after
the lifecycle has reconciled unresolved transactions and rebuilt accepted
state. It publishes one canonical MTG/1 or MMG/1 terminal record per exact
remaining reservation through the same lifecycle coordinator, advancing from
each newly accepted revision. A durable paired list/kind undo test injects
failure after one compensation reservation, verifies the live document rolls
back and becomes read-only, then reopens and verifies the accepted list/kind
state, empty barrier and writable readiness. The provider-backed consumer
CTest passes. Remote action integration remains open.

**In-session gap recovery follow-up (2026-10-14):** lifecycle publication now
marks the accepted-prefix rebuild after a proven-absent transaction with the
coordinator. Candidate `MarkoffDocument::recoverPendingAllocations()` then
publishes exact terminal gaps through that coordinator and releases the write
barrier only after all reservations are covered. A durable absent text edit
regression verifies tentative rollback, explicit recovery, and a successful
retry on the same open document. The provider-backed consumer CTest passes.

### A07 — port structural identity and introduce the production package build

Add a root build path that consumes installed `CollabText::Core`, without loading
both the current and candidate engines or leaking implementation headers. Port BlockId
to full structural insertion identity, including hashes, comparison/token surfaces,
structural operations, pending keys and all caches/maps in the adoption inventory.
Reconcile order with `visible_entries()`, never by narrowed application values.
Keep caller values as operation payload only; never use them as identity. Use an isolated candidate build
until the production port passes; compile host APIs with correct shared-library exports.

**Product constraint:** this software has no deployed data or users to preserve.
The cutover should have one canonical identity, API and format wherever possible;
do not add numeric-ID conversion, readers for superseded formats or alternate
execution paths to ease migration. The current implementation remains only until its replacement is
ready, then it is removed before A13 closes. No parallel persisted format is part
of the cutover.

**Exit:** actual core target builds with installed Core; concurrent offline block
creation, repeated values, reorder, removal/undo and restart preserve exact block
identities, order and per-block routes. No process-static numeric allocator survives
as vNext identity authority.

**Progress (2026-10-13):** the relocated consumer suite is now an opt-in root
build target, enabled with `MARKOFF_BUILD_COLLABTEXT_VNEXT_TESTS=ON` and
`CMAKE_PREFIX_PATH` set to the installed candidate prefix. The root build
compiled and ran all three candidate consumer tests (3/3); `readelf` confirms
the provider test links neither `markoff_core` nor a CollabText shared library.
This gives the candidate package a normal-workspace compile/test path without
loading the pinned engine into those executables. The production
`markoff_core` target still links the pinned engine and A07's structural
identity port remains open.

**Progress (2026-10-13):** The production `markoff_core` target still links the
current `collabtext` target. A fresh source audit finds 77 direct CRDT references
across 12 production core headers/source files, plus 24 numeric-ID/raw call sites
in those files. The installed candidate exports
`CollabText::Core`; the isolated adapter target consumes it, but there is not yet
a production-core build path that avoids loading both engines. A link-only
switch would therefore not test this exit condition. Port work remains open;
the existing MarkoffDocument boundary must replace structural identity and
operation surfaces together before routing document edits through candidate
streams under A08. The identity cutover has one canonical actor/counter form;
temporary numeric call sites are removed as part of that cutover rather than
kept as a second accepted identity path.

**Revalidation (2026-10-14):** candidate Core now links through the real
provider-backed document test consumer, and the candidate library has no
undefined `MarkoffDocument` methods. The candidate Styled executable still
fails to link on the unmigrated binding, position, formatting and command APIs.
A standalone `cmake --install` run also installs vendored rapidyaml artifacts
but omits `markoff_core` and its headers: Markoff's Core install rules are
currently enabled only by the parent build's `KDE_INSTALL_LIBDIR` setup. The
candidate Core link is useful integration evidence, but it is not an installed
Core package or installed-consumer acceptance. Parent-build staging remains
open under A07/A13.

**Installed Core follow-up (2026-10-14):** built the candidate Core in a clean
temporary parent-shaped export set with the exact installed CollabText alpha5
prefix `/tmp/collabtext-alpha5-prefix-b`, then staged the exported parser,
text-units, vNext adapter, Core libraries and public headers under
`/tmp/markoff-core-install-harness/prefix`. The checked-in
`tests/collabtext-vnext/markoff-core-installed-consumer` fixture configured
against the staged `CorbomiteTargets.cmake`, built, and passed CTest (1/1) while
opening an ephemeral provider, importing Markdown, applying a block edit and
exporting the result. The tracked parent-shaped harness now reproduces this
sequence from `tests/collabtext-vnext/markoff-core-install-harness`; its clean
build compiled `markoff_core` and `c4core`, installed successfully, and the
tracked consumer CTest passed 1/1. `ldd -r` on the installed consumer reported
no unresolved symbols when given the staged and CollabText library directories.
Source was Markoff `HEAD 28e9a734bf5d383ddac9a132653c3691e5e72bd1` plus the
current working tree; candidate `MarkoffDocumentVNext.cpp` SHA-256 was
`8f8115293510566dd528b829869a3822e6a8c216720bc39554b02de37b59ce91` and the
Core CMake file SHA-256 was
`5ae20b6a88d9e53fc70296b231e810df43ea2bd3b21cab22b0fc6310ea364170`. The
checked-in install-harness CMake/README SHA-256 values are
`4664ddbf9c3e5b9c898f9eca0c9c992d7446f152a0bc04ed0bfc361e071fb6ce` and
`d7058fa2f36535364bc027970ae9af11c3ba03e74935f386710e5e01d541665a`;
consumer fixture SHA-256 values are
`e4dd9447177eda68b5d49a01636a606de5ffcf4bc981f0c5dac183144a5d609b` for its
CMake file and `1a88a67b8cf73c7aadd29e86b42a2188f25808492f79c5a57028989b4637d2ea`
for its import/edit/export driver; the provider-backed document regression
source SHA-256 is `6c66736e11487d2859d7b95836bdc13f9b2a055e5c645921c65eb21a99a36188`.
The first install pass identified an unbuilt vendored `c4core` artifact;
building that dependency and rerunning install completed the staging. This
verifies the candidate export and public consumer path, while a fresh full
Corbomite package build/configuration and application consumer remain open.

**Revalidation (2026-10-14):** reran the tracked harness after the MREF/4 remote
acceptance API change. The parent-shaped Release build rebuilt
`markoff_core` and `c4core`, install completed, and the installed consumer
CTest passed 1/1. `ldd -r` on that executable with the staged Core and alpha.5
library directories reported no unresolved symbols. Current source digests:
`MarkoffDocumentVNext.cpp` `ece493686a14d19598d45a35f112e33b7624830b03a1897f1b4002a63b031c06`,
Core `CMakeLists.txt` `c7be9deb5685bda8d5771177dbe197806404a5adab0a2b55cad89544ba0f2e34`,
and installed-consumer driver
`main.cpp` `1a88a67b8cf73c7aadd29e86b42a2188f25808492f79c5a57028989b4637d2ea`.
The public `MarkoffDocument.h` digest is
`732f91665441ee461ffbd993fcf58acae54ae32aabda2b296d3ea55548251959`.
The provider public header digest is
`4c053e05f4fb64808d15564e43b473eaaa08b74de3b5509dc7f513ba77e6db24`,
and provider implementation digest is
`4e37bff17d36469d3d65b6712554e9eed155fd83538e572cb9fd474139b3ebbf`.
The lifecycle header and implementation digests are
`3dfa589a135317c299070bc4225d5d22d529cb00b9000687e4a215119a5f621b` and
`367f255511c68f1b06753c617638aa77fcdb41e576a4a648825d93daea41e61e`.
The fixture still exercises ephemeral provider mode, so durable host-provider
and full application acceptance remain open.

**Current-source install recheck (2026-10-14):** rebuilt the checked-in
parent-shaped harness into a fresh `/tmp/markoff-core-install-current` prefix
from current sources, installed the exported targets and public headers, then
configured the installed consumer in a separate build directory. The consumer
compiled against `CorbomiteTargets.cmake`, ran a durable reference-provider
import/edit/export, destroyed both document and provider, reopened the same
scope with a fresh provider, and verified exact block identity plus undo/redo
history. CTest passed (1/1). `ldd -r` with the installed Markoff and alpha.5
library directories reported no unresolved symbols. This recheck uses
Markoff `HEAD 28e9a734bf5d383ddac9a132653c3691e5e72bd1` plus the current worktree;
digests are `MarkoffDocumentVNext.cpp`
`921854cadcb5082c2aa7e4cddc2c2b65ef76eea2b0e06c90c5b39f574537f2bf`, Core
`CMakeLists.txt` `c7be9deb5685bda8d5771177dbe197806404a5adab0a2b55cad89544ba0f2e34`,
public `MarkoffDocument.h`
`6d8f110875b2cdcf738231ba095b7f007bb8ad2840eda83fabca30e75f43b239`, provider
implementation `d97ad0e5d1273c5a03ffd4fa12e0c5a6b7639d3a16a375fc0b33059f5b2def6a`,
the installed consumer driver
`1fe1410721062b4175a5ba2828ab9974179d540185f7b52c1b37971b0029be1f`, and test
source `tst_markoff_document.cpp`
`969646c38be82ca643b14e8a454c8a8d599e47f8839df38b4855bf3321c98657`; the
tracked harness files retain hashes listed above. This confirms current
parent-shaped installed-consumer acceptance with restartable reference storage;
production host storage, full app linking and Corbomite package acceptance
remain open.

**Installed runtime-link follow-up (2026-10-14):** repeated the checked-in
parent-shaped Release harness from current sources at Markoff `HEAD
28e9a734bf5d383ddac9a132653c3691e5e72bd1` plus the working tree, installed
into `/tmp/markoff-core-install-current-20261014/prefix`, then configured and
ran the installed durable reference-provider consumer in a separate build.
CTest passes 1/1 and verifies import/edit/export, fresh-provider reopen, exact
identity and undo/redo. The first standalone `ldd -r` audit exposed that the
installed `libmarkoff-parser.so` could not locate its sibling
`libts-markdown-parser.so` without the test's `LD_LIBRARY_PATH`; both parser
libraries now carry a relocatable sibling-library install RPATH, as does Core.
After reinstall, `ldd -r` resolves Core, Markoff Parser, the grammar library and
Tree-sitter with no missing libraries or undefined symbols and no environment
override. Relevant current working-tree SHA-256 values are Core CMake
`dcb8f5b3f9cb428a166a618b0062ef37e2d5edcc69c1cddf9ef0d127c7144729`, Parser
CMake `cf1f444b6660be5fef9feb54fc81851f42c0105cf580980d014e12e524ff889e`,
public `MarkoffDocument.h`
`a6f366f0e28ca5797034d48980872ec251d7a4d02ed8e69f5b5c2f8d4b69f8a9`, and
`MarkoffDocumentVNext.cpp`
`b912d386939389ee2234916f2e00aed2b3413947bcc5cb9cbc087e1c044f7954`.
This closes sibling runtime lookup in the staged install; production host
storage, full app linking and Corbomite package acceptance remain open.

**Installed consumer refresh after identity-index correction (2026-10-14):**
rebuilt and reinstalled the existing parent-shaped Release harness from the
current source tree into `/tmp/markoff-core-install-current-20261014/prefix`.
The separately configured installed consumer rebuilt and passed CTest 1/1; a
standalone `ldd -r` reported no missing libraries or undefined symbols, with
both Markoff parser libraries resolved from the installed sibling directory.
This revalidates the exported Core package after changing reference-provider
identity-index keys to binary-safe `QByteArray`. It remains a Core consumer, not
the complete Markoff application or Corbomite host package.

The references group into a concrete port boundary: `MarkoffDocument.cpp` owns
48 of the 77 direct engine references (allocation, list/buffer reads and writes,
remote apply, serialization and replay); `MarkoffDocumentPrivate.h` owns the
current list/buffer storage and numeric allocator. The public leak is in
`MarkoffDocument.h`, `CrdtProxies.h`, `FoldRef.h`, `Session.h` and `AnchorJson.h`;
anchor/session conversion is in `AnchorConversion.{h,cpp}`, `AnchorJson.cpp`,
`Session.cpp` and `SessionPrivate.h`. The coordinated replacement therefore
needs to move list identity, per-block text ownership and public position
surfaces together. Converting only `BlockId` or linking the candidate package
would leave the operation route split.

**Revalidation (2026-10-14):** the default `markoff_core` target still builds,
while a fresh `MARKOFF_USE_COLLABTEXT_VNEXT` production build again fails at
`AnchorConversion.h`'s legacy `crdt/Anchor.h` and `MarkoffDocument.cpp`'s
legacy `crdt/IdList.h`. The relocated candidate consumer suite passes 3/3.
This keeps the next production step unchanged: replace the document's list,
text, map, identity and position ownership as one routed cutover; neither the
passing adapter tests nor the default target build prove that production Core
uses the candidate engine.

**Progress (2026-10-14):** added an engine-selected structural-only `BlockId`
representation and version-2 anchor JSON for the candidate production build;
candidate consumers use the complete actor/counter pair and strict canonical
decode checks. The default configuration retains its existing representation
and wire shape until the replacement is ready. The candidate Core build still
compiles legacy `MarkoffDocument` and anchor conversion sources, so the
coordinated source/ownership selection and stream-backed dispatch remain the
next port work.

**Progress (2026-10-14):** candidate `markoff_core` now excludes the legacy
`MarkoffDocument` and anchor-conversion translation units and compiles a new
provider-backed `MarkoffDocument::open` factory. It performs fallible provider
open/recovery before constructing the document and exposes readiness. Candidate
document reads for order, per-block text, block kind and block attributes now
read the accepted projection. Provider-backed edit, structural, map,
import/export and durable undo/redo actions are also implemented. The focused
provider-backed consumer test now links the actual shared `Markoff::Core`
target, exercises session creation, and passes with the storage-provider test;
the candidate Core has no unresolved `MarkoffDocument` symbols. The Styled demo
entry point and Markdown renderer now open/import through a provider. Building
the app still fails at the `markoff_styled` link boundary on unmigrated source
binding, position, inline-span, FormatOps and Cmd APIs. Legacy root tests also
still instantiate the deliberately deleted numeric constructor. These are the
remaining real consumer and view integration tasks.

**Progress (2026-10-14):** `MarkoffSerializer::encode` now rejects legacy
numeric buffer identities instead of writing them as zero actor/counter fields
that decode as an empty block. `tst_d5_serialization::numericBlockIdentity_isNotSerialized`
passes, preserving schema 2's structural-only identity contract in the default
build while the candidate representation remains actor/counter-only. The
candidate `markoff_core` also rebuilds, and its provider-backed document and
storage-provider tests pass 2/2 with the structural-only representation.

**Progress (2026-10-13):** removed unused `<crdt/Clock.h>` and
`<crdt/Operations.h>` includes from the public `MarkoffDocument.h` and removed
the remaining public CRDT implementation wording. A source scan now finds no
`crdt/` or `collabtext/` includes in Markoff Core or view public headers and no
public `CollabText::Crdt` type references. The complete root build succeeds
across Core, Source, Styled, Canvas and their tests. This closes the public
header dependency leak; `markoff_core` still links the pinned engine internally,
and the A07 production storage/identity cutover remains open.

**Progress (2026-10-13):** changed `collabtext` from a `PUBLIC` to a `PRIVATE`
dependency of `markoff_core`. The four test sources that directly include old
engine headers now declare `collabtext` themselves. The full root build passes,
and focused session, anchor, D2 convergence and canvas folding tests pass 4/4.
An ordinary Core consumer's link command no longer lists `collabtext`; the
shared Core library still has its runtime `DT_NEEDED` dependency until A07's
engine replacement. This removes the old engine from Markoff's compile/link
interface without claiming that production now uses candidate Core.

**Progress (2026-10-13):** candidate `BlockIdentity` now has stable actor-then-
counter ordering and hashes over the complete pair, with a relocated consumer
regression for equal counters from distinct actors in both standard and Qt hash
maps plus a block-route token round trip. This establishes cache-key behavior
for the candidate type only; current
`Markoff::BlockId`, numeric structural operations, token/API migration and the
production build boundary remain open. A three-replica regression now creates
same-value sibling blocks independently on two actors, exchanges the offline
operations in opposite orders, and verifies full identities and structural
order converge without treating application values as keys.

**Progress (2026-10-13):** production `Markoff::BlockId` now has an additive
structural identity form backed by all 16 actor bytes plus the actor-local
counter. Equality, ordering, Qt hashing and the current core's unordered-map
hash all preserve the full identity; zero actor/counter inputs are rejected,
and structural IDs remain distinct from temporary numeric scaffolding. The focused
`tst_block_id` test passes. The unused MBI1 token encoder/decoder has been
removed; the view-local opaque-frame key now derives directly from actor bytes
and counter. This does not route document
operations through the new form: current callers still use `raw()` and the
current `IdList` operation path, while `raw()` fails fast for structural IDs to
prevent accidental narrowing. The production package build and A07 exit
conditions remain open.

**Progress (2026-10-13):** the early adapter bridge between candidate
`BlockIdentity` and current `Markoff::BlockId` has been removed, along with its
numeric-ID rejection test. It was not used by `MarkoffDocument` and advertised a
second production-facing identity API without integrating the candidate list.
The adapter now exposes the full actor/counter identity directly; production
`MarkoffDocument` still needs the coordinated list, text and map port.

The installed-package consumer rebuild passes all three CTest targets (3/3)
after this removal. `readelf` confirms its provider test has no dependency on
`markoff_core` or a CollabText shared library; candidate Core remains statically
linked into the isolated adapter test. This keeps the candidate contract
independently testable while the production port is unfinished.

**Progress (2026-10-13):** the root production `markoff_core` rebuilt against
the pinned implementation, and focused structural-ID, operation-codec, and
installed-package lifecycle tests pass 5/5 with the offscreen Qt platform. This
is a clean baseline check, not candidate-engine integration. The attempted
all-target rebuild was stopped during test executable recompilation; the core
library itself had linked successfully. The next A07 code change must switch
document block identity, list projection and per-block buffer ownership as one
unit, then remove numeric construction and serialization paths rather than
adding another bridge.

**Progress (2026-10-13):** the production pending-publication cache now keys
buffer payloads with full `BlockId` values instead of narrowing them to
`quint64`; the dedicated internal key also preserves target type and operation
identity. A focused MarkoffDocument test verifies two actor-distinct structural
IDs with the same counter retain separate pending payloads. The complete
Markoff CTest set excluding `tst_benchmark` passed 213/213, and the relocated
alpha.5 consumer suite passed 3/3. Remote `MarkoffOp` routing, operation codecs,
structural IdList projection, and production Core build integration still use or
depend on the current numeric implementation.

**Progress (2026-10-13):** `MarkoffOp` now carries one `BlockId` target field,
and `MKOF` has one supported encoding: schema 2 with the complete actor bytes
and counter for block targets. Schema 1 numeric decoding and encoding are
removed. Encoding refuses numeric buffer IDs, and decoding rejects malformed,
null buffer identities and non-null identities on non-buffer operations. The
serialization, operation-type and local-emission tests pass. Current
`MarkoffDocument` still creates numeric IDs in its current CRDT path, so its
buffer operations are not yet serializable under this contract; structural
list allocation and production Core routing must land before that path is
usable end to end.

**Progress (2026-10-13):** source/styled opaque-frame reconciliation now keys
snapshots by complete `BlockId` and compares the same full identity key on the
QTextFrame boundary; Canvas accessibility hashes use `BlockId::identityHash()`
instead of narrowing to `raw()`. This prevents same-counter/different-actor
structural IDs from aliasing in these view caches. The normal root build passed
for the full target graph. The three focused tests and the full normal CTest set
excluding `tst_benchmark` pass (213/213). This closes those view-cache narrowing
sites only; document operations and remaining numeric allocators still block
the production identity port.

**Progress (2026-10-13):** removed the three process-static numeric block
allocators from structural edits, D2 insertion and the test fixture path. All
document-local creation now uses the existing shared document allocator, and a
regression mixes fixture, D2 and structural insertions to prove their IDs stay
distinct. The focused structural test passes. This removes accidental
process-wide state and allocator-range coupling; IDs are still numeric inside
the current engine, so provider-backed actor/counter identity and Core list
projection remain open.

**Finding (2026-10-13):** the current document path still collides when one
replica inserts after receiving another replica's blocks. In
`tst_canvas_accessibility::structure_remote_edit_insert_remove_create_destroy`,
both replicas allocate the same numeric block value from their document-local
counter; the remote insertion aliases the first block, so the accessible tree
has three rows but emits no distinct `ObjectCreated`. The test fails both in the
full workspace and when run alone. A numeric high-water adjustment would only
mask this ordered-delivery case and would not support concurrent offline
creation. The required fix is the A07 full actor/counter identity port together
with Core list projection; no numeric compatibility identity should be added.

**Revalidation (2026-10-13):** rebuilt the complete `build-dev` target graph
successfully from the current worktree. The focused offscreen command
`QT_QPA_PLATFORM=offscreen build-dev/bin/tst_canvas_accessibility
structure_remote_edit_insert_remove_create_destroy` reproduces the same
`ObjectCreated` count failure (0, expected 1). The broad offscreen CTest run
completed with this as its sole failure among the 213 tests selected by its
filter; the installed-package provider and scope probes passed. The worktree
therefore remains an unverified green baseline, and the A07 collision remains
the next production cutover gate.

**Progress (2026-10-13):** added `MARKOFF_USE_COLLABTEXT_VNEXT` as a root
configuration that finds the installed `CollabText::Core`, adds the Markoff
vNext adapters, and excludes the pinned `collabtext` submodule from that target
graph. A configure-time guard rejects a parent target graph that already has
the pinned `collabtext` target. The candidate Core link command contains the
vNext adapter and installed `libcollabtext_core.a`, with no pinned-engine
library; the synthetic mixed-engine configure is rejected as intended. The
default configuration still builds `markoff_core` successfully. The
candidate-only production compile reaches the existing Core sources and fails
at `src/AnchorConversion.h` (`<crdt/Anchor.h>`) and `MarkoffDocument.cpp`
(`<crdt/IdList.h>`). This confirms that anchors and document engine ownership
must be ported together; it does not yet build the target or close A07.

**Revalidation (2026-10-13):** rebuilt `markoff_core` in the default
configuration and reproduced the candidate-only failure at those same two
legacy includes. The provider-backed production target cannot progress by
changing CMake linkage alone: `MarkoffDocument::Private` owns `IdList`, five
legacy sibling maps and per-block `Buffer`s, while its constructor wires local
undo/redo directly to those owners and its edit paths read and mutate them
directly. The candidate equivalents are `ScopedIdListStream`,
`ScopedTextStream` and `ReplicatedMapStream`; they return staged operations
that must pass through `ActionPublicationCoordinator` before becoming
accepted state. A07/A08 therefore begin with that coordinated ownership and
publication replacement, followed by anchor/session migration. Default Core
build passed; the relocated Alpha.5 consumer suite passed 3/3, including five
consecutive provider-test runs. This is a revalidated boundary, not production
integration evidence.

**Build-surface progress (2026-10-13):** the candidate adapter, Core and parser
targets now install their public headers and advertise `include` install
interfaces. The candidate adapter is registered in `CorbomiteTargets`, so
`markoff_core`'s private link dependency can be exported with it. A temporary
parent-project configure generated the combined export set successfully, and
the default `markoff_core` target rebuilt after these changes. This verifies
CMake export composition only; a staged installed-consumer build still awaits
the production engine port. The exported candidate adapter refers to
`CollabText::Core`; the eventual parent package config must discover that
dependency before consumers use the adapter target. The candidate adapter then
built in the parent-shaped tree and its install script staged the archive plus
all 23 vNext public headers successfully. This still does not stage the full
Core package or compile MarkoffDocument against the candidate.

**Export audit (2026-10-13):** inspected the generated installed-target export
from the parent-shaped configure. It rewrites the adapter's build alias
`Markoff::TextUnits` to `Corbomite::TextUnits` and leaves `CollabText::Core` as
an explicit imported dependency; the Core interface likewise names exported
`Corbomite::markoff-parser` and `Corbomite::TextUnits`. The eventual parent
package config must discover Qt, KF6 and `collabtext` before loading this
export. This confirms generated interface references only; it is not an
installed consumer or staged Core acceptance.

**Port boundary confirmation (2026-10-13):** the pinned `IdList` operation
stores Markoff's supplied `uint64_t` as an application value; visible reads
return those values, while insertion origins expose only the old 16-bit replica
and Lamport counter. Neither field can be converted into the provider actor's
full identity. Per-block sibling-map operation keys and `AnchorJson` also still
serialize the current numeric form. These are one identity cutover surface:
do not migrate map/anchor codecs by themselves or add a numeric reader/writer
bridge. Replace list projection, `BlockId`, per-block routes and their codecs
together through provider-backed candidate operations.

**Revalidation (2026-10-14):** a fresh `MARKOFF_USE_COLLABTEXT_VNEXT` build
compiles the candidate adapters, then fails in production Core at
`AnchorConversion.h` (`crdt/Anchor.h`) and `MarkoffDocument.cpp`
(`crdt/IdList.h`). A current source scan finds 57 engine or raw-ID call sites
in `MarkoffDocument.cpp`, plus direct legacy ownership in
`MarkoffDocumentPrivate.h`, anchor conversion, and `AnchorJson.cpp`. The
concentrated operation paths are constructor commit/undo wiring, remote apply,
structural edits, D2 edits, document import/reset, and block-qualified anchor
resolution. These paths all read or mutate the same list/buffer/map owners, so
the production cutover begins by replacing that ownership and action boundary
as one unit; an isolated serializer or ID change cannot establish the exit.

**Progress (2026-10-13):** extracted UTF-8/UTF-16 boundary conversion into the
independent `Markoff::TextUnits` static target. Both current Core and the
installed-package adapter now consume that target; the adapter no longer
compiles `../markoff-core/src/TextUnits.cpp`. The relocated alpha.5 consumer
rebuild passes 3/3, and its provider executable has no runtime dependency on
`markoff_core`, the helper archive, or shared CollabText. Root Core and Canvas
build; Live also builds in its separate configuration. The text-unit tests
pass 4/4, focused Canvas folding passes 16/16, and Live coordinate conversion
passes 12/12. This removes one source-tree dependency only; production engine
integration and structural identity cutover remain open. A clean parent-host
CMake configuration with apps, root tests and Live disabled also built
`markoff_core` successfully, and its generated `CorbomiteTargets.cmake` includes
both `markoff_core` and the exported `TextUnits` dependency. A full staged
install was not completed: the host install script traverses unrelated
unbuilt vendored and product targets, and building the entire host graph started
compiling the vendored engine's full test suite. The production package's own
3/3 installed-consumer checks remain the package evidence for this slice.

### A08 — port actual document text, maps and structural editing

Replace production Buffer/IdList ownership and private operation-field access.
Keep D2 parsing, serializers, exact source handling, proxies and QObject ownership.
Route per-block edits, paste, structural commands, formatting, completion and
programmatic insertion through the action coordinator. Wire every sibling map
family and its typed/raw source values through full actor/counter operations.
Choose returned operations as the single publication source or specify an equivalent
callback policy without publishing twice.

**Exit:** real MarkoffDocument edit/serialization behavior runs through scoped Core
and provider actions. Unicode refusals are atomic; multi-block commands and metadata
changes converge with exact routing. Tests exercise production code, not only adapters.

**Progress (2026-10-14):** accepted-prefix refresh now preserves existing
list/text/map stream object addresses while replacing their engine state from a
fresh replay. Router bindings therefore survive committed publication and
definite rollback; routes created only by a rejected action reset to empty
state in place. The installed consumer suite exercises lifecycle-owned
list/text/map staging, router use after publication, and malformed-action
rollback, plus the lifecycle owner's exact action/history publication overload.
Release build and CTest pass 3/3. This establishes an adapter
ownership seam for A08; production `MarkoffDocument` dispatch, import and view
notifications remain open.

**Progress (2026-10-14):** candidate-mode `MarkoffDocument::applyBlockEdit`
now edits an owner-held scoped text stream, records the exact operation identity
in the action-history index, and publishes through the lifecycle's staged-action
boundary. A focused provider-backed document test seeds and reopens one accepted
block, checks tentative/committed signals and edited text, then verifies fresh
replay. The test passes. This covers one per-block edit path only; structural
commands, map publication, undo and host-consumer completeness remain open.

**Progress (2026-10-14):** candidate `applyStructural(ChangeKind)` now writes
the typed block-kind value through the `kind-tag` map stream and publishes its
reversible singleton map-history association with the action. The focused
provider-backed document test verifies that the changed kind survives a fresh
open after a text edit. Both candidate and default `markoff_core` builds pass;
the candidate library still lacks the legacy D2 methods required by a host
consumer, and list insertion/removal, attribute/link maps and durable undo are
unimplemented.

**Progress (2026-10-14):** candidate `applyStructural(InsertEntry/RemoveEntry)`
now stages the order-list operation and corresponding kind-map write/removal in
one MAB, with exact operation identities and reversible map-history metadata.
The provider-backed test verifies insertion order, inserted kind, removal, and
the accepted list after fresh replay. Insertion text, view commands and undo
remain open.

**Progress (2026-10-14):** candidate `setBlockAttribute`,
`setFrontmatterValue` and `setLinkReference` now encode and publish typed or raw
values through their routed sibling-map streams; matching remove methods publish
reversible map tombstones. `toggleListItemChecked` uses
the attribute action path and preserves its existing no-op conditions. The
provider-backed test verifies attribute, frontmatter and link-reference values
after fresh replay, and verifies the checked-state inverse on a list item. Both
candidate/default Core builds and the focused CTest pass. Import-driven map
population, deletion APIs, export integration and durable undo remain open.

**Progress (2026-10-14):** candidate Core now implements the per-document
`Session` lifecycle, and the focused document test links the shared
`Markoff::Core` library instead of compiling its implementation directly. It
passes alongside the storage-provider regression, and the candidate Core has
no undefined `MarkoffDocument` symbols. The Styled demo entry point and its
Markdown renderer now use provider-backed open/import in candidate builds; the
default Styled app still builds. Candidate Styled, Canvas, and Source apps now
all build and link against provider-backed Core; the Canvas offscreen smoke
app also runs successfully and is registered as a CTest in app-enabled builds.
Their candidate CMake gates now expose the demo targets for vNext builds. This
proves app entry-point construction and linkage, while interactive editing,
persistence choice, host-specific startup and full production ownership remain
open under A08/A12.

**Progress (2026-10-14):** candidate commands and Canvas routes now cover
multi-range formatting, atomic block-payload swaps for line movement, structured
Markdown paste with original-tail restoration, and table insertion with its
template in the same split action. Focused Core tests verify paste and swap
undo/redo after provider reopen; Format Ops tests cover per-block and multi-block
format undo; Canvas session tests verify payload-swap undo, typed paste/tail
restoration, and single-action table-insertion undo. These complete additional
candidate view commands, while full production ownership, peer routing and
installed host-consumer acceptance remain open.

**Provider-backed Source paste follow-up (2026-10-14):** multiline pastes in
the Source `QTextDocument`, including replacement of a non-empty selection,
route through structured range paste. Inserted blocks, target-tail restoration,
and text/list/kind/attribute operations publish as one action; the binding
restores the caret after the pasted content. Regressions cover collapsed paste,
same-block replacement, cross-block replacement, one-action undo/redo and the
original end-block tail. Focused Release and ASan/UBSan tests pass, and the
complete candidate Release suite passes 94/94. A fresh provider-open with no
registered initial import has no visible block, and the lifecycle rejects
ordinary actions in that state; structured paste remains gated on initial
import registration rather than bypassing it. Empty Markdown import creates a
blank paragraph, which structured paste replaces in one action; undo restores
that paragraph and redo reapplies the pasted blocks. The focused test and full
candidate Release suite pass 94/94. The
docs-excluded worktree fingerprint is
`caf9acc0db8b63443cf24d0fe6f516981c3c0538720f676f0c72377eabac7f0c`
(1,707 files, 83,449,299 bytes); installed Markoff and alpha.5 prefix hashes
remain unchanged.

### A09 — import, reopen, external replacement and export

Implement one durable initial import and registration intent; peers replay its exact
seed. Fresh engine replay precedes local-history restore; discarded views are not
used to replay already-observed operations. Expose an accepted replacement action
for external/plugin bytes through normal publication, including closed registered
document service use. Preserve identities/references according to specified replacement
semantics. Provide revision-bound export/acknowledgment and independently fallible saves.
Cover Corbomite's `modify/process`, link rewriting and external reload requirements
with Markoff-side host-shaped fixtures while Corbomite stays paused.

**Exit:** initial-import/replacement/reopen crash traces, same-history peer replay,
stale export acknowledgement and export failure behave as contracted. Untouched setext,
blockquotes, CRLF, trailing whitespace and final-newline cases remain byte-identical.
Markdown-only import/fork creates a new namespace/history and does not inherit undo.

**Progress (2026-10-14):** candidate `importInitialMarkdown` now parses source
with `Markoff::Document`, stages the ordered blocks, per-block initial text,
typed kinds and attributes, frontmatter, footnote definitions, and link-reference
records, and publishes them as one authorized initial MAB with an exact MDS/1
source snapshot and complete owner history. A retry after prepublication staging
failure reuses the persisted transaction authorization and rejects changed source
bytes. The provider-backed regression
imports a frontmatter document with heading and paragraph, verifies typed
projection values and exact source bytes, and reopens from accepted replay. Both
candidate/default Core builds and the focused CTest pass. External replacement,
complete Markdown fidelity, and peer import replay remain open.

**Progress (2026-10-14):** candidate export now returns Markdown bytes together
with the accepted revision token. Pristine documents export their exact MDS/1
source bytes; edited documents serialize from the accepted projection. Export
acknowledgment rejects stale revisions and bytes that differ from the current
projection, then persists the provider's export watermark. Candidate `save()`
uses `QSaveFile`, reports open/write/commit/acknowledgment failures, and only
acknowledges after a successful commit. The regression covers exact pristine
bytes, stale and altered acknowledgment rejection, current watermark persistence,
and successful/failed file saves. Candidate Core and focused CTest pass. Full
source-fidelity behavior after edits, external replacement, and peer import
replay remain open.

**Progress (2026-10-14):** `importInitialMarkdown` now requires UTF-8 bytes to
round-trip through `QString` before parsing or persisting an import intent. A
focused regression submits malformed source, verifies the lifecycle remains
`awaitingInitialImport`, then successfully imports valid Markdown on the same
document. This prevents replacement-character parsing from diverging from the
exact source bytes retained for replay/export; broader source-fidelity and
replacement coverage remain open.

**Progress (2026-10-14):** pristine-source export now has a focused fidelity
matrix for setext headings, blockquotes, CRLF line endings, trailing whitespace,
and missing final newline. Every case is imported, exported byte-for-byte,
reopened from accepted replay, and exported byte-for-byte again. The candidate
provider-backed document test passes; edited-source fidelity and replacement
semantics remain open.

**Progress (2026-10-14):** text-only accepted edits now retain parser-derived
source spans and imported text baselines. Export patches only changed block
spans, keeps original separators and untouched bytes, and restores a line ending
when it falls inside a replaced span. The provider-backed regression edits a
paragraph beside setext syntax and whitespace-bearing text, then checks the
exact export both before and after accepted-prefix reopen. Candidate consumer
CTest passes. External replacement and same-history peer import remain open.

**CRLF edit follow-up (2026-10-14):** added an edited-source case with CRLF
heading and paragraph separators. The test changes the paragraph while
requiring the untouched heading and every CRLF byte to remain exact, both
before and after accepted-prefix reopen. The provider-backed consumer CTest
passes.

**Frontmatter edit follow-up (2026-10-14):** frontmatter-only accepted edits
now patch the parsed frontmatter span in the retained source snapshot while
leaving the Markdown body bytes unchanged. Delimiter line endings follow the
original frontmatter, and an unchanged YAML payload returns the exact original
source. A CRLF regression includes setext syntax, blank separators and trailing
spaces and checks exact export both before and after accepted-prefix reopen.
Candidate provider-backed CTest passes. Structural and link-reference edits
still use canonical serialization; external replacement remains open.

**External replacement foundation (2026-10-14):** added a distinct replacement
MAB action kind with mandatory exact MDS source bytes, normal accepted-prefix
replay, and owner-history ancestry. `MarkoffDocument::replaceMarkdown()` parses
replacement bytes and stages list removals/inserts, old metadata removals, new
text/maps and source snapshot in one action. Replacement assigns fresh block
identities while retaining document history; a provider-backed regression
checks fresh identities, typed metadata, exact source export, reopen replay and
a later text-only source patch. Durable append-before-rename and
append-after-rename fault cases prove that definite absence retains the old
source/identities and uncertain publication resolves from fresh accepted-prefix
replay. MAI/3 now persists the exact source immediately before replacement;
undo and redo restore source and identities across reopen, including a prior
frontmatter-only edit. The provider-backed and storage codec tests pass. Peer
import replay remains open.

**Peer replay boundary (2026-10-14):** same-owner fresh reopen is covered, but
the reference provider cannot currently construct a second owner against the
same durable root: each provider opens the root's single `ReplicaState` writer
lease, and the second `makeReferenceDocumentStorageProvider()` is rejected with
`writer lease holder is active`. This is a harness/provider limitation that
also exposes the missing receiver-owned reservation and remote action contract
tracked under A11. It does not establish same-history peer replay; that exit
remains open pending the A11 routing contract.

**Reference-provider document reopen (2026-10-14):** added a candidate
`MarkoffDocument` regression using the restartable reference provider in
`durable` mode. It imports two blocks, edits each block, destroys both document
and provider, creates a new provider for the same private root, and opens the
same document scope. The test verifies the exact block-ID vector and both text
values, then undoes and redoes the last accepted edit from the restored owner
history. The focused QTest case passes. This proves the candidate document
lifecycle uses the restartable provider path; it does not substitute for
Corbomite's production storage implementation or multi-stream crash-fault
acceptance.

**Durable replica convergence (2026-10-14):** two restartable providers now
start from one accepted initial-import MAB, make concurrent edits on separate
block text streams, and exchange the exact local MABs in opposite orders. Both
projections converge; after destroying both document/provider pairs and opening
fresh providers against each durable root, exact block identities and text
remain equal. Each reopened replica can selectively undo its own local edit
while retaining the peer edit, then redo and reconverge. The focused candidate
MarkoffDocument test passes. This covers durable independent text-stream
convergence and local history, not mixed text/map/list concurrency or crash
injection between peer commits. The covering test file's SHA-256 at that
checkpoint is `c0ee3fa68cdd767f3b203afb5edfe4f88b7dea11ef6b7b74395ec5aedfb5`.

**Durable mixed-primitive convergence (2026-10-14):** extended the durable
replica fixture with concurrent local actions of different shapes. Replica A
merges adjacent blocks in one MAB spanning survivor text, IdList removal and
kind-map tombstone operations; replica B writes a block attribute map value on
an unaffected block. Each replica first commits its own action; B's exact MAB is
accepted by A, then both document/provider pairs close before A's MAB reaches B.
After fresh provider opens, A retains the committed peer map action while B
still has its local action. Retrying A's MAB to B and B's exact MAB to A
converges both replicas on block identity/order, text and map value. Selective
undo on each side preserves the peer's change, and redo reconverges.
`tst_foundation_markoff_document` passes with this case; test file SHA-256
`21b541b870716d2247374778e910f647681e4ed852d34da87d4bbd3c99f47092`. This
tests reopening between peer commits, while injected crash/failure points and
durable transport pending state remain open.

The production merge regression now decodes replica A's exact emitted MAB and
asserts that one action contains text, IdList and Markoff-map operations before
it continues through peer delivery, provider reopen, selective undo/redo and
convergence. The focused `tst_foundation_markoff_document` passes in Debug,
Release and ASan/UBSan builds.

**Link-reference import follow-up (2026-10-14):** candidate import now parses
the definition label, destination, and optional quoted title into the routed
link-reference map. Labels are normalized for lookup, while frontmatter-only
export continues patching the original source and preserves the original label
spelling. The provider-backed import/export regression checks a label longer
than the former 40-byte key prefix, URL/title lookup, pristine byte fidelity,
and retention after a frontmatter edit. Focused CTest passes; full CommonMark
link-reference syntax coverage remains open.

**Parser follow-up (2026-10-14):** link-reference definitions now expose their
label, destination, and optional title from the tree-sitter syntax node, and
provider-backed import consumes those fields instead of reparsing the raw block
as one line. Parser and provider tests cover a multiline label, destination,
and title, including exact source preservation after a metadata edit. Both
focused test binaries pass in Release. Import decodes HTML named/numeric
references and removes backslash escapes for destinations and titles, while
link labels preserve their source spelling for lookup normalization. Provider
coverage checks escaped and entity-spelled labels, angle-bracket and
balanced-parenthesis destinations, quoted and parenthetical titles, and
escaped title punctuation. Import keeps the first definition for a normalized
duplicate label, as required by CommonMark; a provider regression checks
spacing and case normalization across duplicates. The provider-backed Release
test passes with all these cases, and the parser top-level-block Release test
passes 28/28. Both focused tests also pass in the ASan/UBSan build after
rebuilding parser, Core and consumer targets from the current public header.
More exhaustive CommonMark conformance cases remain open.

**Multiline title follow-up (2026-10-14):** a provider-backed fixture now puts
a soft line break inside a quoted reference title. Import converts that line
break to a space in the semantic title, while a metadata-only edit keeps the
original newline byte in the Markdown source. The focused Release provider
test and the full candidate Release suite pass (94/94); the focused
ASan/UBSan provider test also passes. Broader CommonMark conformance remains
open.

The same regression now covers CRLF soft breaks inside a title, confirming
they normalize to one semantic space and the exported source retains the
original CRLF. The focused Release test, full Release suite (94/94), and
focused ASan/UBSan test all pass.

**CommonMark label normalization correction (2026-10-14):** reference labels
now use Unicode case folding and collapse only ASCII spaces, tabs and line
endings. They retain backslash escapes and entity spellings during matching,
as the spec requires; decoded URL and title values are unchanged. Labels over
999 Unicode code points are omitted from the reference map and remain ordinary
paragraph blocks. Provider tests cover distinct escaped/entity spellings, the
999-character accepted boundary, and 1,000-character paragraph fallback.
Focused Release and ASan/UBSan provider-backed tests pass after the correction,
and the full candidate Release suite passes 94/94. The updated docs-excluded
worktree fingerprint is
`e438b7d5d77b84bc199770936c739295c7294c22d0fdf8f2db613b80580990ef`
(1,707 files, 83,422,304 bytes); installed Markoff and alpha.5 prefix hashes
are unchanged.

**Markdown fork follow-up (2026-10-14):** a provider-backed regression edits
Markdown in one document scope, imports its exported bytes into a second scope,
and verifies the edited text survives while neither block-level nor
document-level undo can revert the source document's private history. The
focused Release and ASan/UBSan provider-backed tests pass.

**Peer import replay follow-up (2026-10-14):** two provider-backed replicas
now register peer initial-import and replacement MABs through
`MarkoffDocument::acceptRemoteAction()`. The receiver reconstructs exact source
and export state, then keeps its own later edit and undo history through a fresh
provider reopen. The action-consumer regression also verifies two-way import
and replacement delivery with receipt acknowledgment. Release suite and
consumer tests pass; durable production transport remains under A11.

### A10 — implement document and per-block action undo

Apply A04's group-selection semantics through normal durable action publication.
Persist shared compensation and private action mappings consistently; restore after
replay. Verify multi-stream undo/redo, maps, structural edits, group coalescing and
retirement backpressure. Allocation/append failure midway through compensation
must rebuild/reconcile the whole tentative action, not accept a subset.

**Exit:** selective regression from A04, global undo/redo across streams, concurrent
peer edits, original identity restoration, reopen and fault traces all pass.

**Progress (2026-10-14):** The candidate provider-backed `MarkoffDocument` now
selects the latest not-yet-undone text edit group for a full block identity,
publishes its compensation as one durable `undo` MAB with a parent-action link,
updates owner history, and restores the visible text after publication. The
focused candidate regression verifies `canUndoForBlock`, text compensation and
reopen replay, plus block-map compensation restoring both an absent prior value
and an overwritten value across consecutive undos. Paired insertion undo now
compensates both the order-list group and targeted kind-map entry in one MAB.
Its focused regression reopens the document after insertion undo and verifies
the inserted identity remains absent and no longer offers undo. Paired removal
undo restores the exact full block identity and kind in one publication. Other
block-level redo now restores text, map writes and paired list/kind actions by
following the persisted undo parent chain; consecutive map redo and text redo
after reopen are covered. Candidate `undoD2()` and `redoD2()` now select the
latest eligible action, including standalone untargeted frontmatter and
link-reference map writes; a document-level frontmatter regression covers
undo/redo. Production peer-edit behavior through accepted remote MABs,
retirement/backpressure, compensation allocation/append failure handling and
broader replay/fault acceptance remain open.

**Concurrent text undo progress (2026-10-14):** the provider-backed consumer
now opens two independent durable writer identities for one scoped text route,
delivers a shared baseline, and cross-applies concurrent local insertions. It
then delivers one writer's undo and redo compensations, asserting that undo
removes only that writer's inserted text, retains the peer's text, and that
both projections converge after each compensation. The focused candidate CTest
passes three consecutive runs. Its redo assertion permits either convergent
ordering of the concurrent inserts while requiring both edits to remain. This
proves the scoped CRDT adapter behavior; accepting remote MABs into
the production `MarkoffDocument` and its owner-history policy remain open under
A11.

**Compensation fault follow-up (2026-10-14):** added a durable paired
list/kind undo regression with a definite append-before-rename failure. The
test confirms the tentative structural undo is fully rolled back: both list
entries and the inserted block's kind remain, the tentative change is reverted,
no commit signal is emitted, and the ordinary-action barrier stays closed for
reconciliation. Candidate provider-backed CTest passes. This covers one
append-failure boundary; allocation failures, uncertain publication, retirement
and backpressure remain open.

**Mid-compensation allocation follow-up (2026-10-14):** the reference-provider
fault hook can now skip a configured number of matching commit boundaries. A
paired structural undo test lets one reservation commit, fails the next
reservation before rename, and verifies the entire tentative list/kind undo is
rolled back and the lifecycle enters `recoveryRequired`. The provider-backed
consumer CTest passes. This covers allocation failure after partial staging;
mixed text/list/map grouping, uncertain compensation, retirement and
backpressure remain open.

**Retirement wiring audit (2026-10-14):** `MarkoffDocumentVNext` currently
restores local action ancestry and per-stream snapshots, but it does not emit
`RetiredHistoryAssociation` records or attach retirements to a later action.
There is also no public document API or configured undo-history budget that
defines when a group becomes eligible for retirement. The MAI/MAH codecs can
encode and validate explicit retirement associations, but the production
document has no policy to invoke them. This leaves retirement/backpressure as
an implementation gap requiring a specified retention policy, rather than a
behavior that can safely be inferred from ordinary undo/redo or save operations.

**Mixed compensation failure follow-up (2026-10-14):** the production provider-
backed document regression now creates one merge action containing survivor
text, order-list removals and removed-block kind-map tombstones. It fails the
undo action append before atomic rename, verifies the accepted merged text and
block identities remain intact while ordinary writes are closed, then recovers
the absent transaction and retries undo. The retry restores all original
blocks and survivor text. A companion test injects failure after atomic rename,
then verifies fresh provider reopen resolves the uncertain compensation to the
fully undone state with writes enabled. `markoff_provider_backed_document_test`
passes. These close the mixed-group definite-absence/retry and uncertain-commit
cases; retirement/backpressure and broader fault acceptance remain open.

**Mixed compensation redo follow-up (2026-10-14):** the same durable regression
now completes the recovered undo and then redoes the original merge, asserting
the survivor text, removed order entry and kind projection return together.
The focused provider-backed document executable passes in Release. This closes
the positive redo path for the mixed action; retirement/backpressure and wider
replay/fault acceptance remain open.

**Mixed compensation allocation follow-up (2026-10-14):** the provider-backed
document test now fails a later reservation after one reservation succeeds
while compensating the mixed merge. It verifies the accepted text, order and
kind projection remain wholly merged through recovery, then retries undo and
checks the full original state returns. The focused Release executable passes.
Uncertain, retirement/backpressure and broader fault acceptance remain open.

**Production selective-undo follow-up (2026-10-14):** the provider-backed
`MarkoffDocument` regression now publishes independent text actions for blocks
A and B, selectively undoes A, reopens from accepted history, then redoes A.
The assertions verify B remains unchanged and independently undoable throughout.
The same regression publishes separate structural insertions A and B, each
covering the shared order list and a targeted kind map. Selectively undoing A
retains B's identity and kind, and redo restores A after accepted-prefix reopen.
The focused consumer CTest passes. A later regression adds one production merge
action spanning survivor text, shared order-list removals and removed-block
kind-map tombstones; selective undo restores all three groups and redo reapplies
them after provider reopen. Retirement/backpressure still requires an explicit
retention policy.

**Mixed-action selective undo/redo follow-up (2026-10-14):** `undoForBlock()`
and `redoForBlock()` now replay every group in the selected action, resolving
each text route independently and compensating all map/list groups. The durable
regression merges two blocks, selectively undoes through the survivor, reopens,
then redoes; it verifies the original block identities, text and kind return on
undo and the merged projection returns on redo. The focused provider-backed
test and full candidate Release CTest pass (94/94). Retirement/backpressure
remains open pending a specified retention policy. The docs-excluded worktree
fingerprint at that implementation point was
`5aeb47c8646d4830713d495218679a93e146b6dbdda9427ea312939e0e0cfb48`
(1,707 files, 83,450,948 bytes); installed Markoff and alpha.5 prefix hashes
remain unchanged.

**Mixed undo/redo sequence follow-up (2026-10-14):** selective undo/redo now
advances edit sequences for each touched block, advances the structural
sequence when list or kind-map state changes, and emits insertion/removal row
signals from the before/after block order. The durable mixed-merge regression
asserts both block sequences and the structural sequence advance for undo and
redo. Full candidate Release CTest passes 94/94. The refreshed docs-excluded
worktree fingerprint is
`75bee1ebb1b552f1ecf61945069e3a37f8eedd8f45eaea3c8d2f6ef5cee5f707`
(1,707 files, 83,458,773 bytes); installed Markoff and alpha.5 prefix hashes
remain unchanged.

**Candidate command follow-up (2026-10-14):** Core now publishes structured
Markdown paste and payload swaps as atomic actions; format operations can
publish across multiple blocks as one MAB. Provider-reopen tests cover paste,
swap and formatting undo/redo. Canvas table insertion now includes its template
in the same split action, and a Canvas regression verifies one undo removes the
whole insertion. The focused candidate tests and configured offscreen suite
pass; peer-originated compensation, retirement/backpressure and broader
compensation-fault acceptance remain open.

**Remote peer undo follow-up (2026-10-14):** the candidate production
`MarkoffDocument` consumer now accepts one peer MAB containing a remote text
insertion and a block-attribute map write. A subsequent local text edit is
selectively undone and redone while the peer text remains visible; the remote
map value and receiver-local history are preserved. The focused consumer and
full configured suite pass. The provider-backed document regression now also
accepts peer-authored `ActionKind::undo` and `ActionKind::redo` MABs after a
reopen, removes and restores only the peer insertion while retaining a later
receiver-local edit and map value, and returns the same revision on exact undo
retry. Two-document convergence remains open.

The same focused test now opens two provider-backed replicas from one accepted
initial MAB, delivers concurrent peer text actions in opposite orders, and
verifies both recovered projections contain both edits and have identical text.
This covers text-prefix convergence; durable cross-stream convergence and
production transport/receipt integration remain open.

### A11 — finish production routing, recovery and receipts

Wire bounded text/map/list routing into the document. Validate scope, version,
primitive, target and decoded identity before apply. Retain unknown streams,
deferred operations and original rejected envelopes with explicit retry/quarantine
ownership. Persist quarantine before acknowledging it. Enforce cross-bundle exact
identity uniqueness transactionally; Core duplicate outcomes alone do not compare
old bytes. Advance applied receipts only after the whole recoverable action is
accepted by the projection. Drain waits and refresh parse/layout state coherently.

**Exit:** duplicate/reordered delivery, unknown stream registration, list dependencies,
capacity/backpressure, malformed input, forged/reused identities, gaps and restart
produce explicit outcomes without lost bytes or premature receipts.

**Progress (2026-10-13):** accepted-prefix rebuild now preflights every MAB's
decoded operation identities against a prefix-wide `(stream, actor, counter)`
set before applying any record. A durable-reopen regression appends a second,
otherwise valid MAB reusing operations from the first and verifies that no
projection is returned. The reference provider also keeps an accepted identity
owner index for local preparations. A bounded list router now retains unknown
order-stream envelopes, drains causally deferred inserts after dependencies
arrive, sorts completion batches by delivery sequence and bundle index even when
Core internally drains its deferred queue, and preserves quarantine/backpressure
ownership. A reversed three-insert chain covers transitive dependency drain.
The map-router regression also verifies bounded unknown-stream retention,
registration drain, retry after backpressure, and explicit duplicate and stale
outcomes while preserving the newer LWW value. It also caught and fixed an
admission bug: held-envelope capacity no longer blocks a valid operation that
can be immediately applied to a registered map, while a zero-budget router still
backpressures an unknown stream without retaining its envelope.
Production `MarkoffDocument` routing and receipt integration remain open; the
accepted-prefix lookup and isolated router conflict checks are recorded below.

**Routing audit (2026-10-13):** `ScopedOperationRouter::applyKnown()` returns
applied/duplicate envelopes to the caller, but only unknown, deferred and
rejected envelopes remain in its bounded held queue. `offer()` checks identity
conflicts only against that queue. After an operation has applied, the router
has no identity-to-original-bytes evidence, so a later changed envelope with
the same scoped actor/counter reaches Core's duplicate handling. The lookup
and router checks below address this in the isolated adapter; production
`MarkoffDocument` wiring and durable receipt integration remain open. Counting
a Core duplicate alone as an exact receipt would not satisfy the requirement.

**Progress (2026-10-13):** `AcceptedPrefixProjection` now retains implicitly
shared references to the exact accepted records and provides an on-demand
`findAcceptedOperation(stream, actor, counter)` lookup. It scans MAB/3 records
and returns the original decoded payload plus action ID, delivery sequence,
operation index and bundle count; it checks the record transaction ID and uses
the projection's own replayed prefix, and returns that record's exact prior and
committed revision tokens. The durable three-stream consumer
regression verifies exact list, text and map lookup. This supplies the
accepted-history comparison primitive without a router-owned payload cache;
production document wiring and receipt integration remain open. Installed
alpha.5 consumer CTest passes 3/3.

**Progress (2026-10-13):** text, list and map routers can borrow
`ProviderBackedDocumentState` and consult its current accepted projection when
Core reports a duplicate. They emit a duplicate completion only when the
scoped identity, payload and MAB envelope metadata match accepted history;
changed envelopes enter the existing bounded quarantine with `actor_conflict`.
A durable consumer trace covers exact and changed envelopes for all three
primitives, proves a quarantined conflict does not shadow a later exact replay,
and keeps the text router alive across publication to verify it reads the
refreshed projection. Production `MarkoffDocument` wiring and durable
quarantine receipt integration remain open. Installed alpha.5 consumer CTest
passes 3/3.

**Progress (2026-10-14):** the text router now rejects noncanonical block
stream keys before holding or applying an envelope, and stream registration
rejects noncanonical scopes before draining queued operations. A regression
offers a valid operation under a forged route and verifies `wrong_stream` with
no retained envelope. This closes the same route-validation invariant already
enforced by list/map routing; production `MarkoffDocument` receipt wiring
remains open.

Text quarantine recovery also rejects a validly encoded MQR record carrying a
noncanonical block route, and verifies the bounded router remains unchanged
after refusal. The relocated consumer suite passes 3/3.

**Progress (2026-10-13):** `DocumentStorageSession` can now retain opaque,
bounded quarantine records idempotently and return them in its recovery snapshot.
The reference provider persists those exact bytes in MREF/3; a consumer test
closes and reopens the provider and verifies byte-identical recovery, including
an embedded NUL and an exact retry. The build and provider test pass. Router
acknowledgment is now coupled to persistence in text/list/map routers. MQR/1
captures the full scope, operation identity, target where applicable, payload,
delivery metadata and typed reason. A durable-provider lifecycle regression
verifies each primitive round-trips its rejected envelope through this path.
Text, list and map routers now also restore matching MQR/1 records from recovery
snapshots as one bounded batch; malformed or over-budget batches leave the
router unchanged. A provider close/reopen trace restores an exact text
quarantine into a new router, and primitive-specific tests restore all three
route types. An injected post-rename uncertainty returns `commit_uncertain`,
keeps the router hold, and recovers the committed envelope after a fresh
provider opens. Production `MarkoffDocument` wiring remains open. The complete
relocated suite passes 3/3.

The durable three-stream acceptance regression also asserts that its list and
map operations may use the same actor and counter because their identities are
scoped by stream. Do not add a global `(actor, counter)` conflict rule; the
provider and accepted-prefix uniqueness key is `(stream, actor, counter)`.

**Remote acceptance storage follow-up (2026-10-14):** added a distinct
`DocumentStorageSession::acceptRemoteAction()` transaction path. A
Markoff-validated remote MAB can be durably appended with its exact bytes and
scoped identity claims, without receiver-owned reservations or changing the
receiver's private undo history. The reference provider rejects stale accepted
prefixes, reused identities and changed transaction retries; exact retries
recover the committed revision. MREF/4 records the remote/local transaction
kind and rejects older experimental MREF versions. A durable close/reopen test
covers identity ownership, changed bytes, local-history isolation, and an
injected post-commit uncertain result followed by exact retry. The provider
regression passes. `ProviderBackedDocumentState::acceptRemoteAction()` now
validates the complete MAB against its document/epoch, transaction ID and exact
operation claims, appends it through the remote provider path, then replays and
refreshes the entire accepted projection before returning committed. Its
consumer regression applies a peer-authored map operation to the receiver's
seeded block and verifies the accepted value and unchanged local undo history.
The focused provider-backed consumer and full configured suite pass.
`MarkoffDocument::acceptRemoteAction()` now builds the remote storage claims,
uses lifecycle validation/replay, updates block/document signals after a new
commit, and returns the stored revision without repeating notifications for
an exact duplicate. Its consumer test applies a peer MAB containing both a
text edit and block-attribute map write, checks the updated projection and
one-time notification, then selectively undoes a later local edit while
preserving peer text. Before provider commit, lifecycle acceptance now replays
the candidate complete prefix and rejects causal deferrals and text/map targets
that are absent from both the prior and resulting IdLists; this permits a
structural insert and its content in one MAB as well as edits to a block removed
by a replacement action. Regressions verify unknown targets leave the accepted
prefix unchanged and peer replacement succeeds. Bounded router deferred/
wait handling, durable quarantine integration at the document boundary, and
external receipt delivery remain open.

**Peer registration follow-up (2026-10-14):** an empty provider-backed
document can now accept a validated peer initial-import MAB through the same
remote transaction path. Replay classifies owner-local history by the next
matching owner event or local operation producer instead of treating every
initial import/replacement as receiver-owned. A MarkoffDocument regression
registers a peer import, verifies the imported source/export projection, makes
a receiver-local edit, and reopens with that edit's undo history intact. The
two-replica convergence fixture now also seeds its second replica through
lifecycle acceptance. Focused provider-backed consumer tests pass.

The same consumer now accepts a peer replacement MAB with fresh block
identities, verifies the exact replacement source export, then commits and
reopens a receiver-local edit with undo history intact. This exposed and fixed
preflight target validation for replacement bundles: operations may target a
block visible before the action even when that action removes it.

**Bounded action routing follow-up (2026-10-14):** added
`MarkoffDocument::routeRemoteAction()` for complete MAB envelopes. It retains
causally deferred actions within configured operation/byte limits, returns no receipt
while held, retries the held prefix after each accepted action, and returns
action-ID/revision receipts for every MAB committed by the drain. The regression
uses a zero-capacity budget to verify backpressure leaves the exact MAB with the
caller, then offers a dependent text action before its insertion dependency,
verifies the document and receipt list remain unchanged, and offers the
dependency to check both actions commit and are receipted in order. Unknown/rejected stream
quarantine and sending these receipts through a production transport remain
open.

**Production queue backpressure regression (2026-10-14):** added focused
provider-backed coverage at the `MarkoffDocument` boundary. Two replicas share
an accepted base action; the sender creates sequential edits to the same known
text stream, and the receiver is offered the later MAB first. A zero-action,
zero-byte budget returns `backpressured`, leaves the queue empty, and reports
`deferred_queue_full`. Raising the budget permits the exact same envelope to be
retained as deferred; delivering its predecessor commits and receipts both MABs
in order, empties the queue, and produces the expected text. The focused
`tst_foundation_markoff_document` CTest passes in candidate Debug and in the
ASan/UBSan build with leak detection disabled for the runner's ptrace limit.

**Deferred retry after reopen follow-up (2026-10-14):** added the public
owner-thread `MarkoffDocument::retryDeferredRemoteActions()` operation for
re-evaluating durably retained causal waits after startup or external stream
registration. A regression persists a dependent text MAB, accepts its dependency
through the provider path, closes and reopens with both records present, then
explicitly retries the wait and verifies its action-ID/revision receipt and final
projection. The focused provider-backed document CTest passes. Durable transport
outbox state and production network transport remain open.

**Document quarantine follow-up (2026-10-14):** permanent rejection from
`MarkoffDocument::routeRemoteAction()` now encodes each operation from the
complete rejected MAB as MQR/2 and persists the records through the active
document provider before returning `quarantined`. MQR/2 preserves the exact
enclosing bytes, including action-level fields, while remaining able to decode
existing MQR/1 router records. A provider write failure
returns `uncertain`, so the caller cannot mistake an unpersisted rejection for
an acknowledged quarantine. A provider-backed regression reuses an accepted
action ID with changed valid bytes, confirms the accepted projection is
unchanged, closes and reopens the provider, and verifies the durable exact MAB,
action ID and `pin_conflict` reason. The focused provider-backed document CTest
passes. `MarkoffDocument::quarantinedRemoteActions()` now exposes recovered
whole-action records after reopen, validates their document scope and action
IDs against their exact MABs, and groups the per-operation records into one
retryable action. The provider-backed regression verifies one exact action is
returned after an exact quarantine retry. The bounded in-memory route now also
holds `unknown_stream` target waits without receipts and retries them after
later accepted actions; a regression verifies an absent target remains pending
instead of entering quarantine. Durable unknown-stream retention across process
restart is now covered by the same MQR/2 recovery journal: wait records are
persisted before the router returns `deferred`, and open restores only the
latest unresolved wait records that are not already in the accepted prefix.
Regressions close and reopen the provider for both a causal text dependency and
an unknown target, then verify the held MAB is still pending. Quarantine
enumeration filters wait records and exact MABs that were accepted after the
wait. Explicit retry is now exposed for restored waits; automatic retry
scheduling and production transport receipt dispatch remain open.

**Action receipt transport follow-up (2026-10-14):** the in-memory transport
now carries complete action envelopes with sender-side pending ownership and
returns action-ID/accepted-revision receipts to the transport peer that
originally offered each MAB. A dependency action can therefore drain and
receipt an earlier wait from a different peer. Exact-byte retransmission keeps
the pending action; rebinding that action ID to changed bytes is rejected.
`CollabConsumer` has an action-bundle mode that routes inbound MABs through
`MarkoffDocument::routeRemoteAction()`, dispatches only committed receipts, and
emits those receipts back to the sender; callers can submit complete envelopes
through `sendActionEnvelope()`. `ProviderBackedDocumentState` invokes a callback
only after a local MAB/3 commits and the accepted projection refreshes;
`MarkoffDocument` exposes its exact action ID and envelope through
`localActionEnvelopeCommitted`. The action-bundle `CollabConsumer` connects that
signal to `sendActionEnvelope()`, so local edits enter the receipt-aware
transport automatically. `tst_binding_vnext` verifies one local committed edit
emits its MAB/3 bytes, and `tst_vnext_action_consumer` verifies that initial
import and replacements travel in both directions through the real consumer
and clear each sender's pending action only after a revision receipt. The
standalone transport Qt test also verifies receipt ownership and deferred-drain
dispatch. Candidate builds now default the consumer to action-bundle mode and
omit the legacy receiver that cannot link to candidate `MarkoffDocument`; direct
translation-unit compiles pass for both candidate and legacy definitions.
Durable transport state and production network transport remain open.

**Durable transport outbox follow-up (2026-10-14):** provider recovery now
derives pending outbound MABs from locally accepted transactions and a durable
receipt ledger. `MarkoffDocument` exposes exact action bytes for resend and a
host receipt acknowledgment API. Reference-provider MREF/5 writes persist
receipt action IDs and opaque peer revisions while retaining read compatibility
with MREF/4 state. `CollabConsumer` reloads pending actions when it starts and
records a receipt before signaling it to its owner. Durable integration covers
multiple pending actions, exact bytes after provider reopen, idempotent receipt
retry, changed-receipt conflict, and emptying the outbox after acknowledgment;
the action-consumer test also verifies startup resend through a peer and receipt
clearing. Candidate Release CTest passes 94/94, and the durable document plus
action-consumer tests pass under ASan/UBSan with leak detection disabled.
Production provider
implementations still need to pass the extended contract, and delivery remains
host/network-owned under A16.

### A12 — port positions and consumers within Markoff

Replace raw anchor fields with scoped opaque boundary/attachment tokens and full
block identity. Port caret/selection, scroll, folds, reference registries and
versioned JSON/binary storage. Define affinity and strict-detachment policies;
never fabricate end positions for unavailable history. Keep owner-thread engine
subscriptions, feedback suppression, session sharing and accessibility notifications.
Preserve the source/styled scope restrictions in the migrated API and update
their callers directly. Inventory any required leaf/host API migrations; do not
retain parallel compatibility surfaces for Markoff's unused experimental APIs.

**Exit:** original tokens resolve after concurrent edits/replay/reopen; deletion,
detachment and wrong-scope outcomes are correct. Actual active views and two-session
fixtures preserve selection, rendering and accessibility through remote drains.

**Progress (2026-10-13):** Markoff's public `AnchorJson`, `FoldRef`, `Session`
and `MarkoffDocument` position APIs now use `TextAnchor`; raw block-CRDT anchor
helpers have been removed from the document API. The canonical anchor JSON shape is version 1 and stores
the complete block identity; unsupported and unversioned shapes do not decode.
Canvas fold references now retain their block identity, so a newly attached view
can restore folded heads from the session. A same-counter/different-actor
regression verifies session folding keeps those block identities separate. The
focused anchor, fold, session, block-position and canvas tests pass 5/5. Scoped opaque CollabText boundary
tokens, failure-aware decoding, remote-position replay and accessibility behavior
remain open; this is API-surface migration evidence, not A12 completion.

**Progress (2026-10-14):** candidate `FindController` and syntax-highlighting
consumer tests now open provider-backed documents. `MarkoffDocument::inlineSpansFor`
uses the parser and caches by per-block accepted edit sequence; the candidate
document also exposes document-level and structural edit-sequence accessors.
Undo/redo increments affected block sequences so cached inline spans invalidate
after compensation. These targeted tests pass, but they do not close scoped
position tokens or the still-unimplemented Markoff position adapters.

**Progress (2026-10-14):** candidate `MarkoffDocument` now exposes
block-scoped opaque boundary token minting and resolution through
`ScopedTextStream`. The token regression confirms a boundary follows an edit
in its stream and fails with `wrong_stream` when presented to another block.
This provides a candidate position primitive for view migration; the existing
`TextAnchor`, session and active view adapters still use the old coordinate
surface, so A12 remains open.

**Progress (2026-10-14):** candidate `MarkoffDocument` now reconstructs list
and blockquote display markers from the replicated kind/attribute projection.
The provider-backed MarkoffDocument test verifies `- ` and `> ` after Markdown
import. Candidate Styled builds; its `ldd -r` audit no longer reports the two
missing display-marker symbols, though binding, position, formatting and
command symbols remain unresolved.

**Progress (2026-10-14):** candidate Styled link hit-testing now uses the
provider-backed block lookup helper instead of removed document anchor APIs.
`SourceTextDocumentBinding` has a candidate implementation sufficient to build
the active Styled library: it synchronizes provider-backed flat text, publishes
single-block non-structural edits, and merges deleted cross-block selections
through a new mixed text/list/kind-map action. Candidate binding tests cover
local edits, remote reverse sync, a cross-block merge, and Enter splitting as
mixed-stream actions. The tests cover selection-collapse splits,
document-start insertion, caret output, and undo/redo after reopening the same
ephemeral provider; they pass, as does
the candidate link-interaction test. The candidate
`ldd -r` reports no unresolved symbols for `markoff_styled`. This is an initial
build bridge, not the A12 view migration: specialized list/tab semantics,
cross-block replacement, session selections and token-backed caret/selection
mapping are still incomplete. Merge and split use atomic mixed-stream actions.
`applyBlockMerge` path and grouped document compensation, with undo/redo verified
after reopening the same ephemeral provider. Durable fault injection remains
outstanding. These limitations prevent claiming active-view acceptance or
complete collaborative structural editing.

**Progress (2026-10-14):** candidate `SourceTextDocumentBinding` now captures
both view-selection endpoints as stream-scoped boundary tokens when Source and
Styled editors move their caret. After an accepted document edit refreshes the
Qt projection, it resolves both tokens and emits their rebased selection; a
deleted or detached endpoint is discarded instead of becoming a fabricated
block-end position. `tst_binding_vnext::remoteEditRestoresSelectionFromScopedTokens`
verifies a selection survives insertion before it and still selects the same
text at its shifted positions; a second regression confirms deleting the
selected block drops the detached selection without emitting a fabricated
caret. The rebuilt 12-test focused candidate set passes 12/12, and candidate
Styled `ldd -r` reports no undefined symbols. Remote-drain concurrency and
accessibility acceptance remain open under A12/A14.

**Progress (2026-10-14):** candidate `Selection` now carries versioned
`ScopedTextPosition` values for both endpoints. Session JSON encodes each
position with the complete actor/counter block scope and canonical base64url
boundary token; decoding rejects malformed identity/token encodings by leaving
the scoped position unavailable. `SourceTextDocumentBinding` publishes captured
view tokens into the session, restores a loaded session selection through
provider resolution, and declines detached or wrong-scope values. A binding
regression round-trips session JSON and attaches it to another view, restoring
the same text selection. The focused binding, session, selection, document,
fold-reference and Styled checks pass 15/15. This is not yet durable
process-reopen or accessibility acceptance; A12 remains open.

**Canvas cutover evidence (2026-10-14):** `View::pushSelectionToSession()` and
its session-driven caret/selection restoration now use scoped provider tokens in
candidate builds. The new provider-backed Canvas regression source exercises a
caret following an accepted edit, but building its executable fails at link
time: `markoff_canvas` still has unresolved calls to legacy `d2UndoLog`, D2
primitive, per-block `FormatOps`, anchor and buffer-proxy symbols. This confirms
the Canvas runtime path remains incomplete under A08/A12/A13; no link workaround
was applied and the test is not counted as passing evidence.

**Revalidation (2026-10-14):** the current candidate-enabled `build-dev` now
builds `markoff_canvas` and the previously failing
`tst_canvas_vnext_session_positions` target. Its four provider-backed cases pass,
covering a caret following an accepted edit, line-move payload-swap undo, smart
Markdown paste with tail restoration, and one-action table insertion undo.
Candidate `markoff_styled` also builds; binding round-trip, block-format and D2
integration tests pass 3/3. This supersedes the earlier link-failure snapshot
for the current source, but does not establish full app, process-reopen Canvas,
remote-presence or complete accessibility acceptance.

Candidate Canvas remote-presence paint and its test-facing projection now
resolve `Selection` scoped-token endpoints instead of `TextAnchor` engine
coordinates. Canvas fold references also store scoped tokens in candidate
sessions, and candidate `FoldRef` no longer exposes the legacy `TextAnchor`
field. The clean candidate build succeeds and the focused fold/session/binding
and provider-backed consumer set passes 15/15. Presence transport production
wiring, runtime Canvas validation, and the remaining legacy editing calls are
still open.

**Candidate remote-presence regression (2026-10-14):** the provider-backed
Canvas session-position test now uses two provider-backed documents seeded
from one initial MAB. It supplies a peer selection using scoped boundary
tokens, routes a peer-authored text MAB into the receiver, verifies both
endpoints rebase and remain visible to the Canvas presence projection, then
removes the block and verifies the detached presence is omitted. It also
captures the rendered Canvas view and confirms the remote selection tint adds
participant-colored pixels beyond the same participant's caret-only rendering.
The focused `tst_canvas_vnext_session_positions` CTest passes. This exercises
view-side presence resolution and rendering through a real remote action drain;
presence transport and cross-session fold/presence integration remain open.

The same two-provider fixture now also opens the accessible Canvas tree on the
receiver, applies a peer structural-insert MAB and verifies that the new
composite `BlockId` is distinct, the accessible child count increases, and one
`QAccessible::ObjectCreated` event targets the exact new child. Removing the
original block then removes the detached presence and contracts the accessible
tree. This directly covers the candidate provider path behind the earlier
legacy numeric-ID accessibility failure; the default legacy build still needs
the coordinated identity cutover and remains an A07 blocker.

**Durable Canvas process-reopen regression (2026-10-14):** added a two-process
test to `tst_canvas_vnext_session_positions`. The writer process creates a
restartable provider-backed document, imports and edits a two-block document,
sets a Canvas caret, serializes the scoped Session JSON, and exits. A separate
reader process creates a fresh provider, replays the accepted document, loads
the saved session, attaches it to a Canvas view, and verifies the exact block
identities, edited text, resolved boundary-token offset and rendered view caret.
The focused CTest passes in Debug, Release and ASan/UBSan builds. This closes
document/session process-reopen acceptance for the Canvas caret; presence
transport and cross-session fold convergence remain open.

**Progress (2026-10-14):** Canvas selection collapse now publishes a same-block
deletion through `applyBlockEdit()` or a multi-block collapse through the
atomic `applyBlockMerge()` candidate API, preserving one action and one undo
unit. The candidate Canvas library rebuilds with the change. The broader
`build-dev` all-target build is not clean in candidate mode: legacy Source
frontend tests still construct `MarkoffDocument` from a numeric replica ID and
fail to compile. The deliberately narrow clean candidate build remains the
build evidence; Canvas runtime acceptance and the rest of its legacy mutation
paths remain open.

The candidate build and target inventory are recorded below; the frontend test
directories are deliberately disabled in this build because most still use
the removed replica-ID constructor. Their migration and active-view acceptance
remain open. An earlier candidate-wide `ctest` ran before all targets existed
and reported `Not Run`; it is not acceptance evidence.

**Progress (2026-10-14):** candidate `MarkoffDocument::applyBlockEdits()` now
stages validated, non-overlapping edits across one or more block text streams
into one accepted MAB action and records each stream's local history snapshot.
A regression reopens the provider and verifies one document undo and redo
restore the complete two-block edit. This supplies an atomic text-batch
primitive for formatting and other multi-range consumers; map/list grouping and
remote compensation acceptance remain open.

**Progress (2026-10-14):** candidate Markdown `wrapToggle`, `insertLink` and
`setHeadingLevel` now resolve flat-view selections to block-local edits and
publish through `applyBlockEdits()`. This keeps two-marker formatting and
multi-block wraps in one accepted text action. Candidate format tests cover
wrap/unwrap with undo, a multi-block selection with undo, link insertion, and
heading changes; `tst_format_ops_vnext` passes. The candidate Styled library
rebuilds, and `ldd -r` no longer lists any FormatOps symbols; position, binding
and command integration still remain open.

**Progress (2026-10-14):** candidate `Cmd::changeKind` now stages the kind map
and optional block-attribute map writes into one accepted action. Block-local
undo/redo handles all map groups for that block as one unit. The provider-backed
regression closes and reopens the provider before checking the kind and level
attribute through undo and redo. Candidate Styled `ldd -r` no longer reports
`Cmd::changeKind`; binding and position symbols remain unresolved.

**Progress (2026-10-14):** the candidate build now completes for Core, Source,
Styled, Canvas and the provider-backed document test using the relocated
CollabText alpha.5 prefix. Candidate D2 CMake selects migrated provider-backed
tests; legacy D2/D5 suites and the old frontend test directories stay in the
legacy configuration. Candidate mode also omits legacy application entry
points that still construct documents through the removed replica-ID API.
The focused 12-test run passes 12/12, covering binding, formatting, structural
edits, undo, save history, Styled link interaction and provider recovery. The
global CTest manifest lists 130 tests: 64 pass and 66 disabled frontend
executables are `Not Run`; that invocation is not a full-suite pass. Canvas
links with unresolved legacy consumer symbols, frontend tests and app entry
points are not migrated, and the active view acceptance required by A12/A14
remains open.

**Candidate fold/session synchronization follow-up (2026-10-14):** Canvas now
subscribes to `Session::foldedRegionsChanged` and rebuilds its folded-head set
from the attached session, validating scoped boundary tokens against the active
document. Reattaching the same document reprojects the existing session even
when the session pointer did not change. A candidate regression verifies two
views sharing a session update together, JSON-restored fold state appears in a
new view, detach/reattach restores it, and an external session clear removes
stale view state. It also edits the heading and verifies the fold remains
projected. The focused CTest passes in Release, Debug and ASan/UBSan; the full
candidate Debug suite passes 94/94. Rebuilt and installed current-source
Release Core, Source, Styled and Canvas; the installed durable Core and
three-editor consumers pass 2/2, and the Canvas library resolves to
`506dd94cdeaca3fab9c8316dd7a10bb6b084f38d80b6cf3dae5c696072b90fa7`.
The updated Markoff input snapshot is
`6b992441db880280b8939275b496f60dc215e10d5815a0c3573c293eb64f8ce8` (1,707
files, 83,407,835 bytes, excluding `docs/`); the installed Markoff prefix is
`ebf4e786afde89e1b247661e8d2d86d6d058f50392c2ec00920c38ea0e7da573` (221
files, 20,772,492 bytes), and the alpha.5 prefix remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.

### A13 — remove obsolete save/GC paths and complete build surfaces

Remove save-triggered compaction, local watermarks treated as global durability,
undo clearing, and premature disposal of removed streams. Retain history and
disable physical collection, pruning and epoch transitions. Remove obsolete
engine/packed-op/raw-anchor paths as part of the cutover; do not retain dual APIs,
format readers or compatibility wrappers for unused experimental forms. Complete
root and parent-subdirectory builds, dependency discovery, header exports and
ordinary test integration with installed Core.

**Progress (2026-10-13):** removed the deprecated `toMarkdown()` and
`toMarkdownUtf8()` APIs and implementations, which read a disconnected flat
buffer. `serializeForSave()` is now the only document export path; all code
callers already used it. Replaced tests of stale flat-buffer output with a
canonical serializer assertion and removed the obsolete flat-buffer undo test.
Also removed the unused flat-buffer `visibleLength()`, raw version, undo/redo,
undo-depth/coalescing and unbundled remote-op APIs, along with tests that covered
only those surfaces. Removed the unused flat-buffer `editSequence()` accessor;
sequence checks now use `d2EditSequence()` and D2 undo/redo. The D2 API is now
the sole public edit, history and remote application path. Removed the
unqualified flat-buffer anchor API and its fallback branches; text anchors are
now block-qualified throughout. Removed the unused `WatermarkCoordinator`,
save-triggered and explicit flat-buffer compaction/GC APIs, and the local
watermark/ack protocol that treated a save as permission to clear history.
Replaced its GC tests with regressions proving saved text and removed blocks
remain undoable. Root-wide save/GC removal remains open pending the broader
build-surface and persistence review.

**Progress (2026-10-13):** removed the remaining `CausalLwwMap::compact()` and
`UndoLog::compact()` APIs and their tests. Neither had production callers; both
could prune local undo history outside a durable retirement contract. The root
core rebuilt, and `tst_causal_lww_map` plus `tst_undo_log` pass. The candidate
provider's explicit history-retirement association remains the only planned
retirement mechanism; production routing is still open under A07/A08/A10/A11.

**Progress (2026-10-13):** removed parser `Document::frontmatter()` and
`parsedFrontmatterLegacy()`, the deprecated raw-text alias and `QVariant`
conversion path. `frontmatterRaw()` and `parsedFrontmatter()` are now the sole
raw and structured frontmatter accessors. No production call site used the
removed methods; the parser frontmatter test passes 24/24, and a source search
finds no remaining declarations or references. This closes that obsolete parser
surface only; A13's production engine and build-surface removals remain open.

**Progress (2026-10-13):** removed the duplicate
`Markoff::Live::Detail::Coordinates` include/namespace alias. Live consumers and
coordinate tests now use `Markoff::TextUnits` directly, the owning Core API; the
deleted header was also removed from the QML module export list. A live-enabled
build compiled Core and Live, and `tst_live_render_coords` plus
`tst_live_render_paragraph_edit` pass (2/2). No in-tree call site references the
removed header or namespace. `delegateGoingAway` now requires the dying
delegate root; QML delegates already supplied it, and the two tests now do as
well. This removes the unconditional one-argument behavior that could erase a
replacement delegate registration. The live-enabled build succeeds and
`tst_live_cursor_state_chokepoint` passes.

**Progress (2026-10-13):** removed `BlockAnchor` and the duplicate
`Markoff::Live` cursor aliases. All active core/view signatures now name the
single `Markoff::BlockId` and cursor types directly; metatype declarations live
with `BlockId`. The root core/canvas/source/styled targets and a separately
configured optional live target build. Focused root tests pass 6/6 and live
tests pass 3/3. This removes duplicate API names only; numeric allocation and
the production engine still block the structural identity port under A07/A08.

**Progress (2026-10-13):** removed the unpopulated parsed-block identity/range
cache, `BlockAnchorComputation`, and the three `MarkoffDocument` lookup APIs
that depended on that cache. The unused helper minted apparent block IDs from
text-anchor character values, so deleting it also removes that false identity
route. Active consumers resolve a block-qualified `TextAnchor` through its
actual `BlockId` and per-block buffer. Core, Canvas, Source and Styled rebuild;
focused core/canvas/styled tests pass 6/6. The block-local anchor regression
still passes after removing its assertion against the dead range API.

**Progress (2026-10-13):** removed the unreachable standalone
`TopLevelBlock::Kind::BlockQuote` representation and its parser classification,
Core conversion branches and clipboard HTML fallback. The parser emits quoted
content as its actual paragraph/heading/code kind with quote depth and run
identity; that is now the only parser contract. The top-level-block parser suite
passes 28/28 and clipboard codec suite passes 20/20.

**Progress (2026-10-13):** removed the incomplete cut-to-paste identity reuse
path. It emitted `sourceReplicaId`/`cutSequenceNumber`, retained a bounded
recent-cut cache, and exposed `PasteMeta`, but structured paste only consumed
the cache; the block identity remap was still a TODO. Cut and paste now use one
structured clipboard contract (`version` plus `blocks`), and paste has no
identity-reuse option. Removed the unused cache, metadata type, and tests that
asserted cache consumption without reuse. Core structured-paste tests pass
2/2; the five Live clipboard and paste-kind suites pass.

**Root-default build-surface check (2026-10-14):** configured a fresh Release
root tree with `MARKOFF_USE_COLLABTEXT_VNEXT=OFF`, apps, Live and tests enabled.
The complete root build succeeds, including the legacy engine, all three
editor libraries, Live, their apps and the collaboration test app. The full
offscreen root CTest run passed 323/324 in 68.44 seconds. The one failure is
`tst_d5_testapp_convergence`: its legacy `CollabConsumer` serializes raw
numeric block identities, while the new canonical MKOF/2 writer deliberately
rejects them; both convergence cases then receive an empty bundle. Do not relax
the structural-identity codec or add a second wire representation to restore
this retired path. The failure identifies the remaining coordinated
CollabConsumer/test-app cutover required before A13 closes. During the build,
the removed `Markoff::Live` cursor aliases also exposed stale test references;
`tst_live_render_cursor` now names the canonical `Markoff::NoCursor`,
`Markoff::TextCaret` and `Markoff::BlockSelected` types directly, and the full
root build then succeeded.

**Root-default and action-consumer refresh (2026-10-14):** the numeric-ID
legacy convergence fixture could no longer exercise the active wire contract:
MKOF/2 correctly rejects its raw buffer identity. Replaced that root-default
fixture with `tst_d5_legacy_identity_rejection`, which asserts the strict
rejection, and expanded the candidate `CollabConsumer` test to verify peer
initial import, text edits initiated by each replica, structural insertion
with exact identity convergence, replacement, receipts and transport draining.
The regenerated root-default Release suite passes 324/324 in 137.12 seconds.
The complete root-default Release build also succeeds with the regenerated
test target.
Candidate Release and Debug suites pass 92/92 each; the changed action-consumer
test also passes under ASan/UBSan. This closes the root-default suite failure
without restoring the retired numeric wire path. Production app cutover and the
broader obsolete-path audit remain open.

**Candidate+Live build-surface audit (2026-10-14):** the optional
`build-candidate-live` tree confirms `markoff_live` itself compiles against the
provider-backed Core, and the registered vNext action-consumer and in-memory
transport targets compile. A full build fails in the retired Live test suite:
consumers still link removed `d2UndoLog`/`d2ApplyBufferEdit`/`d2SetBlockKind`
symbols, and `tst_live_find_adapter` still constructs the deleted numeric
replica-ID `MarkoffDocument`. Building `markoff-collab-testapp` directly also
fails because `MainWindow` constructs `MarkoffDocument(quint16)`. This is not
the normal standalone configuration (`MARKOFF_BUILD_LIVE=OFF`), and those
legacy QML consumers are not provider-backed acceptance. The direct test-app
build failure was resolved in the follow-up below; the retired Live test suite
remains incompatible and is excluded from the standalone candidate build.

**Exit:** save does not erase undo/reference recovery; no active private Core source
includes or accidental source-tree linkage remain. Normal Markoff and a minimal
parent host build use the intended targets. One production API and one canonical
format reader/writer remain for each record family; unused experimental APIs and
formats are removed.

**Obsolete API audit (2026-10-14):** candidate Core compiles the vNext document
implementation while marking the legacy `MarkoffDocument.cpp` and related D2
translation units header-only. The public `MarkoffDocument.h` still declares
legacy-only methods such as `d2UndoLog()`, `d2ApplyBufferEdit()`, proxy accessors,
and `loadFromMarkdown()` outside candidate guards, despite those implementations
being absent from the candidate library. Existing installed-consumer tests do
not reference these symbols, so they do not prove the candidate public API is
complete. This is a concrete A13 cleanup item; preserve the legacy declarations
for pinned-engine builds while removing or conditionally exposing them in the
candidate interface, then verify with a linked header-surface consumer.

**Candidate header separation follow-up (2026-10-14):** candidate builds now
omit the legacy save-watermark/test insertion methods, transaction-based D2
helpers, CRDT proxy accessors, and legacy `loadFromMarkdown()`/load-time-byte
methods and their `UndoLog`/`CrdtProxies` includes from the public header.
Pinned-engine declarations remain unchanged.
The full standalone candidate Release build and CTest pass (94/94); the pinned
Core, Live, Source, Styled and Canvas libraries rebuilt, and three focused
legacy D2/Live tests pass with `QT_QPA_PLATFORM=offscreen`. The docs-excluded
worktree fingerprint is
`62393561e8e9e66c0a117bdcd9691a65d5d5d8146cc3e55f0ff60ec5248f9f81`
(1,707 files, 83,459,037 bytes); installed Markoff and alpha.5 prefix hashes
remain unchanged. The full pinned test-target build was not completed; the
remaining retired-Live and source-surface audit is still open.

**Pinned-only API separation follow-up (2026-10-14):** the candidate header now
also omits the numeric replica identity, legacy remote-op ingress, `TextAnchor`
methods, reset/flat-edit/newline ingress, load-touch query, remote-cursor API,
legacy-only signals and private D2 reconstruction helpers. Canvas no longer
connects to the pinned-only `documentReloaded` signal in candidate builds; the
candidate replacement path already emits `d2DocumentChanged`. Full candidate
Release build and CTest pass 94/94. The pinned `markoff_core` target rebuilds
with the legacy declarations and implementations intact. The docs-excluded
worktree fingerprint is
`2e76a70aca7bdb0b8c5ea060c3868f027c940c0b27079b814375ff3f7840d9bb`
(1,707 files, 83,459,653 bytes); installed Markoff and alpha.5 prefix hashes
remain unchanged.

### A14 — actual Markoff acceptance

Run the actual document/routing/view/serializer tests against a relocated exact
candidate. Retain meaningful existing D2/D5 regressions, including
`setext_untouched_roundtrip_is_byte_identical`, `tst_block_buffer_invariant`,
`tst_d2_reset_content`, two-document convergence and multi-block payload routing.
Add provider/action fault evidence and real process reopen where needed. Complete
relevant Debug, Release, sanitizer and installed-consumer checks; use root test
guidance, offscreen execution and at most four build jobs. Do not claim a full
suite from a filtered run or accept empty selections.

Measure typing, paste, structural edits, token resolution, reopen and retained
memory at realistic document/history sizes. Record hardware/configuration and
explain limits without weakening semantic assertions. A reference provider can
establish Markoff recovery behavior; Corbomite's production storage still needs
its own later acceptance.

**Exit:** all Markoff-owned required scenarios have current evidence, no unexplained
regression or failing required check remains, and results record actual Markoff
revision/source digest, dependency SHA, package hashes, components and commands.

**Candidate full-suite follow-up (2026-10-14):** after adding explicit retry
for persisted remote waits, the complete candidate-enabled `build-dev` tree
rebuilt successfully with `cmake --build build-dev -j2`. The configured offscreen
CTest run passed 90/90 with `ctest --test-dir build-dev --output-on-failure`
(20.45 seconds), including the candidate peer-MAB presence rendering and
rebase/detachment case.
`build-dev` has an empty `CMAKE_BUILD_TYPE`, so this is a default-config
candidate suite result, not Debug. It includes the provider-backed document
test; it does not close A14's Release,
sanitizer, installed full-host consumer, realistic performance, or reproducible
source/dependency/package digest requirements.

**Candidate app-enabled suite refresh (2026-10-14):** enabled the three
library-local Markoff demo apps in `build-dev` alongside the vNext Core and
tests. `cmake --build build-dev -j2` built Styled, Source and Canvas apps, and
`ctest --test-dir build-dev --output-on-failure` passed 91/91, including the
new offscreen Canvas app startup/import smoke test. The collaboration test app
remains omitted because it depends on retired `markoff_live`; this is not full
Corbomite or transport acceptance.

**Release candidate acceptance refresh (2026-10-14):** configured a clean
`/tmp/markoff-vnext-release` tree with `CMAKE_BUILD_TYPE=Release`, vNext enabled,
all three Markoff demo apps enabled, tests enabled and the installed CollabText
alpha.5 prefix selected. `cmake --build /tmp/markoff-vnext-release -j2`
completed, and the full configured offscreen CTest run passed 91/91 in 9.01
seconds. This now verifies Release Core, view libraries, apps and the candidate
provider consumer suite. Sanitizers, the retired-live collaboration test app,
realistic performance budgets and reproducible dependency/package digests
remain open.

**Release performance probes (2026-10-14):** expanded the manual vNext
`perf_load_bench` diagnostic to report import, per-block inline-span resolution,
text edit, structured paste, structural insert, provider reopen and retained
RSS. On a generated 1,000-block, 86,170-byte Markdown document with 15,000
inline spans, the corrected Release run completed: import 3.09 s, span
resolution 190 ms, text edit 2.22 s, structured paste 2.26 s, structural insert
2.34 s, and provider reopen 2.41 s. RSS was 77,288 KB after import (40,508 KB
above process baseline) and 76,384 KB after reopen; the reopened document had
1,003 blocks. The test machine reported Manjaro Linux 6.18.49, x86_64, with an
11th Gen Intel Core i7-1185G7 and 8 logical CPUs. The collision came from
indexing binary identity keys in `std::map<QString, ...>`; changing the key to
`QByteArray` keeps the exact bytes. A provider-backed regression now imports
1,000 blocks, edits, pastes two blocks and verifies provider reopen. The full
Release CTest suite passes 91/91 in 18.08 seconds; the focused Debug provider
test passes in 32.33 seconds. These runs use the reference ephemeral provider
and synthetic input, so they do not establish production-provider responsiveness
or a realistic workload budget. These timings are diagnostic, not acceptance
thresholds.

Ran the same Release diagnostic against the repository's real 161,364-byte
collaboration-adoption execution plan (SHA-256
`115eca337351fa2c33078e1d093dfe4cbe1d55ee8e7cf972835f47479c6849c6`). It
contains 250 imported blocks and 14,348 inline spans. Provider open took less
than 1 ms at the displayed precision; initial import took 698 ms, inline-span
resolution 183 ms, text edit 226 ms, structured paste 207 ms, structural insert
202 ms, and provider reopen 379 ms. RSS was 70,168 KB after import and 70,748 KB
after reopen; reopen produced 253 blocks. The benchmark executable's SHA-256
was `ae23bcd905d5a264171b38532e81309f1294581853688eaf0ce571d9dfeca105`.
It ran on Manjaro Linux 6.18.49, x86_64, with an 11th Gen Intel Core i7-1185G7
and 8 logical CPUs. This adds a repository-authored workload alongside the
synthetic case, but one project plan is not a representative user-document
corpus and does not establish responsiveness budgets.

**Typing-corpus scaling probe (2026-10-14):** ran the same Release diagnostic
against 100, 250, 500 and 750 paragraph prefixes of
`libs/markoff-live/tests/fixtures/inline-formats/typing-corpus-1k.md` (the full
fixture is 60,836 bytes and 1,000 blocks). Import/action/reopen timings were
42/23/27 ms at 100 blocks, 161/104/110 ms at 250, 542/411/385 ms at 500, and
1,240/934/921 ms at 750. Here “action” is each of text edit, structured paste,
and structural insert, which were within a few milliseconds of one another.
The already recorded full 1,000-block run measured import 2,488 ms, action
1,713–1,836 ms and reopen 1,600 ms. Span resolution ranged from 4 ms at 100
blocks to 108 ms at 1,000; RSS after import ranged from 43,436 KB to 73,880 KB.
This is a useful local Markdown typing corpus and shows strongly superlinear
action/import costs as block count grows. It is still one fixture on one
machine, so it is diagnostic rather than a user latency budget. Candidate
publication currently replays the accepted provider prefix after each action;
that replay is a concrete profiling target before setting a performance gate.

**Accepted-prefix replay isolation (2026-10-14):** extended the manual
`perf_load_bench` probe to time provider recovery-snapshot reads separately from
`AcceptedPrefixProjection::replay()`. On another full 1,000-block Release run,
the snapshot read rounded to 0 ms while a direct replay took 1,470 ms. The same
run measured local edit/paste/structural publication at 1,547/1,552/1,612 ms
and reopen at 1,562 ms. This confirms accepted-prefix replay accounts for most
of the per-action and reopen latency on this fixture. The probe still uses the
reference ephemeral provider; it does not establish production-provider costs.
This isolated cost motivated the guarded staged-local fast path recorded below;
remote and ambiguous outcomes still require full recovery and replay.
A 250-block repeat measured 98 ms for direct replay against 105/111/116 ms
for edit/paste/structural publication and 121 ms for reopen, matching the same
pattern at a smaller scale.

**Staged local publication replay optimization (2026-10-14):** local
`publishStagedAction()` now advances accepted-prefix metadata in place only
when the provider snapshot proves that the exact prepared MAB and exact private
history bytes were committed as the sole next record after the lifecycle's
prior accepted prefix. The already staged stream operations remain in place;
an initial import also completes each staged text stream's import barrier.
Direct publication, remote acceptance, history-pending commits, unresolved
reservations/transactions, and any prefix or byte mismatch retain full replay.
The Release typing-corpus probe now measures edit/paste/structural publication
at 50/53/50 ms for 1,000 blocks, compared with 1,547/1,552/1,612 ms before the
fast path; a 250-block repeat measured 10/10/9 ms, compared with 105/111/116
ms. Full accepted-prefix replay and reopen remain about 1.68/1.67 s at 1,000
blocks and 105/121 ms at 250 blocks, as they still reconstruct the complete
history. These remain single-machine diagnostic timings, not performance
budgets. The complete app-enabled Release candidate suite passes 92/92 after
the change, including provider recovery, history, undo and remote-action tests.
The complete app-enabled Debug suite also passes 92/92, and the configured
ASan/UBSan suite passes 91/91 with leak detection disabled as in the prior
candidate sanitizer run.

**Candidate action-consumer suite refresh (2026-10-14):** the two-way action
delivery/receipt consumer test had been nested under the legacy collaboration
app, which is omitted when `MARKOFF_BUILD_LIVE=OFF`. Registered the existing
test in the installed-package candidate suite instead. It passes independently
in Debug and Release. The earlier `build-dev` result of 92/92 in 42.17 seconds
was also default-config (`CMAKE_BUILD_TYPE` empty), not Debug. The complete
app-enabled candidate CTest suites pass 92/92 in Release (16.81 s) and in a
dedicated `CMAKE_BUILD_TYPE=Debug` tree (39.56 s). The Debug build includes a
new Canvas test that writes durable provider state and serialized caret-session
JSON in one process, then reopens both from a separate reader process. The test
covers peer initial import, source replacement, delivery in both directions,
accepted revision receipts and transport queue draining through
`CollabConsumer`. This closes the missing candidate test registration and adds
process-boundary durability evidence for the Canvas document/session path; it
does not add durable transport outbox state or production network transport.

**Candidate sanitizer acceptance refresh (2026-10-14):** configured a
RelWithDebInfo vNext candidate build with AddressSanitizer and
UndefinedBehaviorSanitizer, then built all configured targets. The complete
offscreen CTest suite passed 91/91 in 132.56 seconds with
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1`; leak detection
remains disabled for the runner's ptrace limitation. The first run found a
test-only heap-use-after-free in durable-provider replay: the test retained a
reference into the temporary vector returned by `visibleEntries()`. Keeping
the vector alive and copying its block identity fixed the lifetime error; the
provider replay test and the full suite then passed under sanitizers. The
separate 1,000-block provider-backed import/edit/paste/reopen test passed in
108.64 seconds. This closes the candidate sanitizer check, but does not establish
the installed full-host consumer, production-provider behavior, realistic
performance budgets, or reproducible source/dependency/package digests.

**Installed Markoff view-consumer acceptance (2026-10-14):** extended the
parent-shaped install harness to build and export Core, Source, Styled and
Canvas together. Added missing installed include interfaces and public-header
install rules for the three view libraries, and exported JKQT MathText as the
Canvas shared library's link-only dependency. A separate installed consumer
now opens and imports a vNext document and constructs, shows and event-processes
all three editors using only the staged prefix's exported targets and headers.
The consumer build succeeds, both installed consumer CTests pass (2/2), and
`ldd -r` on the view consumer reports all Markoff libraries resolved with no
undefined symbols. This covers the installed Markoff editor-library surface;
it does not build the Corbomite application graph or validate production host
storage and package configuration.

**Candidate source and package snapshot (2026-10-14):** added
`scripts/hash-acceptance-snapshot.py` to compute length-framed SHA-256 digests
for the dirty non-ignored worktree and installed prefixes. With the command
below, the Markoff build-input tree digest is
`4cc3b52e14eb26ff4004aba4f814204c77224c02414c40439c9b00149cb34303`
(1,707 files, 83,386,962 bytes); it excludes the narrative `docs/` tree and
ignored build outputs. The Markoff Git base is `28e9a734bf5d` and the legacy
CollabText submodule pin is `8b9e780bc932` (the candidate build consumes the
installed alpha.5 package instead). The complete staged Markoff install prefix
hash is `9e78915825bf32e7bf672df9f2282deda7c55a7d8671cba75f665b35618b0653`
(221 files, 20,770,774 bytes); the selected CollabText 0.2.0-alpha.5 install
prefix hash is `3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`
(28 files, 4,821,504 bytes). The Markoff prefix's individual Core, Source,
Styled and Canvas shared-library SHA-256 values are, respectively,
`08029648603c64922d268986900ddea7149e967b74008ac74f6cecf71fcf0df4`,
`41e0b2e237c6c6ebec268113f807f0bbabe558355cdcf6edbb6a2880540e7bfd`,
`05411cb7e0e774bdf5af954032cb5f6b523e73a01558924dec60b23315dc1d88`, and
`2860aa9d385166aae1a43999ada259424d2bd2712a6e11c62d5409615c18f7f9`.
The snapshot was built on Manjaro Linux 6.18.49, x86_64, with GCC 16.2.1,
CMake 4.4.3, Qt 6.11.2, and an Intel Core i7-1185G7 (8 logical CPUs). These
digests identify this exact dirty worktree and the local candidate prefixes;
they are not release attestations or a Corbomite package digest.

**Provenance refresh (2026-10-14):** after the production queue-backpressure
regression and candidate+Live CMake target-guard change, recomputing the command
below gives the current Markoff build-input digest
`4cc3b52e14eb26ff4004aba4f814204c77224c02414c40439c9b00149cb34303`
(1,707 files, 83,386,962 bytes). The alpha.5 source worktree still resolves to
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b` and has no modified or untracked
files. The staged Markoff prefix remains
`9e78915825bf32e7bf672df9f2282deda7c55a7d8671cba75f665b35618b0653`; the
installed alpha.5 prefix remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`. This
refresh records provenance only; A01–A14 implementation and acceptance gates
remain open.

**Performance-probe source refresh (2026-10-14):** added isolated recovery-read
and accepted-prefix replay timings to `perf_load_bench`, then recomputed the
same snapshot command. The build-input digest is now
`a50b94fd560add6199f878698e0fe982235936a5b38c4e1a4f7e85cc502da4c4`
(1,707 files, 83,388,071 bytes). Installed Markoff and alpha.5 prefix digests
are unchanged because this diagnostic edit is not part of either install.

**Staged replay fast-path source refresh (2026-10-14):** after adding the
guarded accepted-prefix metadata advance and initial-import stream completion,
the snapshot command reports Markoff build-input digest
`9dfbe946d1fbbe6c3eda7a2a20742b36bbaa837d06bc5171c452e04c03afd780`
(1,707 files, 83,393,717 bytes). Installed Markoff and alpha.5 package
digests remain unchanged; the source change has not been installed as a package.

**Collaboration-consumer test refresh (2026-10-14):** after replacing the
retired root-default numeric-ID convergence fixture and expanding candidate
action-consumer convergence coverage, the same snapshot command reports
`3aee89a9927f20eb66bde6546d630051da961fac6016cba68910e0633af20f23`
(1,707 files, 83,392,569 bytes). Installed Markoff and alpha.5 prefix digests
are unchanged; these test and CMake changes are not part of either install.

Recompute the recorded tree fingerprints from the repository root with:

```sh
python3 scripts/hash-acceptance-snapshot.py --worktree . --exclude docs/ \
  --tree markoff-install=build-host-harness/prefix \
  --tree collabtext-alpha5=/tmp/collabtext-alpha5-prefix-b
```

**Baseline evidence (2026-10-13):** `cmake --build build-dev -j2` succeeds for
the current root build. `scripts/run-tests.sh -E tst_benchmark` ran 213 tests:
212 passed, while `tst_canvas_perf_500` exceeded its time budget under the
script's eight-way load. The isolated rerun passed in 6.82 seconds; `tst_realistic`
passed in 187.69 seconds during the parallel run. The isolated command was:

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev --output-on-failure \
  -R '^tst_canvas_perf_500$'
```

This records the pre-cutover regression baseline and does not establish the
alpha.5 production path.

**Current worktree check (2026-10-13):** `cmake --build build-dev -j2` passed.
The 8-way `scripts/run-tests.sh -E tst_benchmark` run was interrupted while its
last long-running test was active; under load, `tst_canvas_perf_500` and
`tst_canvas_perf_formatted` exceeded their 16 ms p95 limits. Isolated reruns
passed for `tst_realistic` (213.22 s), `tst_canvas_perf_formatted`, and a second
`tst_canvas_perf_500` run; the first isolated 500-block rerun narrowly missed at
16.95 ms. Treat this as load-sensitive, incomplete regression evidence, not a
full-suite pass or a candidate-path acceptance result.

**Revalidation (2026-10-14):** the root test setup had registered
`markoff_provider_backed_document_test` whenever `Markoff::Core` existed, even
in the default legacy configuration where candidate vNext headers are not
available. Its CMake condition now also requires
`MARKOFF_USE_COLLABTEXT_VNEXT`; the default test registry drops that invalid
target, and the candidate build still compiles and passes both the provider-
backed document and storage-provider tests (2/2). The full default CTest run
with `QT_QPA_PLATFORM=offscreen` reached the known
`tst_canvas_accessibility::structure_remote_edit_insert_remove_create_destroy`
failure: the remote block insertion changes the accessible child count but
emits no `ObjectCreated` event. This is the recorded remote insertion
regression; no full-suite pass is claimed.

**Progress (2026-10-14):** the remote insertion failure traced to the legacy
document-local allocator starting every replica at the same block value. The
shared block value now uses the existing replica ID in its high 16 bits and a
48-bit local counter, so blocks created by distinct replicas remain distinct.
The remote accessibility regression asserts the inserted block identity differs
from both existing blocks, then verifies `ObjectCreated`. Focused offscreen
CTest for `tst_canvas_accessibility`, `tst_d5_two_doc_convergence`, and
`tst_d5_three_doc_convergence` passes 3/3. Full-suite acceptance remains open.

**Revalidation (2026-10-14):** the default suite excluding the long benchmark
and realistic-history test ran 214 tests; its three failures were the numeric
identity serializer regression and two Canvas p95 budgets under four-way load.
After rejecting numeric serialization, the functional suite excluding both
Canvas perf tests and the two long benchmark tests passes 212/212. The two
Canvas perf tests also pass 2/2 in an isolated run. The current
`QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R '^tst_realistic$'`
run passes in 162.92 seconds. Together these runs cover 215 of the 216 default
tests; only the separately handled `tst_benchmark` remains excluded here. The
complete candidate-backed Markoff acceptance path remains open.

**Clean candidate build audit (2026-10-14):** configured a fresh
`/tmp/markoff-candidate-clean` tree against `/tmp/collabtext-alpha5-prefix-b`
with vNext enabled, apps/live disabled and tests enabled. The initial full build
failures in `tst_foundation_session` (removed `crdt/Anchor.h`) and
`tst_fold_ref` (`BlockId::fromRaw`) have since been migrated: FoldRef now creates
candidate actor/counter identities, and the session fixture opens a
provider-backed document and uses an engine-independent bias type. Candidate
document, SearchEngine and Cmd edit tests now cover provider-backed reads,
search, sessions and accepted edit/undo/redo. Candidate `replaceMatches()` now
stages all current non-overlapping hits, including hits across streams, as one
MAB action. Its durable regression closes and reopens the provider, then verifies
that one undo restores both blocks and redo reapplies both. The combined
parser/link, migrated Core and vNext/provider run passes 23/23. A new full build
advances past these suites and stops at more legacy Core consumers, currently
`tst_foundation_find_controller` and `tst_foundation_syntax_highlight_service`,
which still use the deleted numeric-replica constructor. A full CTest attempt
against reused `build-dev` binaries also produced stale-ABI missing-symbol
failures and stack corruption, so it is not candidate acceptance evidence.
Candidate consumer migration and the full Markoff acceptance surface remain
open under A07/A12/A14. Candidate `inlineSpansFor` now caches results by block
edit sequence, and successful edit, replacement, undo and redo actions advance
the affected counters. `d2EditSequence()` and `structuralEditSequence()` are
implemented for the candidate. FindController, syntax highlighting, table
loading, targeted structural signals, revision-bound export, and view/document
signal consumer tests have been migrated and pass. The combined clean
parser/link, migrated Core, and vNext/provider suite now passes 32/32.

The structured-paste target now links and verifies that the candidate path
rejects the operation without mutating document content or block order. This
was an explicit safety boundary; atomic structured paste and its undo history
were then implemented for documents with an existing visible block. Candidate
paste now replaces a same- or cross-block range in one action, retains the
untouched end-block tail, and supports undo/redo. The provider-backed Source
binding routes multiline paste, including selected text, through this action
and restores the resulting caret. Release focused structured-paste and binding
tests pass 2/2, full candidate Release CTest passes 94/94, and focused
ASan/UBSan tests pass. A document with no visible block still has no atomic
structured-paste target and remains an open edge case. The latest full build also found numeric-replica callers in
the save-watermark, targeted block-signal, and table-loading suites; these have
been migrated to revision-bound export, provider-backed structural operations,
and accepted Markdown import respectively. Those targets now pass.
SourceTextDocumentBinding now has provider-backed local edit, reverse-sync,
cross-block deletion, Enter splitting and separator-boundary key tests. Merge
and split actions undo/redo after reopening the same ephemeral provider. The
candidate canvas target compiles against a candidate StructuralKeyHandler,
but an `ldd -r` audit still reports unresolved legacy Cmd, FormatOps, TextAnchor
and D2 helper symbols in `markoff_canvas`; this is a concrete A12 consumer
migration gap. Specialized list/tab behavior and session selection mapping also
remain incomplete.
Candidate fixtures have also replaced legacy Core tests for opaque table
frames, MarkdownView undo routing, blockquote buffer behavior, save-history
preservation, primitive block edits, and structural insert/remove/change-kind
plus per-block undo/redo. `applyBlockEdit()` now rejects invisible block
identities instead of creating orphan text streams. The six directly affected candidate
tests pass 6/6; the adjacent migrated `BlockId`, block-types and undo-log tests
also pass, and the candidate structural and undo suites pass 2/2. A fresh
all-target build now advances into the remaining D2 suite; broader candidate
build and runtime audits are still open.
The old forward/reverse binding tests are replaced in candidate mode by a
provider-backed suite; other legacy suites may still block a full candidate
build. This remains an open consumer gap, and the full candidate build and
A07/A12/A14 acceptance remain open.

### A15 — freeze and assemble transition readiness

Select the verified candidate and reproducible Markoff revision/build contract.
Record dependency provenance/fetch availability, stable-enough host API signatures,
format/readers/limits and source/API migration inventory. Supply the reference
provider/conformance harness, minimal host example and fault scripts so Corbomite
can implement and test its storage without reverse-engineering Markoff internals.
Include fallible construction, durable edit outcomes, opaque BlockId/positions,
replacement actions, revision-aware exports and initial-import lifecycle.

Audit every prerequisite and acceptance item against actual evidence. Separate
remaining Corbomite service/lease/journal/application work and Wave/global release
gates from completed Markoff work. Document known limitations and supported
ephemeral/durable modes. No unimplemented Markoff requirement may be relabeled
as host work to pass readiness.

**Exit:** a fresh Corbomite agent can build the selected Markoff dependency, implement
the provider, adapt all named mutation/save/rename paths and run the supplied
conformance checks using a self-contained transition package.

### A16 — final signal to Corbomite

After A01–A15 close with evidence, create a dated
`docs/handoff/YYYY-MM-DD-to-corbomite-ready-for-collabtext-transition.md` and
issue the readiness signal through the established handoff channel, including a
direct link in the user-facing completion message. Record the actual artifact
and delivery method; creating a draft alone is not a delivered signal.

State that Corbomite may now begin its transition. Include exact Markoff/dependency
revisions, build commands, final provider/format APIs, migration list, acceptance
results, test harness and the host tasks/production gates that follow. Update
STATUS, CLAUDE and this board with the signal record. Until this task, keep the
pause explicit and send no earlier readiness message.

**Exit:** the completed readiness handoff and signal are recorded. This initiates
Corbomite development; production enablement and global merge/publication require
their separately tracked acceptance and authorization.

## Evidence and maintenance rules

For each completed task record implementation paths, tested revision or source
digest, exact candidate/package, commands, nonempty results and remaining limitations.
A code stub or compiled header is partial. Restore/replay tests must compare
identities, groups, scopes and references as well as visible bytes.

Classify test failures against the intended contract before changing assertions.
Preserve existing user behavior and independent semantic models. A product
behavior change must be explicit, with its consequences and authorization recorded.

When a failure changes the next action, record it here and in the relevant spec.
If a genuine new Core capability is required, prepare a precise upstream request,
preserve the current candidate and reassess dependency/consumer evidence for its
replacement. Keep Corbomite paused through that work. Do not add readiness tasks
after A16; missing Markoff work must be resolved before the signal.


**Standalone default cutover verification (2026-10-14):** the root CMake
project now defaults `MARKOFF_USE_COLLABTEXT_VNEXT=ON`; embedded Markoff
subprojects default it to OFF. `MARKOFF_BUILD_COLLABTEXT_VNEXT_TESTS` follows
that selection. A fresh standalone Release tree configured without either
option, built all configured targets, and passed 91/91 offscreen CTest. A
fresh parent-project configure confirmed both options default OFF when Markoff
is embedded, preserving the Corbomite boundary before A16. The source snapshot
(excluding docs and the two temporary build directories) is
`c0324710c4bd79fc83241cae3c99e13e97baf9eb382b2face88d3b1dcd719ef3`
(1,707 files, 83,393,002 bytes); installed Markoff and alpha.5 prefix digests
remain unchanged.


**Repository-corpus responsiveness probe (2026-10-14):** ran the standalone
Release `perf_load_bench` built with default vNext options and the alpha.5
package on three repository Markdown inputs. The 60,836-byte, 1,000-block
typing corpus (`9d9c827a4eaaa9f7d7924f53819f86abea45a58764414556afafbb0f17e4a18d`)
measured 718 ms import, 112 ms inline-span resolution, 56/66/67 ms text
edit/paste/structural insert, 1,634 ms direct accepted-prefix replay, 1,682 ms
reopen, and 74,252 KB retained RSS. The 6,723-byte table dogfood fixture
(`4597fe9ff1e1f2e2647c7c49d239bdcfc16081014600a5904ddae4cfaf3ef92b`,
69 blocks) measured 28 ms import, 5 ms token resolution, 4/4/4 ms actions,
23 ms replay, 28 ms reopen and 44,528 KB retained RSS. The 187,514-byte
repository-authored Canvas production plan
(`8f43f49ccc76813bb284f681120531209508ad706b031ca09e3d993cde9835a9`,
480 blocks) measured 748 ms import, 176 ms token resolution, 35/40/38 ms
actions, 940 ms replay, 1,179 ms reopen and 94,848 KB retained RSS. All ran
on Manjaro Linux 6.18.49, x86_64, GCC 16.2.1, Release, 8 logical CPUs, with
the reference ephemeral provider; benchmark executable SHA-256 is
`0556154a1a323088c7ea658b7830b48cecf86128fae8804d1b616bdecf6dd695`.

For this local repository corpus, use provisional budgets of import <=1,000 ms,
inline-span resolution <=250 ms, each local edit/paste/structural action
<=100 ms, reopen <=2,000 ms, and retained RSS <=128 MiB on the recorded
hardware/configuration. These three single-run samples meet those budgets.
They provide reproducible regression targets, not production-provider guarantees
or broad user-document acceptance; repeat distributions, larger histories,
additional document shapes, and host-provider measurements remain open under
A14. The source fingerprint remains
`c0324710c4bd79fc83241cae3c99e13e97baf9eb382b2face88d3b1dcd719ef3`
(1,707 files, 83,393,002 bytes), excluding docs and the generated Release
build tree; installed Markoff and alpha.5 prefix digests are unchanged.

**Expanded corpus follow-up (2026-10-14):** rebuilt the Release probe from the
current source and ran five varied repository Markdown inputs with the
reference ephemeral provider. The 61,566-byte Canvas spike design (136 blocks;
input SHA-256 `06f0b6f98e7e2d159b6f716f8662ae7a055aa1afc8d2ecc4b528e2033f8ec1d1`)
measured 181 ms import, 55 ms token resolution, 7/7/7 ms edit/paste/structure,
71 ms replay, 146 ms reopen and 54,612 KB retained RSS. The 72,657-byte
foundation design (392 blocks; `654c8e8ae36392123466537fe85d954613094984b852a778b01b24af7d3635bc`)
measured 243 ms import, 49 ms token resolution, 21/23/21 ms actions, 455 ms
replay, 527 ms reopen and 65,392 KB retained RSS. The current 186,608-byte
execution plan (278 blocks; `a4ac53d3d8c1f567f02d6fb6eb61fea35c91bd5a83cd513cfe87bc4ea9e4ce02`)
measured 390 ms import, 140 ms token resolution, 13/13/13 ms actions, 156 ms
replay, 319 ms reopen and 81,240 KB retained RSS. The 6,723-byte table dogfood
fixture (69 blocks; `4597fe9ff1e1f2e2647c7c49d239bdcfc16081014600a5904ddae4cfaf3ef92b`)
measured 23 ms import, 5 ms token resolution, 3/4/3 ms actions, 18 ms replay,
25 ms reopen and 44,368 KB retained RSS. The 60,836-byte, 1,000-block typing
fixture (`9d9c827a4eaaa9f7d7924f53819f86abea45a58764414556afafbb0f17e4a18d`)
measured 714 ms import, 105 ms token resolution, 53/49/49 ms actions, 1,632 ms
replay, 1,704 ms reopen and 73,920 KB retained RSS. All five satisfy the
provisional repository-corpus budgets above. These are repository-authored
inputs, not a supplied user corpus or production-provider evidence.

The probe binary SHA-256 is
`fbb22136a07588e794cb4cf30011cb71120b06e4f1dcd2e90f3b87119677f84c`. It was
built in Release against the installed CollabText alpha.5 package on Manjaro
Linux 6.18.49, GCC 16.2.1, CMake 4.4.3, Qt 6.11.2, and an Intel Core i7-1185G7
with 8 logical CPUs. The current Markoff build-input snapshot is
`40699ca0e25461b8db8b70e89ae5cfc3ef3816bcbe9809ddb68d5b0d89f53962` (1,707
files, 83,401,481 bytes, excluding `docs/`); the staged Markoff install is
`8c565201fb7f98a1e66e4892ab826a84b743b020e2b4cc93a0485735f415b7c7` (221
files, 20,767,856 bytes), and alpha.5 is
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23` (28
files, 4,821,504 bytes). This extends repository-corpus coverage only; broader
user-document, repeated-distribution and durable-provider timing remain open.

**Repeat and history-depth follow-up (2026-10-14):** ran three serial Release
samples each for the 60,836-byte, 1,000-block typing corpus and the 72,657-byte,
392-block foundation design. For the typing corpus, medians (ranges) were:
import 701 ms (698–713), token resolution 100 ms (99–104), text edit 51 ms
(44–54), paste 50 ms (49–52), structural insert 50 ms (47–55), accepted-prefix
replay 1,668 ms (1,604–1,711), reopen 1,830 ms (1,766–1,867), and retained RSS
74,160 KB (73,908–74,184). For the foundation design, medians (ranges) were:
import 321 ms (242–324), token resolution 62 ms (47–67), text edit 27 ms
(23–28), paste 27 ms (24–32), structural insert 25 ms (22–29), replay 597 ms
(500–612), reopen 693 ms (674–703), and retained RSS 65,352 KB
(65,304–65,496). The first design showed higher variance in import than the
other measures; all samples still meet the provisional local budgets.

The manual probe now accepts `MARKOFF_BENCH_EXTRA_EDITS=1..500`, commits each
edit as its own action, and checks both first-block text and accepted MAB count
after reopen. One 50-extra-edit run on the foundation design committed and
reopened 54 accepted MAB records, verified the exact edited text and block
count, and measured 1,101 ms for the 50 local edits, 551 ms accepted-prefix
replay, 597 ms reopen, and 82,092 KB retained RSS. This is one larger-history
sample, not a budget or durable-provider result. The updated probe SHA-256 is
`435bfd86ca73d2f114eb6b1881dc7245b30c2871ed78f7337420072eec28d760`; the
current build-input snapshot is
`1e8f498894b7acc70f292945ffe06bade04f02d29e95684eb90c20ef2b3d5551` (1,707
files, 83,403,515 bytes, excluding `docs/`). The staged Markoff and alpha.5
install digests remain unchanged from the expanded corpus follow-up.


**Provider-backed collaboration app cutover (2026-10-14):** the standalone
candidate `markoff-collab-testapp` now opens two independent ephemeral providers
for the same logical document scope, registers both `CollabConsumer` instances,
imports through peer A, and presents both documents in the active Canvas editor.
The app detaches editor views before destroying their documents. Candidate CMake
now includes the app when standalone apps are enabled, without requiring the
retired Live leaf; the pinned-engine QML app remains behind the legacy+Live
configuration. The offscreen app smoke test, transport test, and bidirectional
action-consumer test pass 3/3. A fresh standalone Release tree rebuilt all
targets and passed 94/94 CTest, including the new app smoke test. The separate
optional Live test suite remains incompatible with vNext APIs and is still
excluded from candidate acceptance.

**A13/A14 source refresh (2026-10-14):** after the provider-backed app and
Canvas migration, the source fingerprint is
`b0898373466ea4945b24fafd8b6e671cb5a513a621e1d5c49dd8d7e558303f49`
(1,707 files, 83,397,151 bytes), excluding docs and the two build trees.
Installed Markoff and alpha.5 prefix digests remain
`9e78915825bf32e7bf672df9f2282deda7c55a7d8671cba75f665b35618b0653` and
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`. The
app exercises ephemeral reference providers only; host storage and production
transport remain separate handoff work.


**Current app-enabled configuration acceptance (2026-10-14):** after the
provider-backed Canvas collaboration app cutover, the full standalone Release
tree passes 94/94 CTest in 13.31 seconds, and the full dedicated Debug tree
passes 94/94 in 25.51 seconds. The RelWithDebInfo ASan/UBSan tree also passes
94/94 in 42.85 seconds with `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=print_stacktrace=1`. The first sanitizer invocation correctly
reported the newly enabled Canvas app smoke as Not Run because that app target
was not part of the focused build; after building `markoff-canvas-app`, the
complete suite passed. The app smoke and transport/action consumer tests also
passed as a focused 3/3 in each configuration.


**Post-review app smoke and source fingerprint (2026-10-14):** changed
provider/open/import construction failures in the collaboration demo from
uncaught C++ exceptions to explicit `qFatal` diagnostics. Rebuilt the app and
reran its offscreen smoke in Release, Debug, and ASan/UBSan; all three passed.
The immediately preceding full app-enabled suites passed 94/94 in Release,
Debug, and ASan/UBSan; the small diagnostics-only change does not alter their
success path. The refreshed build-input SHA-256 is
`9e8be38e4a4e639175caa3ea9588e1234392c4a22d13aa873fa372ff98114974`
(1,707 files, 83,397,076 bytes), excluding docs and generated build trees.
Installed Markoff and alpha.5 prefix digests are unchanged.


**Provider-state notification completion (2026-10-14):**
`ProviderBackedDocumentState` now invokes its state-change callback on every
actual lifecycle-state transition, rather than relying on reservation recovery
to notify observers. `MarkoffDocument` maps this to `providerStateChanged`;
Canvas uses it to recompute pending and effective read-only state. A durable
reference-provider regression injects an append-before-rename failure and proves
the view becomes pending/read-only, then recovers the reserved gap and proves
both states clear. The focused Canvas and provider-backed document tests pass
in Release, Debug and ASan/UBSan (2/2 each); the full standalone Release suite
passes 94/94 after the change. The updated source fingerprint is
`8b1577e754ff97e8f43b78c0edf06b45298193235aa4229a86730efccc0577bf`
(1,707 files, 83,400,878 bytes), excluding docs and generated build trees.
Installed Markoff and alpha.5 prefix hashes remain unchanged.

**Parser and mixed-history integration refresh (2026-10-14):** after adding
structured CommonMark link-reference fields to the public parser block,
rebuilding every Release target refreshed downstream consumers and the full
offscreen candidate CTest passes 94/94 in 5.94 seconds at parallelism 2. This
includes multiline/escaped/entity/parenthetical/duplicate-reference import coverage and
the mixed text/list/map compensation undo, allocation-failure recovery, and
redo checks, and Markdown-fork undo isolation. Focused parser and provider-
backed document tests also pass under ASan/UBSan after rebuilding their
dependency chain from the changed public header. The current non-doc source
snapshot is
`4494be0efd06f0c338a6eafe459615a759e199d9e036af43e6768f9247180bce`
(1,707 files, 83,417,500 bytes); the installed Markoff prefix is
`ebf4e786afde89e1b247661e8d2d86d6d058f50392c2ec00920c38ea0e7da573` and
CollabText alpha.5 remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.

**Installed parser-header consumer refresh (2026-10-14):** rebuilt and
installed the current Release Core, Source, Styled and Canvas libraries after
the parser public-header change. The separate installed-header Core and
three-editor consumer CTests pass 2/2, and `ldd -r` reports no unresolved
symbols when run with the installed library directory in `LD_LIBRARY_PATH`.
The refreshed installed Markoff prefix digest is
`dbd354a7e68ce1eaf5029179c836148010a57f30f1b9952d89fb236e8ef191b9`
(221 files, 20,785,812 bytes); source and alpha.5 digests remain as above.

**Multiline title and current-source refresh (2026-10-14):** added LF and
CRLF soft-break coverage inside quoted reference titles. Focused Release and
ASan/UBSan provider-backed tests pass, and the complete Release candidate
suite passes 94/94. The current docs-excluded worktree digest is
`5df950d762358efae93c964b40356d1a2c2e90e1ad03725f57b7e51fafb8ae19`
(1,707 files, 83,418,647 bytes); the installed Markoff and alpha.5 digests
remain unchanged.

**Legacy-root parser-header compatibility (2026-10-14):** rebuilt the full
Release root-default tree with Live, apps and tests enabled after the public
parser block change. The build succeeds; focused parser top-level-block,
legacy D2-load, and block-buffer invariant CTests pass 3/3. This complements
the complete candidate Release suite and installed-package consumers above.

**Candidate public-header and installed-package audit (2026-10-14):** gated
additional pinned-only declarations and includes in `MarkoffDocument.h`,
including numeric replica/remote-op APIs, `TextAnchor`, flat-edit entry points,
remote cursors, and legacy reload helpers; Canvas no longer connects to the
candidate-absent `documentReloaded` signal. The candidate Release suite passes
94/94, and the pinned Core target plus focused pinned D2/render tests pass.
Rebuilt and installed current-source Release Core, Source, Styled and Canvas
through the parent-shaped harness. Both separate installed consumers compile
and pass (2/2); `ldd -r` resolves both consumer binaries with the installed
library path. The install script also required building its bundled `c4core`
target before installation. Current docs-excluded source SHA-256 is
`2e76a70aca7bdb0b8c5ea060c3868f027c940c0b27079b814375ff3f7840d9bb`
(1,707 files, 83,459,653 bytes); installed Markoff is
`5076169a64302ef6e17008cbe8fa1e2d2de0a2db06a4a046b2df94decacce9a3`
(221 files, 20,790,924 bytes), and alpha.5 is unchanged at
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.

**Pinned full-build acceptance after header split (2026-10-14):** rebuilt the
existing Release root-default tree with the pinned engine, apps, retired Live
leaf and all test targets enabled. The complete build succeeds, and the full
offscreen root CTest manifest passes 324/324 in 69.36 seconds with parallelism
2, including the Live QML and view-contract tests. This verifies that the
candidate-only header guards preserve the configured pinned consumer surface;
it does not change the recorded candidate+Live configure rejection or resolve
the separate history-retirement policy.

**Root dependency-scope and documentation refresh (2026-10-15):** moved
`KF6SyntaxHighlighting` discovery to the root scope so Qt's deferred app/test
finalizers can resolve the public dependency. The pinned collaboration app now
requests all Qt Quick/QML components even when `Qt6::Core` was already
discovered by an earlier subdirectory; this loads QTP0005 and the other QML
policies explicitly. Root reconfiguration is warning-free, the complete pinned
Release build succeeds, and CTest passes 324/324 in 165.88 seconds at
parallelism 2. Updated the top-level README and Canvas CMake description to
identify Canvas as the primary editor, mark Live as retired, and describe the
current standalone/embedded engine defaults and parent-export install model.
The standalone provider-backed Release tree also reconfigures without Qt/KF
dependency warnings, rebuilds all targets and passes 94/94 CTest in 27.38
seconds, including both app smoke tests.
The refreshed docs-excluded source fingerprint is
`0263791bb08c99ecc0dc6ba492cbe41a8775accddbe576303ce43f2b91df9707`
(1,707 files, 83,460,493 bytes); installed Markoff and alpha.5 hashes are
unchanged at `5076169a64302ef6e17008cbe8fa1e2d2de0a2db06a4a046b2df94decacce9a3`
and `3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.

**Scoped selection and session positions (2026-10-15):** candidate `Selection`
and `Session` now expose only scoped opaque positions. Candidate selection JSON
contains `anchorPosition`/`activePosition`, and session scroll state uses
`topPosition`; neither emits or accepts the old `TextAnchor` JSON fields.
Candidate Core no longer declares the `TextAnchor` JSON helpers or publishes
the `AnchorJson.h`/`AnchorJson.cpp` implementation or the retired `Cursor.h`
header, and candidate installs exclude `AnchorJson.h`, `TextAnchor.h` and
`Cursor.h`. Added candidate selection/session round-trip coverage and
retained the existing tests for the internal pinned build configuration. The
candidate Release build and CTest pass 93/93; pinned Core rebuilds and the
focused anchor, selection and session tests pass 3/3. This API cutover has no
deprecation window or old-install compatibility promise.
Candidate regressions explicitly feed legacy selection and scroll-anchor JSON
to the readers and verify that the old positions remain unavailable and are not
re-emitted; the focused selection/session tests pass 2/2.
The clean installed package omits `TextAnchor.h`, `Cursor.h`, and
`AnchorJson.h`; the two installed Core and view consumers build and pass 2/2
CTest. Current
docs-excluded source SHA-256 is
`b432d36b8258c2067254b6377f4a6c92c27dfdbba42b4c3ee7941235fe66edf3`
(1,708 files, 83,466,436 bytes); candidate install SHA-256 is
`0af0d3fe3bb154c010723acec5526e11d6a7eddb1dd19e3b0b6b16d8ec670077`
(218 files, 20,779,206 bytes), and alpha.5 remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.

**Deferred-action reconnect retry (2026-10-15):** when a sender retransmits
the exact MAB already retained in the durable wait queue, `routeRemoteAction()`
now retries pending actions against the current accepted prefix. This covers a
dependency committed while the receiver was offline and lets the retransmitted
action receive its revision receipt instead of remaining stuck as deferred.
The provider regression now drives this exact reconnect path; the focused test
and full candidate Release suite pass (1/1 and 93/93). Rebuilt and installed
current Core, Source, Styled and Canvas; the Core and view installed consumers
pass 2/2 and the clean package omits `TextAnchor.h`, `Cursor.h` and
`AnchorJson.h`. Current docs-excluded source SHA-256 is
`6c7c1abf24eb59a86c8e0ce7de21aaade2a2b66caa9e2e080197692f1c072d80`
(1,708 files, 83,468,884 bytes), installed Markoff SHA-256 is
`d1805c458bcf046274d9913d36c6e27d34cf29c2a5e1feea26b6c49530bbb026`
(218 files, 20,779,206 bytes), and alpha.5 remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.

**Transport reconnect replay (2026-10-15):** `InMemoryTransport` now retains
each pending action's stream together with its exact MAB bytes and resends
those envelopes when a peer reconnects, iterating a snapshot because a receipt
can synchronously remove an action during delivery. The transport regression
verifies the stream and bytes are unchanged; the provider-backed consumer
regression queues an initial import while disconnected, reconnects the peers,
verifies the receiver applies it and confirms the sender's durable outbox
receipt is cleared. The focused tests pass 2/2 and the full candidate Debug CTest passes
93/93 in 19.58 seconds. Real network transport and production-provider outbox
conformance remain open.

**Single-engine root cutover (2026-10-15):** root CMake now requires the
installed provider-backed CollabText Core package for standalone and embedded
builds. The pinned engine and retired Live leaf are no longer build choices;
explicit attempts to select either path fail at configure time. A fresh
candidate Debug configure/build succeeds, followed by the full offscreen CTest
manifest passing 93/93 in 39.84 seconds. A parent-shaped configure and
all-target build also succeeds with apps/tests disabled and no engine-selection
variable. The installed Core compile flags select the scoped-position API, and
the retired Live target is absent. `git diff --check` passes. The docs-excluded
source SHA-256 is
`d109eae39d69e69906e7f71376c424e320991f3a4551798cca4755db49f18cd9`
(1,708 files, 83,468,257 bytes); alpha.5 remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.
Retired implementation files and dead compatibility branches still require
source cleanup; this cutover removes the selectable legacy build path but does
not claim that cleanup is complete.

**Retired anchor API deletion (2026-10-15):** deleted `TextAnchor.h`,
`Cursor.h`, `AnchorJson.h` and `AnchorJson.cpp` from the source tree and removed
their target-source entries. Replaced the stale `tst_foundation_text_anchor`
candidate case with `tst_foundation_scoped_position`, which checks token
resolution after edits and rejection against another block's scope. The full
Debug build succeeds and CTest passes 93/93 in 44.41 seconds. Rebuilt and
installed current Core, Source, Styled and Canvas through the parent-shaped
harness; the fresh package contains none of the deleted headers, and both
installed consumers build and pass 2/2. The docs-excluded source SHA-256 is
`5c6fb0a73b1d101ca3f221c4916c344935b6c12f908f40b33175c72e8218b60e`
(1,704 files, 83,457,464 bytes); installed Markoff SHA-256 is unchanged at
`d1805c458bcf046274d9913d36c6e27d34cf29c2a5e1feea26b6c49530bbb026`
(218 files, 20,779,206 bytes), and alpha.5 remains
`3d874ff5bd164ed0068ad4d23b37568a50185629f9fbb299046e76b333ce7c23`.
Old tests and inactive source branches still mention these deleted types, so
the root build is clean but the wider dead-code audit remains open.

**Retired anchor-conversion adapter cleanup (2026-10-15):** removed the
unreferenced `AnchorConversion` source/header and its CMake source entries,
then removed the stale include from the inactive legacy `MarkoffDocument.cpp`.
The full candidate Debug build succeeds and offscreen CTest passes 93/93. The
inactive legacy document source and tests still contain other candidate/legacy
conditional code; A13 source cleanup remains open. `git diff --check` passes.

**Scoped-position public API cleanup (2026-10-16):** removed the legacy
`TextAnchor` fields, `isReversed()` approximation and legacy anchor JSON branch
from `Selection`; removed the equivalent legacy start anchor and JSON branch
from `FoldRef`. Core and the four focused selection/session/fold/Canvas tests
pass, and a refreshed install build plus both external installed consumers
pass 2/2. Neither header or implementation refers to `TextAnchor` or
`AnchorJson` now. Remaining public Core headers and inactive legacy source
branches still need the same cleanup audit.

**Session scoped-position cleanup (2026-10-16):** removed the legacy
`topVisibleAnchor()`/`scrollChanged(TextAnchor)` API, the local `topAnchor`
state and the legacy `topAnchor` JSON reader/writer from Session. Session now
compares scoped selection positions, carries scoped scroll positions and
matches folds using scoped tokens only. Focused Session and Canvas session
position tests pass 2/2, the refreshed installed Core and view consumers pass
2/2, and the full candidate Debug build and CTest pass 93/93 in 25.77 seconds.
The preceding full build initially ran out of space in `/tmp`; it completed
after directing compiler temporary files to the workspace build directory.
`git diff --check` passes.

The complete candidate Debug build also succeeds after the reconnect fix, and
its offscreen CTest manifest passes 93/93 (37.01 seconds at parallelism 4).
The first parallel build attempt reported a transient missing-main link for
`tst_foundation_edit_sequence`; rebuilding that target and then the full tree
resolved it without a source change. `git diff --check` passes.
