# Corbomite open asks — triage and order

Updated 2026-10-19. Task list for the workfront opened by Corbomite's
consolidated asks of 2026-10-18
(`~/dev/Corbomite/docs/handoff/2026-10-18-to-markoff-consolidated-open-asks.md`),
39 numbered asks plus a punch-list table that actually has **24** rows,
not the 21 the document claims. Sources read at Markoff
`dca64686`, Graffodil `011b342f`, collabtext at both the installed pin
`e8a8e7d1` (via `git show`) and the local `master` checkout `e4779644`.
Collabtext `master` is **not** the pin: it is 329 commits behind the pin
and carries 2 packaging commits the pin lacks.

**How this was verified.** Read-only audit: every cited site was read in
the current tree. Nothing was built or run, so no test count is claimed.
Where an ask cites a file that no longer exists, the verdict says so.

**The headline.** The ask document says every row was checked at pin
`dca64686`, but its markoff-core evidence (`MarkoffDocument.cpp`,
`MarkoffDocumentPrivate.h`, `SourceTextDocumentBinding.cpp`) does not
exist at that commit — the A13 rewrite replaced them with
`MarkoffDocumentVNext.cpp` / `SourceTextDocumentBindingVNext.cpp`. Four
core rows are therefore already closed (M2, M10, M22, M12's mechanism)
and four more are partly closed (M17, M18, M27, M28); the two collabtext
P0s, M1 and M29, are closed at the installed pin — a first pass read
collabtext `master` instead of the pin and got them wrong. The parser,
view-leaf and Graffodil findings are real and current.

## Verdicts

Sizes: **S** ≈ under a day, **M** ≈ 1–2 days, **L** ≈ more than that.
"Landing" is the repo whose board must carry the task.

| Ask | Handoff severity | Verdict | Current anchor | Size | Landing |
|---|---|---|---|---|---|
| M1 | P0 | **CLOSED at the pin** | pin `IdList.cpp:160-200` (`try_remove_at` → `ErrorCode::unresolvable_anchor`), `:70-83` (`try_insert_after`), `:1218-1243` (`apply_concrete` returns false before observing); tests `tst_operation_validation.cpp:822,835,847,895` | 0 | reply |
| M2 | P0 | **CLOSED** — mechanism removed | no `pendingOpPayloads`/`registerOp`/`localOpsProduced` anywhere; one action = one MAB, `routeRemoteAction` | 0 | reply |
| M3 | P0 | OPEN | `Document.cpp:379-388` (`parsedOnce` set before parse; no mutex) | S | parser |
| M4 | P0 | OPEN | `TreeSitterParser.cpp:247-290`, early return `:261`, teardown `:269-271` never reached | S | parser |
| M5 | P0 | OPEN | Graffodil `BatchRenderer.cpp:28-32`; `synchronize()` `:128-129` | S | Graffodil |
| M6 | P0 | OPEN | Graffodil `GraphEdgeItem.h:121-122`; `GraphScene.cpp:30-63`; `GraphEdgeItem.cpp:241-242` | M | Graffodil |
| M7 | P1 | OPEN | `FormatPass.h:21` unused; `FormatPass.cpp:425` reads only `fontScale`; `computeBlockHash` `:217`; `StyleApplier.cpp:39`; `Theme.h` has no `operator==` | M–L | styled (exception) |
| M8 | P1 | OPEN | `MarkdownView.h:105`; no `destroyed` wiring in core/source/styled; canvas has the pattern, `EditorWidget.cpp:206-216` | M | core + leaves |
| M9 | P1 | PARTIAL | `MarkdownView.h:64-66` has no availability query; `MarkoffDocument.h:266-278` has `undoD2/redoD2` + per-block `canUndoForBlock` only | S | core |
| M10 | P1 | **CLOSED** — APIs deleted | `latestParse`, `parsedDocument`, `blockAnchorAt`, `blockByteRange`, `blockAt` absent | 0 | reply |
| M11 | P1 | OPEN | `SearchEngine.h:27-33`; `SearchEngine.cpp:23-26` `Q_ASSERT` only; `FindController.h:17-18` still implies support | S–M | core |
| M12 | P1 | **STALE** | `onSaveComplete`, `wantsAcksAtWatermark`, `ackedWatermark`, `collectGarbage` absent; `save()` = `snapshotForExport`/`acknowledgeExport`; collection is disabled by design | 0 + question | reply |
| M13 | P1 | OPEN | `SourceTextDocumentBindingVNext.cpp:336-345,292,304`; uncached `widgetFlatView` `MarkoffDocumentVNext.cpp:4915`; `sepViewPosOf` `:231-238` | M | core |
| M14 | P1 | OPEN | Graffodil literals as listed; `BatchRenderer.cpp:108` unconditional colour (cites `:112`) | M–L | Graffodil spec |
| M15 | P1 | OPEN | Graffodil `GraphScene.cpp:75-78`; five scan sites; no `Spatial` consumer in Core | L | Graffodil spec |
| M16 | P1 | OPEN | `SearchEngine.cpp:53-64`; no debounce `FindController.cpp:112-116` | S–M | core |
| M17 | P1 | PARTIAL | coordinate fix landed in `LinkInteraction.cpp:47-71`; `handleMove:100-104` still O(document) per move | S | styled |
| M18 | P1 | PARTIAL | `replaceMatches` one action `:3237-3395`; per-block row still O(N) at `:4226-4234,3226,1842` | M | core |
| M19 | P1 | PARTIAL | `SugiyamaLayout.cpp:437-439` partial order; promotion loop `:497-524` exists but back edges keep `span <= 0` and no waypoints | M | Graffodil |
| M20 | P2 | OPEN | `MarkdownView.h` has no `session()`; Styled has it; Source keeps a private `QPointer` | S–M | core + source |
| M21 | P2 | OPEN | Source `Editor.cpp:113,117-122,152`; Styled `setSession` `:417-421` never emits on death | S | source + styled |
| M22 | P2 | **CLOSED** — removed | no `static std::atomic`, no `allocateD2BlockId`; `BlockId` from the engine | 0 | reply |
| M23 | P1 | PARTIAL | styled `Editor.cpp:494-527` no frame guard; canvas already fills row/col (`EditorWidget.cpp:326-329`), so "no leaf" is wrong | S | styled/source |
| M24 | P1 | OPEN | `InnerEditor.cpp:33-39` vs `StructuralTextEdit.cpp:44-81` | S | core helper + leaves |
| M25 | P2 | OPEN | `Editor.cpp:82-175` never rewires `m_findAdapter`; `StyledFindAdapter.cpp:60-85,136-180` | S–M | styled |
| M26 | P2 | OPEN — deliberate | `MarkoffCoreExport.h:4-7`; rationale recorded `markoff-core/CMakeLists.txt:9-16` | M | decision |
| M27 | P2 | PARTIAL | apps gated (`CMakeLists.txt:101-114`); headers installed; item 4 open: PUBLIC `Markoff::Parser`/`CollabTextVNext` + `<collabtext/...>` in `MarkoffDocument.h:18-20` | S–M | core |
| M28 | P2 | PARTIAL | no `LABELS`; `perf_load_bench` unregistered; no production doc-destroyed test; d5 test gone (item stale); `markoff-source/CLAUDE.md:27` `FindBar` stale; `UndoLog` moved to collabtext | M | all leaves |
| M29 | P0 | **CLOSED by redesign at the pin** | pin `Clock.cpp:164-178` keeps the documented asymmetric summary fold, `:179` adds `coverage_intersection`; `Buffer::compact` is gone (`collect_garbage` `:484`); `NetworkSim.cpp:262-265`; 3-replica coverage tests `tst_clock.cpp:167,190` | 0 | reply |
| M30 | P1 | OPEN | `TreeSitterParser.cpp:204-223` lead byte only; duplicate table `SourceSpan.cpp:10-28` | M | parser |
| M31 | P1 | OPEN | `TreeSitterParser.cpp:247-290` passes `nullptr` previous tree twice; zero-filled map | L | parser |
| M32 | P2 | OPEN | `PerfProbe.h:40-43`; in-loop probe `TreeSitterParser.cpp:279` | S | parser |
| M33 | P2 | **DELIVERED, differently** | `setFrontmatterValue`/`removeFrontmatterValue` `MarkoffDocumentVNext.cpp:4251-4278` via `publishMapValue:371`; acceptance undo test missing | S | core test |
| M34 | P2 | OPEN (doc only) | `Origin.h:14`; `Origin` has no production reader left | S | core |
| M35 | P1 | OPEN | `Document.cpp:42` and `:186` (`markdownContent`; handoff cited `:191`) | S | parser |
| M36 | P3 | OPEN | `markoff-parser/CMakeLists.txt:95` unconditional; canvas gate defaults ON | S | build |
| M37 | P1 | OPEN | canvas `EditorWidget` ctor has no card/scroll mode; `View` is a `QAbstractScrollArea`; `renderInto` const overload already delivered (`DocumentRenderer.h:33-34`) | M | canvas |
| M38 | P2 | **ANSWER** | one session per key confirmed; option 1 needs no API change | 0 | reply |
| M39 | P2 | **DELIBERATE** | trim `:3906`, split `:4127+`, tail on last block `:4197+`; now documented `Cmd/D2.h:65-66` and asserted by `smart_paste_inserts_typed_blocks_as_one_action` | 0 | reply |

Punch list (24 rows): 15 Graffodil (all still present; two ask details are
wrong — `edgeActionAt` does not copy paths, and `SpatialGrid` is not a
`BatchNodeItem` member), 8 packaging/vendoring rows across collabtext,
jkqtmathtext and rapidyaml, and 1 Markoff duplicate `Theme` member. Batch
them by repo; do not open one task per row. The collabtext integration of
2026-10-19 added one more family item: `Corbomite/cmake/CorbomiteConfig.cmake.in`
includes `CorbomiteTargets.cmake` without `find_dependency(collabtext)`, so a
clean-prefix `find_package(Corbomite)` cannot resolve the imported
`CollabText::Core` target — Corbomite-side, not an ask.

## Order

Waves are ordered by risk and unblocking value, not by ask number. Each
slice is one commit under the repo's own board.

### Wave 0 — Reply and close (no code, do first)

Send one reply that: confirms M38 option 1 (one card document; paint
holds a non-owning `const MarkoffDocument *`; `renderInto` already
guarantees it); states M39 is deliberate and keeps the documented split;
closes M2, M10, M22 and M12's mechanism, and M1/M29 as already fixed at
the installed pin; asks them to re-verify the markoff-core rows against
`dca64686`, since those cited files do not exist there; and hands back the
half-closed M17/M18/M27/M28 rows as "partly delivered, rest scheduled".
This deletes more than four consumer workarounds for the cost of writing
the letter.

Exit: reply artefact under `docs/handoff/`; no build.

### Wave 1 — Small, independent correctness and hygiene (Markoff)

One commit each, in this order:

1. **M3** `parsedFrontmatter` — `std::call_once` computing inside the
   initializer; document the threading contract in `Document.h`.
2. **M4** parser validity — tear down inline trees at the top of
   `parse()`, add `m_valid`, gate `inlineSpansFor`/`buildDocumentQueries`.
3. **M35** closing-fence helper used at `:42` and `:186`; add the four
   `tst_frontmatter` cases (`----`, `--- `, `---foo`, round-trip).
4. **M33** acceptance test — set a key, assert one `undoD2()` restores
   the previous frontmatter byte-for-byte. Fix only if it fails.
5. **M36** canonical `MARKOFF_BUILD_TESTS` gate propagated to every
   leaf, parser tests wrapped, canvas default OFF.
6. **M32** compile `PerfProbe` out unless `MARKOFF_PERF`; key on
   literal address; move the in-loop probe out of the loop.
7. **M34** correct or delete `Origin.h` (no production reader remains).
8. **M28 (labels only)** `LABELS` on every test; register
   `perf_load_bench`; fix `markoff-source/CLAUDE.md:27`.

Exit: focused tests each, then
`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`, plus a Release
build for the parser rows (M4's guarantee only exists under `NDEBUG`).

### Wave 2 — Lifetime, ownership, and wrong UI state (Markoff)

Crash class and consumer-visible lies. One commit each:

1. **M8** — `QPointer<MarkoffDocument>` + `destroyed` retire in
   `MarkdownView::setDocument`, then the five holders. Add the missing
   doc-destroyed regression test (the retired live-leaf test is gone with
   the leaf).
2. **M20** — `virtual Session *session()` / `setSession()` on
   `MarkdownView`, including the Source leaf.
3. **M21** — destroy the owned session in `Source::Editor` teardown;
   `destroyed` → null + emit in Styled.
4. **M9** — document-wide `canUndoD2()` / `canRedoD2()` (and a
   `canRedoForBlock`), plus virtual availability on `MarkdownView`.
5. **M11** — decide remove-vs-implement. Removing `Regex`/`WholeWords`/
   `Backwards` and adding `supports(FindFlag)` is the boring option and
   matches the code; fix the `FindController.h` comment either way.
6. **M23** — reuse `selectionInTrustworthyRegion` in the styled/source
   context path; leave row/col to canvas.
7. **M24** — lift the marker-restore helper into core and call it from
   both leaves; add a Source clipboard test.
8. **M25** — rewire `m_findAdapter` in `setDocument`.

Exit: ASAN run of the affected leaf tests; one consumer-shaped test that
deletes the document while attached.

### Wave 3 — Hot path (Markoff)

Order matters: build the index once, then use it.

1. **M16** running UTF-16→UTF-8 offset; 150 ms debounce in
   `FindController`.
2. **M17** remaining — bail when the caret's block is unchanged;
   throttle `handleMove`.
3. **M13** cached flat view invalidated on edit sequence + a block-offset
   vector for binary-search sep↔block; coalesce `onQtContentsChange`.
4. **M18** remaining — row index (`BlockId → index`) for the paste and
   removal loops.

Exit: before/after numbers on the probes named in the handoff; no budget
regression.

### Wave 4 — Parser structure (Markoff)

**M30 before M31**, because M31 rewrites the map builder that M30
validates. M30: continuation-byte validation, invalid sequence = 1 byte,
final `charIdx == text.size()` assertion, Latin-1 and truncated
fixtures; cover the `SourceSpan.cpp` twin. M31: pass the previous tree,
`resize` instead of zero-fill, incremental map; needs a parse benchmark
before and after.

### Wave 5 — Features that need a decision (Markoff)

1. **M7** — the largest visible gap, but it is a feature in a frozen
   leaf. Recommend opening one named exception task in three parts:
   `Theme::operator==` + `StyleApplier` by value; read `opts.theme` at
   every colour site; add theme to `computeBlockHash`. The third is not
   optional — without it the first two have no visible effect.
2. **M37** — card/embedded mode. Gated on the Wave 0 answers (done for
   M38; M39 keeps the emulation viable). Needs a documented flag and
   scroll ownership, not a second editor.
3. **M26 + M27 item 4** — packaging. Both are latent on this toolchain
   and one contradicts a recorded deliberate choice. Recommend deferring
   M26 and doing item 4 (forward-declare the engine types, make the
   engine link PRIVATE) only when a non-submodule or MSVC consumer
   exists.
4. **M12 reframed** — if Corbomite still measures growth, file a
   retention-policy task: current design deliberately keeps history and
   removed streams, and destructive collection is disabled. Measure with
   a multi-document soak before changing policy.

### Wave 6 — Graffodil (separate repo and process)

Order: **M6 → M5 → punch-list batch → M19 → M14 → M15.** The two P0s are
teardown/dangling work on the same seam; the leaf's 15 punch-list rows are
mostly S and should be one batch task; M19 is confined to
`SugiyamaLayout.cpp`; M14/M15 need a spec and plan under Graffodil's own
conventions before code.

### Wave 0b — collabtext pin publication (DONE 2026-10-19)

Closed within a day of being recorded. collabtext merged the engine line into
`master`, pushed the branch, every candidate tag, and a new annotated
`collabtext-integration-2026-10-19` = `4d0177cd6c3f8802ec031327d0f7d9f314a97e6f`;
`origin/master` now contains `e8a8e7d1`, so the prefix is recreatable.

Markoff keeps `e8a8e7d1` — verified ancestor of the integration commit, with
three lines of capability-manifest metadata as the only `libs/` delta, so the
A14/A15 acceptance record still applies and no re-acceptance is owed now. The
integration tag is the candidate for the next Core change that needs a fresh
acceptance run. See
[`../handoff/2026-10-19-collabtext-master-integration-pin.md`](../handoff/2026-10-19-collabtext-master-integration-pin.md).

M1 and M29 stay closed: both are in the pinned tree.

## Standstill rules that constrain this list

- `markoff-styled` and `markoff-source` are bug-fix-only. Waves 1–3 are
  bug-shaped and may proceed; M7 and M37 are feature-shaped and need an
  explicit exception recorded on this board before code.
- collabtext is an installed published package. A pin move is an acceptance
  record, not an in-tree patch.
- Physical collection and log pruning stay disabled. M12 cannot be
  "fixed" by enabling GC without changing that standing rule.
- Graffodil's own convention requires a bug report for fixes and a spec
  for new API; M14/M15 are the latter.

## Suggested capacity split

- Next session: Wave 0 reply + Wave 1 rows 1–4 (the P0/data-corruption
  set) and M33's test.
- Then Wave 1 rows 5–8, then Wave 2 in full.
- Graffodil M6/M5 can run in that repo in parallel; they share no code
  with Markoff.
- Wave 0b is done; the pin is published and unchanged.
