# ARCHIVED status board — 2026-10-16

Copied from `docs/STATUS.md` when the live board was reset on 2026-10-17.
Historical baselines, closed-arc narrative, and the long CollabText progress
summary live here. The live board is `docs/STATUS.md`.

---

# Markoff — live status board

> The single live status board. Keep it **sparse**: workfront, baseline,
> dormant items, pins. Superseded dated entries move to
> [`STATUS-LOG.md`](STATUS-LOG.md); closed-item detail lives in
> `docs/archive/`.

**Last updated:** 2026-10-16 — **single-engine CollabText adoption in progress**.
Root CMake now requires the installed provider-backed CollabText Core package
for standalone and embedded Markoff builds. It no longer offers the pinned
engine or retired Live leaf as build choices. The full Debug build succeeds and
the offscreen candidate suite passes 93/93, including reconnect replay: queued
action envelopes retain their stream and exact bytes, and a provider-backed
consumer delivers a disconnected initial import and persists the returned
receipt. The two installed Markoff consumers pass against the fresh package;
the clean install omits `TextAnchor`, `Cursor` and `AnchorJson`. These checks
use Markoff's reference provider, so production host storage and real network
transport remain separate acceptance work. Core `Selection` and `FoldRef`
now expose scoped positions only; their legacy anchor fields and JSON readers
are gone. Focused selection/session/fold/Canvas tests and both installed
consumers pass after the API cleanup. The A01–A16 roadmap remains authoritative;
Corbomite stays paused until A16.
(Earlier: H arc landed 2026-08-30, `9b0138f8`; the canvas production
arc is closed and its board body lives in `STATUS-LOG.md`; the E-arc is
formally closed.)

**H arc (Hologram) — CLOSED 2026-08-30.** Opt-in
`View::setHideMatchingFirstHeadingAsTitle(bool)` (+ `EditorWidget`
pass-through, `9b0138f8`); plan
[`plans/2026-08-30-hide-first-heading-as-title.md`](plans/2026-08-30-hide-first-heading-as-title.md);
consumer answer note:
[`handoff/2026-08-30-to-hologram-hide-first-heading-as-title.md`](handoff/2026-08-30-to-hologram-hide-first-heading-as-title.md).

## Workfront — Collabtext vNext adoption planning

User goal: migrate Markoff's CRDT integration to Collabtext's
`feat/fuguemax-placement` candidate. The latest Markoff candidate tag is
`collabtext-markoff-core-2026-10-13-alpha5` at
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b` (0.2.0-alpha.5, manifest v17).
The alpha.4 candidate remains immutable; Markoff's current submodule pin remains
unchanged until the production cutover is ready.

- **Plan:** [`plans/2026-10-11-collabtext-vnext-adoption.md`](plans/2026-10-11-collabtext-vnext-adoption.md)
- **Execution task authority:** [A01–A16 roadmap](plans/2026-10-12-collabtext-adoption-execution.md),
  with dependencies, implementation surfaces, acceptance evidence and startup
  commands. Parent milestones M0–M12 remain summarized in the adoption plan.
- **Coordination:** Corbomite development stays paused by user direction until
  final A16/M12 delivers the readiness handoff. Markoff completes its own path
  using reference providers; Corbomite's production storage/application gates
  follow its transition and are not prerequisites for starting it.
- **A13 clean break in progress:** removed MarkoffDocument's disconnected
  flat-buffer APIs and storage, raw unqualified anchors, save-triggered GC,
  local watermark/ack protocol, WatermarkCoordinator and D2 history-compaction
  APIs, plus the duplicate `BlockAnchor`, `Markoff::Live` cursor alias and
  `Markoff::Live::Detail::Coordinates` alias. Live and its coordinate tests now
  use Core's `Markoff::TextUnits` directly; the QML module no longer exports the
  removed header. `delegateGoingAway` now requires the delegate root, removing
  its unconditional one-argument behavior; QML and tests pass the dying root so
  a stale destruction callback cannot remove a replacement registration.
  Also removed the unpopulated parsed-block cache and its always-empty lookup
  APIs; anchors now resolve through their actual block identity.
  Saves retain D2 undo history and removed-block content; focused root tests
  pass 6/6 and optional live tests pass 3/3. Production CollabText wiring and
  broader A13 persistence/build acceptance remain open.
- **Milestones:** M0's relocated Core probe and M2's checked
  coordinate contract pass. M3 has a binary local-history codec/reopen probe
  and compile-checked MAB envelope, MMP map codec, typed MBK/MAV/MLR values and
  isolated durable-counter `ReplicatedMapStream`; fixed typed-value wire fixtures
  and malformed UTF-8/version/truncation refusals now pass in the relocated
  consumer probe. The Markoff-owned `MTG/1` envelope now binds CTGP intervals
  to full stream scope and accepted-prefix tokens; the list adapter validates
  both before gap apply. `MMG/1` now defines terminal counter coverage for
  Markoff map streams without introducing a Core primitive; replay enforcement
  remains open. The codec supports carrying `MDS/1` exact Markdown bytes,
  separator slices and source/cache-digested disposable parser caches atomically
  in `MAB/3`; import orchestration remains open. `MAI/2` defines the
  owner-private action-to-group, undo/redo ancestry, retirement associations
  and bounded prior map values. The earlier MAI/1 format is rejected;
  `MOH/1` composes that index and per-stream MAH snapshots while binding
  the prepared transaction and prior revision. A05 remains partial until these
  records are captured and restored by production recovery.
  MAB validates `kind-tag`,
  `block-attrs` and `link-ref` payload semantics. Its LWW projection is keyed by
  both target block and map key. `ScopedTextStream` and `ReplicatedMapStream`
  now apply accepted MTG/1 and MMG/1 intervals through scoped projection APIs,
  reject wrong-prior/covered identities and advance same-actor local allocation
  beyond terminal coverage; production accepted-prefix replay and map wiring
  remain open. M4 has an isolated
  `ScopedTextStream` adapter and per-stream initial-import slice; the
  2026-10-14 status still described production `MarkoffDocument` on the pinned
  engine. The 2026-10-15 root cutover now requires the installed candidate.
  M6 has bounded list, text and map
  stream routers with export/ack quarantine seams. The list router now retains
  unknown-order envelopes and retries structural dependencies after registration
  or arrival. The reference provider keeps
  an accepted identity-owner index for local preparations, and fresh accepted-
  prefix replay rejects repeated `(stream, actor, counter)` identities before
  applying records, with a restart regression. The accepted-prefix projection
  can now retrieve the exact operation payload and MAB envelope/revision by
  scoped identity on demand, without a second payload cache. Text/list/map
  routers check Core duplicate outcomes against that record and quarantine a
  changed accepted envelope; a durable consumer trace covers all three.
  Candidate `MarkoffDocument::acceptRemoteAction()` now accepts complete MABs,
  replays the accepted prefix before returning committed, and updates block and
  document notifications. Lifecycle preflight also rejects unresolved candidate
  operations and targets absent from the resulting IdList before provider commit;
  the unknown-target regression leaves the accepted prefix unchanged. Empty
  documents can register peer initial-import and replacement MABs, then publish
  and recover receiver-local history; replacement preflight accepts targets
  visible before or after the action. Two replicas also converge after
  opposite-order concurrent text MAB delivery. `MarkoffDocument::routeRemoteAction()`
  now bounds complete causally deferred MABs, drains them after dependencies
  arrive, leaves envelopes caller-owned on configured backpressure, and returns
  action-ID/revision receipts only after accepted projection updates. Permanent
  remote-action rejections now persist MQR/2 records with each operation and
  the exact enclosing MAB before returning a quarantined outcome; the decoder
  remains compatible with existing MQR/1 records. A provider close/reopen test
  verifies a changed accepted action-ID retry is durable and leaves the
  projection unchanged. Reopened documents can retrieve whole-action
  quarantines as exact MABs with validated scope, IDs and reason. Unknown-target
  waits remain in the bounded route and retry after accepted dependencies.
  MQR/2 wait records now persist before deferred acknowledgment and reload on
  document reopen; accepted and permanently rejected actions are filtered from
  recovered waits. The in-memory action transport now routes accepted receipts
  to the peer that offered each MAB, including deferred actions drained by a
  different peer; the action-mode `CollabConsumer` uses `routeRemoteAction()`.
  The provider lifecycle now emits exact local MAB bytes after acceptance and
  projection refresh, and action-mode `CollabConsumer` automatically submits
  them through the in-memory transport. A candidate integration test verifies
  two-way initial-import/replacement delivery and action-ID/revision receipts.
  Durable transport state, automatic quarantine retry and production network
  receipt dispatch remain open. M7
  has a CTPT boundary-token slice, not view integration.
  The vNext adapter now owns its CMake library target, consumed by the isolated
  package probe; at that earlier status point the normal build still used the
  pinned engine. Root CMake now requires the installed candidate in every build.
  M1's former IdList API blocker is resolved upstream in alpha.4 with ordered
  full entry identities; Core also now applies committed terminal gaps, and
  CTLO/0002 has a durable-reader promise. Corbomite has now supplied document
  UUID/copy/fork, shared-history and private-state design directions, accepted
  in Markoff's provider reply. M1 has a compiled provider API draft; production
  ownership/storage integration remains open. M3 still lacks
  Markoff's durable gap record/log/recovery integration. M5's upstream reader
  gate is closed, but Markoff's structural IdList port remains open. The
  candidate `BlockIdentity` now orders and hashes the complete actor/counter
  pair, with a consumer collision regression and a three-replica offline
  same-value insertion convergence test. The isolated package now has a
  provider-backed document lifecycle owner that opens, reconciles unresolved
  transactions, replays and gates first import/recovery before exposing writes;
  it retains the publication coordinator across recovery-gap publication, and
  its reopen traces finish committed private history or keep absent reservations
  gated until terminal coverage. Candidate `MarkoffDocument` now uses this
  lifecycle for provider-backed imports, edits, undo and remote MAB acceptance;
  app startup and durable host-provider acceptance remain open. Production
  `Markoff::BlockId` now has
  an additive full actor/counter identity form with complete equality, ordering
  and hashes, covered by `tst_block_id`. The unused MBI1 identity token codec
  has been removed; the opaque-frame key derives directly from actor bytes and
  counter. This structural form remains scaffolding because `MarkoffDocument`
  still owns the current numeric `IdList`. The unused candidate-to-`BlockId` conversion bridge
  was removed so the adapter exposes only its full actor/counter identity.
  The pending publication cache now keys on full `BlockId`,
  with same-counter/different-actor coverage. `MarkoffOp` and `MKOF` now use a
  single full-identity schema, with numeric schema 1 removed. Remote list
  allocation and production build integration remain open; current document
  buffer ops still originate from numeric IDs and cannot yet be serialized.
  Source/styled frame snapshots now key by full `BlockId`, QTextFrame matching
  uses the complete identity key, and Canvas accessibility hashes no longer
  narrow identities; focused root tests pass 3/3.
  Markoff CTest passed 213/213 excluding `tst_benchmark` after the wire change;
  strict count, target and trailing-byte validation was then added and the
  serializer test passed again.
- **M10 current finding:** both fixture streams use `blockTextStreamName()`.
  Text and map adapters now accept only an open provider session; the earlier
  direct ReplicaState constructors and MPSK/1 workaround are removed. The
  relocated alpha.5 Release consumer build and CTest pass 3/3, including exact
  full-route reservations, provider reopen, document/epoch separation and
  wrong-session refusal. This remains isolated adapter evidence, not production
  Markoff acceptance.
- **A02 reference provider — COMPLETE:** the isolated MREF/4 provider now has
  scope and process leases, exact reservations, crash reopen, corruption/authority
  refusal, pre-commit failure handling and post-rename/file-sync/directory-sync
  uncertainty injection with exact transaction reconciliation. The A06
  `ActionPublicationCoordinator` now validates MAB/MOH bindings and recovers a
  uncertain append after reopen; when persistence may have atomically replaced
  the durable file, the live session is poisoned until reopen. A definitively absent preparation
  is durably retired and its reservations can be covered by an MMG/1 map gap;
  this survives provider reopen and prevents later append of the retired action.
  Text/list MTG/1 publication and reopen reconciliation also pass. The
  coordinator refuses gap publication until `recoverAndRebuild()` succeeds;
  its test rebuilds a fresh map projection from a previously accepted MAB import
  and verifies the accepted value before MMG coverage. An MMG append made
  uncertain after atomic rename is recovered after reopen with the same gap ID,
  covering the full reservation once. Production Markoff
  private-history completion now also injects a definite pre-rename write
  failure after MAB commit, retains the preparation and write barrier, then
  recovers the exact MOH bytes after reopen without duplicating the action.
  Recovery snapshots now include per-record prior/committed revision tokens
  derived from the provider transaction index. A reopened action-to-MMG prefix
  verifies adjacent token continuity, giving Markoff replay an exact gap-scope
  binding point. The coordinator rejects discontinuous chains, reused revision
  tokens and heads that do not match the prefix before publication/gap/recovery;
  `AcceptedPrefixProjection` now rebuilds fresh list/text/map adapters from the
  complete prefix, checks operation routes and identities, requires exactly one
  valid MAB/3 initial import matching its durable source intent before accepted
  edits, and applies MTG/MMG against their exact prior tokens. It validates the
  committed owner-history action groups against their MAB, restores actor-local
  list/text undo snapshots, and retains the map compensation index. The durable
  cross-stream undo reopen trace and absent-map gap test use this replay path.
  A zero-operation initial-import absence trace now confirms proven-absent
  preparations are retired and the write barrier is held through rebuild, then
  released when no gaps remain. The lifecycle owner can rebind the same exact
  source to a new transaction ID after provider-proven absence, and the retry
  reaches ready state after lifecycle reopen; relocated consumer CTest passes
  3/3. The lifecycle owner also exposes live absent-action recovery: it rebuilds
  the accepted projection before publishing an MMG/1 gap, discards the tentative
  map write, and only returns to ready after gap commit. It also recovers a
  committed map MAB with pending private history and refreshes the accepted map
  projection after inspection. Candidate document/lifecycle wiring now covers
  provider-backed edits and complete remote MAB replay; app startup, bounded
  router delivery and host-provider acceptance remain open. Corbomite's
  production journal is a later acceptance gate.
- **Host boundary:** [provider contract](specs/2026-10-12-document-storage-provider.md)
  and [Corbomite reply](handoff/2026-10-12-to-corbomite-provider-contract.md).
  The draft covers leases, prefix revisions, reservations, explicit publication
  outcomes, commit-associated undo metadata and export revisions. It compiles;
  the session can now persist a first-import intent and the coordinator binds
  initial MAB source bytes to it before allowing that action. After definitive
  absence and complete gap coverage, the same source can be retried under a new
  transaction ID, and the retry MAB/private history commits after provider
  reopen;
  explicit in-memory and restartable local reference providers now pass their
  installed consumer checks, including reopen across prepared and accepted
  transaction states. Text, map and structural IdList provider-session
  allocation paths pass the consumer checks; repeated list values retain
  distinct structural identities and anchors. A single durable MAB action with
  text/map/list writes also survives provider reopen and replays into fresh
  projections with the same list identity and rendered text/map values. A fixed
  CTGP archive fixture round-trips and applies only after the provider accepts
  its gap transaction, preventing counter reuse. A
  child-process abrupt exit/reopen check preserves
  actor and reservation state, a competing process receives `writer_conflict`,
  corrupt MREF is refused, missing private identity is rejected against existing
  document state, and a forced pre-commit reservation write failure preserves
  prior counters. A pre-rename preparation failure also preserves the exact
  unattached reservation and prior accepted revision across reopen, then allows
  an identical retry. The relocated alpha.5 consumer now injects post-rename,
  file-sync and directory-sync failures; reopen reconciles the exact reservation
  and committed transaction without counter reuse or duplicate acceptance. The
  reference provider is not Corbomite's journal; broad recovery coordination and
  its production storage acceptance remain open.
- **CollabText adoption A03:** provider-backed text, map and structural IdList
  adapters pass the relocated alpha.5 consumer baseline. A durable mixed MAB
  action survives reopen and replays into fresh projections; repeated list
  values retain structural identity, and a fixed CTGP archive is applied only
  after provider commit. A reservation write failure also leaves a text edit
  unmodified and its stream counter/reservation unchanged before retry. A03 is
  complete at the isolated-adapter baseline.
- **Selective undo A04:** the production dispatcher still discards logged opIds
  and invokes stack undo on shared engines. Alpha.5 Core adds selective text/IdList
  group compensation, now exposed by isolated adapters. A relocated consumer trace
  proves A then B on shared order: selectively undo A, preserve B, replay/restore,
  and redo both exact block identities. The coordinator now rejects MAB actions
  whose owner-history index omits or misroutes operation identities. New MAI/2
  owner records persist each map write's prior key state; the map adapter emits
  a fresh inverse, refuses to overwrite a later same-key LWW head, and can
  compensate after replay from MAI/2. A durable adapter regression now
  publishes A and B, then one undo MAB across list, text and map, reopens and
  verifies B remains. The undo append injects uncertainty after its atomic
  rename; recovery after provider reopen inspects the exact undo transaction and
  persists its private history. The same coordinator rejects another write
  while that undo is unresolved.
  The absent-action test injects a pre-rename append failure, retires the exact
  preparation, rebuilds from the accepted prefix and commits its map gap.
  The initial-import path also covers coordinator prepare failure, reopen,
  rebuild and terminal gap; absent transaction IDs are durably tombstoned,
  including transactions whose preparation never reached atomic rename. The
  provider-backed adapter covers reservation failure atomicity. A validated
  transaction builder binds each MAB op to its adapter-reported reservation and
  the live accepted prefix; it rejects omitted staged ops, stale history and
  incomplete bindings. `ActionStager` provides the single typed conversion path
  from list/text/map adapter results to a MAB and reservation bindings; the
  provider test exercises that path. Map encoding now precedes counter
  reservation, with a regression proving rejected values consume no counter.
  Production MarkoffDocument group capture, publication and undo dispatch remain
  open. See the
  [A04 handoff](handoff/2026-10-13-a04-selective-history.md).
- **Architecture:** retain D2 per-block streams and Markoff-owned document,
  transport, structural maps and cross-stream action semantics. Integrate via
  public Core APIs with consumer-owned durable identity/history.
- **Gates:** settle multi-stream undo/publication before claiming parity; keep
  physical collection and log pruning disabled; integrate durable gap events,
  host absence proofs and accepted-prefix recovery before enabling writes after
  failed appends; finish Markoff and Wave acceptance against one immutable
  candidate. The candidate adds the Core gap apply path and durable CTLO reader;
  Markoff must still adopt and verify both.
- **M1 host findings:** Corbomite currently uses replica ID `1` and has no
  stable document ID separate from its renameable relative path. Markoff's
  current block counters do not establish uniqueness across devices. The plan
  recommends deriving composite BlockId from the structural insert identity
  and alpha.4 now supplies its ordered IdList identity API. Inspection of
  Corbomite's `NoteDocument`, Vault path-keyed
  cache and rename path handling confirms the document ID must be persisted
  with shared document metadata, while private `ReplicaState` stays in local
  application data. Corbomite's response defines proposed copy/fork and state
  policies; their implementations and provider acceptance remain open.
- **Candidate source:** `/home/clinton/dev/collabtext/.worktrees/reliability-v2`.
  Candidate report and response are linked from the adoption plan. Wave's Q06
  production acceptance remains external and open.

The G1 canvas accessibility arc closed 2026-09-30 (A5.4). Its manual Orca pass
(A5.3) remains deferred by the user and is separate from this workfront.

User-opened 2026-08-19, closed 2026-09-30 at A5.4. Orca pass (A5.3)
deferred by the user (needs `pacman -S orca` + `--direct` permission).
Goal was: make `Markoff::Canvas::View` usable with a screen reader.

- **Spec (normative):**
  [`specs/2026-08-19-g1-canvas-accessibility-design.md`](specs/2026-08-19-g1-canvas-accessibility-design.md)
- **Plan (CLOSED):**
  [`plans/2026-08-19-g1-canvas-accessibility.md`](plans/2026-08-19-g1-canvas-accessibility.md)
  — phases A1 (tree/roles/registration), A2 (text interface),
  A3 (notifications), A4 (folding/actions), A5 (acceptance); user
  gate A-G1 = when to run the Orca pass. First task is **A1.0**, a
  throwaway probe settling whether `QAccessible::Attribute::Level`
  survives Qt's AT-SPI bridge.
- **Platform notes (spec §4.6):** the AT-SPI bridge lives inside
  `libQt6Gui` (no plugin to install); Orca is not installed yet
  (`extra/orca 50.2-1`); `ROLE_BLOCK_QUOTE` and `ROLE_MATH` are
  unreachable from Qt — stated limitations.
- **Scope decision (user, 2026-08-19):** per-block accessibility tree
  (`View` as `QAccessible::Document` container, one text-interface
  child per block over its own buffer) — chosen over a monolithic
  flat `QAccessibleTextInterface`, which would have required a **C4
  amendment** for a whole-document offset space. Acceptance is
  in-process offscreen tests plus one manual Orca pass at arc close.
  Driver is Markoff's own roadmap, not a Corbomite deadline.
- **Motivating gap:** canvas has zero a11y; source/styled inherit
  Qt's for free, so G2 (canvas as Corbomite's sole LivePreview
  engine) regressed Corbomite's editing surface.
- **Decided out:** `QAccessibleTableInterface` (spec §6), theme-side
  a11y — contrast/motion/font preferences (spec §1).
- **Baseline:** 213/213 (`tst_canvas_accessibility` 90 cases). Core needed nothing.

## Previous workfront (CLOSED) — canvas production arc (D5 part 1)

Opened 2026-08-13, closed 2026-08-15 at P7.3; all three gates now
decided (G1 reopened as the current workfront above, G2 done
2026-08-18, G3 retired `markoff-live` 2026-08-19). Arc-close baseline
**315/315**.

- Spec: [`specs/2026-08-13-canvas-production-design.md`](specs/2026-08-13-canvas-production-design.md)
- Plan (phase-by-phase SHAs + findings log):
  [`plans/2026-08-13-canvas-production-plan.md`](plans/2026-08-13-canvas-production-plan.md)
- **Full board body** (phase-by-phase narrative, all gate records)
  moved verbatim to [`STATUS-LOG.md`](STATUS-LOG.md), 2026-08-19
  banner — read it there if you need the history.

Standstill from that arc is **lifted**: `markoff-core` and
`markoff-canvas` are open for ordinary work, not restricted to
plan-named seams. `markoff-styled` stays bug-fix-only (backs
Corbomite's Reading mode); `markoff-source` stays untouched,
permanently; `markoff-live` is retired. Queue **#18** was absorbed
into the closed plan.

## Test baseline

> **Historical baseline: 315/315**, recorded 2026-08-15 at the canvas
> production arc close (P7.2g); a later 213/213 baseline was recorded at G1
> close. Current migration evidence is tracked in the execution roadmap.
> Neither historical count establishes a passing result for current sources.
> The entries below are the chronological accretion that got us here
> (oldest first); read them for history, not for the current figure.

**Full suite: 310/310 (100%)**, re-verified 2026-08-14 at Phase 6
close (P6.4) — up from the 307/307 baseline at P6.0 via three new
canvas executables registered this phase (`tst_canvas_session_selection`
P6.1, `tst_canvas_remote_presence` P6.2, `tst_canvas_concurrency` P6.3).
`check-constitution.sh` clean (C1–C4) over 69 files. Honest re-read of
the phase's diff for C1/C2 smells the script doesn't literally
string-match: clean (plan findings log, P6.4 entry). The previously
reported "pre-existing build break" in `tst_view_contract_live_caret_rect`
(P6.1 finding) did **not** reproduce at P6.4's clean whole-tree
rebuild — stale build-directory artifact, not a real defect; dormant
item withdrawn. Perf re-baseline not re-run this phase (P6.0–P6.3 are
caret/selection/fold/presence bookkeeping + paint-time-only additions,
none of which touch the y-position walk E9 tracks — same judgment
P3.7/P4.8 made for their non-layout phases); last-held figures (P5.7):
load→paint 188 ms/500 ms, p95 keystroke 1.38 ms/16 ms, scroll-realize
11.6 %/30 %, RSS delta 0 KB/100 MB. The plan ratchets this up per
task; any drop is a regression (classify before fixing).

**Canvas-scoped suite (`scripts/run-tests.sh -R canvas`): 33/33**,
verified 2026-08-14 at P7.2 — up from 32/32 via one new executable,
`tst_canvas_drag_drop` (drag-out MIME shape, drop-position accuracy,
file-drop signal shape, middle-click no-op-when-unsupported).
`check-constitution.sh` clean (C1–C4). Full suite not re-run per the
plan's tier rule (P7.2's diff stays inside `libs/markoff-canvas/`, no
core seam touched); the 310/310 full-suite figure above is still the
last verified whole-tree number, from P6.4.

P7.2a (2026-08-14) re-verified 33/33 on the same canvas-scoped suite —
no new executable, `tst_canvas_undo` extended with
`consecutive_printable_keys_coalesce_into_one_undo_entry`.
`check-constitution.sh` clean. Full suite not re-run (diff stays
inside `libs/markoff-canvas/`; the only core header touched,
`Cmd/D2.h`, is pre-existing public API, no core seam changed).

P7.2b (2026-08-14): canvas-scoped suite **34/34** — up from 33/33 via
one new executable, `tst_canvas_editing_command_floor` (word motion/
delete, doc start/end, delete/move/select-line, Esc-simplify; 4
falsification-backed groups). `check-constitution.sh` clean (C1–C4,
71 files). **Correction:** this task's agent flagged
`tst_canvas_concurrency`'s gremlin-fuzz failure as "pre-existing" —
independently re-bisected by the orchestrator and found to be a real
regression introduced by P7.2a, one commit prior; fixed same day
(`623ed6ca`, see Dormant items below and the plan findings log).

Regression fix (2026-08-15): full suite **312/312** (up from 311/311
at P7.2) — the only run since P6.4 that re-verified the whole tree,
since a core header (`UndoLog.h`) was touched. `check-constitution.sh`
clean.

P7.2c (2026-08-15): canvas-scoped suite **35/35** — up from 34/34 via
one new executable, `tst_canvas_auto_pair` (8 cases: auto-pair,
type-through, wrap-selection, bold two-star completion, fresh-pair
Backspace, manually-typed-pair non-interference). `check-constitution.sh`
clean (C1–C4, 72 files). `tst_canvas_concurrency` (incl. the gremlin
fuzz) explicitly re-run 3x to confirm no regression in the shared
typing path. Full suite not re-run per the plan's tier rule (no core
seam touched this task).

P7.2d (2026-08-15): **full suite 313/313** (up from 312/312 at the
regression fix) — required by the plan's own tier rule since this
task's diff touches `libs/markoff-core/tests/` even though it's
test-only, not a source change. `tst_structural_key_handler` 27/27
(up from 25/25): two new regression tests
(`listitem_enter_mid_split_ordered_list_renumbers`,
`listitem_enter_empty_at_indent_gt0_outdents_not_exits`). No canvas
files touched, so `check-constitution.sh` not applicable this task.

P7.2e (2026-08-15): **full suite 314/314** (up from 313/313 at P7.2d)
— required by the plan's tier rule since this task added
`Theme::Slot::SelectionOccurrenceBackground` (a new, additive core
`Theme.h` enumerator + light/dark default colors). One new executable,
`tst_canvas_selection_matches` (3 cases: selecting a repeated word
highlights its other occurrences, not its own span; sub-minimum-length
and whitespace-only selections highlight nothing; collapsing the
selection clears all highlights). `check-constitution.sh` clean
(C1–C4, 73 files).

P7.2f (2026-08-15): **full suite 315/315** (up from 314/314 at P7.2e)
— required by the plan's tier rule since this task added
`Theme::Slot::BracketMatchBackground` (a new, additive core `Theme.h`
enumerator + light/dark default colors). Canvas-scoped suite **37/37**
(up from 36/36), one new executable, `tst_canvas_p72f` (9 cases across
4 falsification pairs: scroll-past-end room-to-scroll, placeholder
focus-independence + typing-clears-it, bracket-match nesting depth,
drop-cursor drag-move tracking). `check-constitution.sh` clean
(C1–C4, 74 files).

P7.2g (2026-08-15): **full suite 315/315** (unchanged count — one new
`Theme::Slot::InvisibleCharBox` enumerator, required the full-suite
tier rule same as P7.2e/P7.2f, but no new test executable — extended
`tst_canvas_projection` with 2 cases). Canvas-scoped suite still
37/37. `check-constitution.sh` clean (C1–C4, 74 files). All 7 F1
gap-closure sub-tasks (P7.2a–g) now complete.

**Baseline reset 2026-08-19 (G3, `markoff-live` retired):**
`MARKOFF_BUILD_LIVE` now defaults OFF, so the default
`scripts/run-tests.sh` run no longer builds/registers the
`markoff-live` test executables. Re-verified same day: **207/207
(100%)** on a from-scratch `cmake -S . -B build-dev` + full build +
full suite, default config. The 315/315 figure above remains accurate
for a `-DMARKOFF_BUILD_LIVE=ON` build (source unchanged, build-fix-
only) but is no longer the default-config baseline; future baseline
figures in this doc are the default-config (LIVE-OFF) count unless
stated otherwise.

## Dormant items

- **CLOSED 2026-08-15 (commit `fc7ea6fe`):** `StructuralKeyHandler`'s
  indent-0 ListItem Backspace previously merged into the previous
  block (P7.2d finding); user decided to switch to CodeMirror's
  de-list-in-place semantics instead. `listItemBackspace`'s indent==0
  branch now converts the block to `Paragraph` + clears `MarkerStyle`
  (mirrors `listItemEnter`'s exit-list branch), never touching the
  previous block. No renumbering needed (de-listing only ever splits a
  run, never merges two). Cross-leaf check caught a `markoff-styled`
  test also asserting the old behavior (`markoff-live`/`markoff-source`
  had none) — renamed and rewritten to match. Full suite 313/313. Full
  writeup: plan findings log, "P7.2d addendum" entry.
- **CLOSED 2026-08-15 (commit `623ed6ca`):** the P7.2a-introduced
  gremlin-fuzz convergence regression (fixed seed `3237998146`,
  bisected to exactly one commit — see the plan findings log's
  "Regression fix" entry, inserted between P7.2a and P7.2b, for the
  full root-cause writeup). Root cause: `UndoLog::
  maybeCoalesceOrTransaction`'s coalesce-extend branch never fired
  `onCommit` for ops after the first keystroke of a coalescing run, so
  they were applied locally but silently never sent to collab peers —
  a latent bug in `UndoLog` made reachable for the first time by
  P7.2a's `insertPrintable` → `Cmd::insertCharacter` routing. Fixed by
  firing `onCommit` explicitly for the coalesced-in ops. Gremlin fuzz
  now passes 5/5 consecutive runs; P7.2a's own coalescing test still
  passes — both properties hold simultaneously. Full suite 312/312.
- **Self-drop text-drag Move does not delete the source selection**
  (P7.2, 2026-08-14) — dragging a selection and dropping it back
  inside the SAME `View` duplicates the text instead of moving it
  (the source selection is left intact even though the drop reports
  `Qt::MoveAction`). Deliberate scope cut, not a bug: deleting the
  source after the drop's own insert has already run in the same
  document would need re-deriving the source range's now-stale byte
  offsets, which this leaf's raw per-block-byte `CanvasCursor` model
  (C4) has no cursor-adjustment machinery for. Cross-view/cross-app
  Move (drop target ≠ this view's own viewport) works correctly. Full
  writeup: plan findings log, P7.2 entry.
- **`BlockLayoutCache::m_preeditByte` goes stale across a remote edit
  mid-composition** (P6.3 finding, 2026-08-14) — cosmetic only,
  bounded to the paint between a remote edit landing and the next real
  IME event. `View::onDocumentChanged()` re-resolves the CARET's byte
  offset from the Session anchor correctly (P6.1) and that IS what
  commit-time positioning uses, so no functional bug results — but the
  cache's own preedit splice point (`BlockLayoutCache::setPreedit`'s
  stored offset) is never re-issued from `onDocumentChanged()`, so a
  remote edit before the caret during composition visually splices the
  preedit at the wrong position for one paint. Fix sketch: in
  `onDocumentChanged()`, after caret resolution, re-call
  `m_cache->setPreedit(...)` with the fresh `m_caret` when
  `isComposing()`. Not fixed this task (production `View.cpp` change,
  outside a tests-only task's scope, and no public inspection surface
  exists to test it directly). Full writeup: plan findings log, P6.3
  entry.
- **Remote presence name flag has no fade** (P6.2, 2026-08-14) —
  `Selection::cursorVersion` is an opaque monotonic counter, not a
  timestamp, so a real "fade out N seconds since last move" needs
  either a core timestamp field (out of P6.2's closed-core scope) or
  view-local per-participant timer state this leaf's draw-time-only
  design otherwise avoids. Painted at full, constant opacity. Full
  writeup: plan findings log, P6.2 entry.
- **`MarkoffDocument::blockAt(const TextAnchor &)` always returns
  `nullopt`** (P6.1 finding, 2026-08-14) — its implementation reads
  `d->latestBlockRanges`/`d->latestBlockAnchors`
  (`MarkoffDocumentPrivate.h`), which are never assigned anywhere in
  `markoff-core/src` (confirmed by a whole-tree grep) — not "stale
  after D2 edits" as a neighboring comment on `offsetInBlock()`
  suggests, but permanently empty on every document and edit path.
  `blockAnchorAt(int)` and `blockByteRange(BlockId)` share the same
  dead-data dependency and are equally non-functional. P6.1 needed
  `blockAt()` for its Session-anchor resolve step and worked around it
  (`TextAnchor::block()` + the confirmed-D2-safe
  `offsetInBlock(BlockId, TextAnchor)`) rather than fix it —
  `markoff-core/` was closed for that task. Worth a real core session:
  either populate `latestBlockRanges`/`latestBlockAnchors` for real, or
  retire the three accessors and redirect any other silent callers to
  the D2-safe alternatives. Full writeup: plan findings log, P6.1 entry.
- **CLOSED 2026-08-14 (P6.0, commit `f2e705d5`):** the D2-safe core
  accessor now exists (`MarkoffDocument::blockCrdtAnchorAt`/
  `resolveBlockCrdtAnchor`) and `View::toggleFold()` writes every fold
  through to `Session::foldedRegions`/`FoldRef`. **New dormant item
  opened in its place:** the reverse direction (rebuilding
  `View::m_foldedHeads` FROM cold/restored `Session` state) is
  unsound as designed — per-block CRDT buffers share one Lamport
  clock seed (`Buffer(d->replicaId)`, no per-block offset), so
  different foldable blocks' byte-0 anchors routinely collide and
  `FoldRef::start` alone can't disambiguate which block a collided
  anchor names. `m_foldedHeads` stays the View's own write authority;
  Session is a write-through mirror only. Closing generally needs
  `FoldRef` to carry a block identity (core schema change). Full
  writeup: plan findings log, P6.0 entry.
- **Inline math (`$...$`) renders as styled monospace, not real
  glyphs** (P5.3 finding) — **now a scheduled arrival, not a Qt
  limitation** (2026-08-14 investigation): standalone `QTextLayout`
  gains a C3-clean inline-object path in **Qt 6.12** (qtbase
  `be73ca50a34`; U+FFFC + `QTextImageFormat` via `setFormats()`).
  Build machine is on 6.11.1, so the styled-text fallback stands
  until then. Mechanism: spec §4.5; implementation steps: plan gated
  task **G-Q612**; full record: plan findings log 2026-08-14. No
  pre-6.12 shim (rejected, same entry).
- **Parser gap: `latex_span`/`latex_block` delimiter spans never get
  `parentCharStart`/`parentCharEnd`** (P5.3 finding) —
  `collectParentRanges` checks for a `latex_span` node type
  markoff-parser's grammar never emits (both `$...$` and `$$...$$`
  parse as `latex_block`). Worked around with per-block (not per-span)
  math reveal; revisit if per-span granularity is ever needed.
- **Table/code horizontal-pan-within-own-rect** (P4.5 carried scope):
  tables and code blocks currently take the (possibly narrowed)
  readable-line-width column like any other block instead of
  overflowing it with their own horizontal scroll (Obsidian's actual
  behavior). Needs per-rect horizontal scroll-offset state that
  doesn't exist yet in `BlockLayoutCache`/`View`. Should land before
  the Obsidian-parity audit (P7.3) if still open then.
- **`Theme` default palette has no colors for the 16 `Code*` token
  slots** (P4.6 finding) — `Kf6SyntaxHighlightService` wiring is
  correct end-to-end but renders no token differentiation under
  `defaultLight()`/`defaultDark()` until those slots are defined.
  Small, cleanly-scoped core follow-up.
- Queue **#13** — source cursor/selection translation rewrite.
- Styled tables — in-grid cell edit, row/col ops (moot if G3 retires
  styled; frozen until then).
- Release scaffolding — install/export rules + header tiering for a
  non-submodule consumer.
- E-arc — CLOSED 2026-08-19 (formally retired, not merely dormant;
  scope shipped under canvas Phase 5 — `docs/archive/e-arc/`).
- Discipline-Log open smells — `docs/queue.md`.

## Corbomite pin

Corbomite tracks Markoff at `libs/markoff-family`. As of 2026-08-12
(Corbomite `fde31e82`) pinned at Markoff `b349f122`. Re-pin guidance:
jump to current master when next touched; **never** pin into
`8c13c5d..079ac1f` (list-after-table SIGSEGV window, fixed `b1b238f`).

## History

Arc dispositions, closed queues, and dated banners:
[`STATUS-LOG.md`](STATUS-LOG.md), `docs/archive/` (incl. the
2026-08-13 pre-canvas snapshots of CLAUDE.md and queue.md; spike
verdict: `specs/2026-08-13-markoff-canvas-spike-design.md` §10).
