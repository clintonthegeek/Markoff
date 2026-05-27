# Session queue — 2026-05-10

> Items queued for sessions where interactive dogfood isn't available
> (remote/SSH/etc). Ordered by **descending execution difficulty** —
> a fresh agent should pick the topmost item that fits the available
> time/energy budget.
>
> **For a fresh agent landing here:** each item below names enough
> context to draft a spec/plan. Use `superpowers:brainstorming` to
> resolve open questions, then `superpowers:writing-plans` to write
> the plan, then execute task-by-task. Specs live in `docs/specs/`,
> plans in `docs/plans/`, both dated `YYYY-MM-DD-<slug>.md`. Add a
> back-reference here once the plan exists.
>
> **Current branch:** `master` (foundation rebuild merged from
> `exploration/new-foundation` at `3c7afa9`, 2026-05-25).
> **Tags held:** `v0.7.0-e2.5` (S1/S2/S3 cursor fixes, commits `463fc36..6c44a07`) and `v0.7.0-e2.6` (theme + zoom + color wiring, commits `9fff98d..ab6bf47`) — both pending interactive dogfood at user's local desktop. Dogfood checklists: e-arc-status.md TL;DR (E2.5) and `docs/specs/2026-05-17-theme-color-wiring-design.md` §"Definition of done" (E2.6 re-dogfood: Ctrl+Shift+D must visibly invert the editor).
>
> **2026-05-10 — Item #5 closed.** Code review of `463fc36..6c44a07`
> ran clean (1 MED, 2 LOW). MED + 1 LOW fixed inline in this session
> (comment expansion in `LiveEditBinding.cpp:165` documenting the
> safety of the cursorChanged emission during the model update;
> null-check ordering aligned across 5 delegates). The duplicated
> qtPos clamp at `LiveListModelBinding.cpp:405-406, 522-523` is
> exactly queue #2 concern #11 and is **folded into #2** — when #2's
> spec is written, treat that as one of the surface points to
> consolidate. 186/186 fast tests pass post-fixes.
>
> **2026-05-18 — Item #1 extended: theme color wiring (E2.6 extension).** The
> 2026-05-10 font/zoom implementation left all color properties reading from
> `palette.*` or hardcoded hex. Color wiring landed in 13 commits
> (`8ddaa93..ab6bf47`): `themeColorFor` Q_INVOKABLE proxy, `QML_FOREIGN` shim
> exposing `Theme.*` enum to QML, all 19 color sites from the spec's slot
> mapping table, 7 new QML integration tests. Zero `palette.*` references
> remain in `libs/markoff-live/qml/`. Tag `v0.7.0-e2.6` now covers this
> extension and is held until re-dogfood confirms Ctrl+Shift+D visibly inverts
> the editor. Spec: `docs/specs/2026-05-17-theme-color-wiring-design.md`;
> plan: `docs/plans/2026-05-17-theme-color-wiring.md`. 76/77 tests green
> (1 pre-existing `tst_live_render_setext_e2e` failure).
>
> **2026-05-10 — Item #1 implemented.** Spec
> `docs/specs/2026-05-10-e2.6-theme-zoom-design.md`; plan
> `docs/plans/2026-05-10-e2.6-theme-zoom.md`. Tag candidate
> `v0.7.0-e2.6`; held until interactive dogfood signs off
> (request: `docs/handoff/2026-05-10-e2.6-dogfood-request.md`).
> 190/190 fast tests green.
>
> **2026-05-10 — Item #3 implemented.** Spec
> `docs/specs/2026-05-10-qml-integration-test-harness-design.md`; plan
> `docs/plans/2026-05-10-qml-integration-test-harness.md`. New target
> `tst_live_render_qml_integration` runs offscreen; eight slots green
> (two QEXPECT_FAIL guards for queue #4 chop-\n). Full ctest still
> green.

---

## Discipline Log

> Append-only log of invariant violations encountered in passing.
> One line per smell. **No fix required in the same session** — the
> point is that the smell becomes visible to the next agent who
> reads the queue. Mechanism prescribed by `docs/INVARIANTS.md`
> invariant 8.
>
> Format: `- YYYY-MM-DD <file:line> — inv #N — <one phrase of context>`
>
> Closing an entry: prepend `~~` and append `→ fixed in <commit>` or
> `→ folded into queue #N`. Do not delete entries; they become the
> trail showing the seam settling over time.
>
> Bulk discovery is fine. If you find ten `Qt.callLater` sites in
> one read, log them as ten entries; the next refactor that touches
> the seam will use the count as evidence.

- ~~2026-05-11 `libs/markoff-live/src/LiveBlockModel.cpp:106` — inv #7 — `applyOps` kindOnlySwap → beginResetModel hammer.~~ → fixed in `d60f896` (tier-3). The kindOnlySwap detector + beginResetModel branch retired; block kind now flows through `delegateClass` bucketing per `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`. Duplicate of the entry below; consolidating procedurally during 2026-05-18 spring cleaning.
- ~~2026-05-11 `libs/markoff-live/src/LiveCursorState.cpp:419-440` — inv #7 — `tryResolvePending` sets `m_cursor` directly, bypassing `request()`.~~ → fixed in tier-4 (queue #2 concern #9). `LiveCursorState.cpp:455` now calls `request(newCursor)`; `validateVariant` is null-registry safe and doc-keyed. Inline comment at lines 422–427 documents the change. Struck procedurally during 2026-05-18 spring cleaning.
- 2026-05-13 `libs/markoff-live/src/BlockKindRegistry.cpp:Math` — inv #8 — `isBlockOnly` is explicit-false for Math despite Math having no `TextCaret` in `supportedCursorVariants`. Transitional asymmetry; will be removed when Math becomes text-bearing in its own spec.
- 2026-05-18 `libs/markoff-core/src/SourceTextDocumentBinding.cpp:onQtContentsChange` — inv #3 — separator-zone deletes (e.g. backspace at start of a block, removing the `\n\n` between two blocks) translate to a zero-length cursor edit in no-separator space, so the model retains both blocks while the QTextDocument has them merged; the subsequent `onD2DocumentChanged` then reverts the user's edit. Surfaced during the v10 cursor-round-trip B1 fallout fix (this session). Source-widget structural-edit parity needs a follow-on — either a sep-bearing `applyFlatEdit` variant or per-block edit routing. Not blocking the v10 test (which only loads + sets cursor); will surface as soon as the source widget is dogfooded for multi-block editing.
- 2026-05-18 `libs/markoff-source/src/Editor.cpp:hideFindBar` — inv #7 — `isVisible()` early-return acts as a re-entrance guard for the `closed → hideFindBar` signal cycle: `FindBar::deactivate()` hides + clears + emits `closed`, which synchronously re-enters `hideFindBar`; the `isVisible()` check (now false after `hide()`) makes the re-entered call a no-op. Not a flag-style guard (state is read from QWidget, not a `m_applyingX` member), but the *effect* is re-entrance handling. Cleaner future shape: split `FindBar` so `deactivate` becomes two methods — `clear()` (hide + clear, no signal) and `requestClose()` (emit signal for host listeners) — so `Editor::hideFindBar` can call `clear()` without signal blow-back. Documented in commit `0ec907d`'s message and inline comment.
- ~~2026-05-15 `libs/markoff-live/src/LiveCursorState.cpp:459-487` — inv #2 — `tryResolvePending` bypasses `request()`'s `validateVariant`~~ → fixed in tier-4 (queue #2 concern #9 closeout). `validateVariant` is now (a) null-registry safe and (b) queries the document instead of the model for the current kind, eliminating the "valid transient states during a structural cascade" rejection. `tryResolvePending` routes through `request()` like every other mutator. The transient was specifically the doc/model kind disagreement window during a cascade — model still has the pre-`changeKind` kind, doc already has the new one. Doc-keyed lookup resolves it.
- ~~2026-05-16 `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — inv #1 — `ListView.focus = true` puts the unified delegate's root Item in the focus chain (`d->hasActiveFocus()` returns true) but the TextEdit child does NOT gain `activeFocus` — keys delivered to the window go to the delegate root, which has no `Keys.onPressed` handler, so structural/nav routing is bypassed entirely. Production papers over this because every realistic interaction (click, programmatic `requestTextCaretAtRow`) goes through the chokepoint's `takeFocus` → `edit.forceActiveFocus()`. Two `tst_live_render_qml_integration` slots (`enter_at_paragraph_end_migrates_focus`, `delete_at_row_end_merges_next`) implicitly relied on auto-focus reaching the TextEdit and broke; both updated to use `requestCursor` (the explicit chokepoint path). The underlying gap — auto-focus doesn't deliver focus to a text-bearing descendant — is worth a follow-up: either the delegate sets `focus: true` on the TextEdit conditionally, the ListView delegate is wrapped in a FocusScope, or the production startup path explicitly seeds focus on row 0.~~ → fixed in dde6413 (tier-4b). Initial focus now routed through LiveView.qml's onCountChanged → cursorState.requestTextCaretAtRow(0, 0). The two test slots that previously migrated from auto-focus to requestCursor stay that way — explicit chokepoint routing is the new normal.
- ~~2026-05-11 `libs/markoff-live/src/LiveBlockModel.cpp:106` — inv #7 — `applyOps` now detects a Delete+Insert-at-same-row kind-change pattern and synthesises `beginResetModel`/`endResetModel`~~ → fixed in d60f896. The kindOnlySwap detector + beginResetModel branch retired in tier 3 (commit d60f896). Block kind now flows through `delegateClass` bucketing per spec `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`. Within-class kind transitions (paragraph↔heading, paragraph↔list-item, etc.) are dataChanged events on the same delegate; cross-class transitions still produce Delete+Insert.
- ~~prior `m_applyingSessionSelection` re-entrance guard in `LiveSelectionView`~~ → retired in tier 4c (`docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md` §4.3). Equality short-circuit on the resolved `(BlockAnchor, qtPos)` pair supersedes the guard. Invariant 7 cleared at this site.
- ~~2026-05-18 `libs/markoff-live/app/Main.qml:20` — inv #5 — `selectionView.setSession(ctxSession)` threw silent TypeError, aborting `Component.onCompleted` before themeToggle wiring; dogfood caught the broken Ctrl+Shift+D path.~~ → fixed in same session at `e8514eb`: `setSession` is now `Q_INVOKABLE` on `LiveListModelBinding`; `Main.qml` calls `modelBinding.setSession(ctxSession)`; `QmlIntegrationFixture` installs a `QtMessageHandler` failing any test on `TypeError|ReferenceError|SyntaxError|is not a function|is not a signal` qWarnings. Invariant #5 broadened in `docs/INVARIANTS.md` to apply project-wide.
- ~~2026-05-18 `libs/markoff-core/src/MarkoffDocument.cpp:1763` + chain — inv #3 — trailing-`\n` was never invariant; three 2026-05-04..05 commits each made local decisions, collectively producing a non-convention.~~ → fixed by spec `docs/specs/2026-05-18-b1-buffer-convention-design.md` + plan `docs/plans/2026-05-18-b1-buffer-convention.md` in commits `a4df009..0f7de6c`.
- 2026-05-19 `libs/markoff-live/src/LiveEditBinding.cpp:73` — inv #7 — `m_applyingTextUpdate` re-entrance guard + public `Q_INVOKABLE isApplyingTextUpdate()` accessor. Read by 2 production QML delegates (`CodeBlockDelegate.qml:65`, `UnifiedInlineTextDelegate.qml:202`) to suppress echo reactions during the apply window. Removal requires re-architecting the QTextDocument↔MarkoffDocument echo loop (signal-blocking, scoped guards on the inner QTextDocument, or per-edit-path routing that doesn't emit `contentsChange`). Frozen with explicit acceptance at the markoff-live freeze (D7 of `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`); redesign spec TBW.
- 2026-05-26 `libs/markoff-core/src/MarkoffDocument.cpp:buildD2FromBytes step 5` — inv #3 — `blockEditSequences.clear()` / `structuralEditSequence = 0` / `touchedSinceLoad.clear()` / `inlineCache->clear()` at the end of `buildD2FromBytes` are now redundant: both call sites (`resetContent`, `loadFromMarkdown`) call `wipeD2State()` first, which performs the same clears. Surfaced in qt-code-reviewer pass of `f48525d`. Safe to delete; left for now so a single follow-up touches both `buildD2FromBytes` doxygen (already done in `61c64a9`) and the redundant block together.
- 2026-05-19 `libs/markoff-live/qml/delegates/MathDelegate.qml:125` — inv #6 — `Qt.callLater(latexEdit.forceActiveFocus)` defers focus takeover after a `BlockInternalEdit` cursor variant change so QML bindings re-evaluate (latexEdit's visibility / focus chain). Single isolated site; removing requires extending the cursor chokepoint protocol with a synchronous focus-takeover signal (cross-delegate ripple work). Frozen with explicit acceptance at the markoff-live freeze (D8 of `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`); E5 (math live-mode parity) retires.
- ~~2026-05-21 `libs/markoff-live/qml/LiveView.qml:onPositionChanged` + `LiveCursorState::extend` — inv #4 + #5 — cross-block selection downward is broken in production (drag-down doesn't extend, Shift+Down doesn't extend, Backspace/Delete on selection deletes wrong region). Upward works. Surfaced 2026-05-21 by Corbomite dogfood…~~ → fixed in this session. Root cause: `applySelection()` in `UnifiedInlineTextDelegate.qml` + `CodeBlockDelegate.qml` programmatically writes `TextEdit.cursorPosition` (assignment + `moveCursorSelection`); each write fires `onCursorPositionChanged` → `cs.syncFromTextEdit(...)` and clobbers `m_cursor` back to the delegate that ran last in the `selectionChanged` dispatch. The up/down asymmetry is which delegate's `applySelection` ends with a non-empty `rangeForBlock` after the clobber. Fix: new `LiveCursorState::isApplyingSelection()` Q_INVOKABLE flag, held true during the synchronous window of every `selectionChanged` emit (wrapper `emitSelectionChanged()` replaces direct `Q_EMIT selectionChanged()` at all three sites: `setSelectionAnchor`, `clearSelectionAnchor`, `extend`). Both delegates' `onCursorPositionChanged` early-return when the flag is set. Falsifiable test `tst_live_render_cross_block_drag_selection_qml` pins both directions (`drag_down_active_end_lands_in_lower_block` + `drag_up_active_end_lands_in_upper_block`); confirmed failing on HEAD pre-fix, passing post-fix. Invariant 7 (re-entrance guards) consciously violated — see header doc + this entry for rationale. 215/215 fast tests pass post-fix.

- 2026-05-21 `libs/markoff-live/include/markoff/live/LiveCursorState.h:isApplyingSelection` — inv #7 — new re-entrance guard around the synchronous `selectionChanged` dispatch window. Logged on landing per invariant 7's "when adding: justify in the commit" rule. Justification: the QML delegate's `applySelection()` writes `TextEdit.cursorPosition` (assignment + `moveCursorSelection`) for visual purposes only; each write fires `onCursorPositionChanged` whose `syncFromTextEdit` is intended for user-driven cursor moves (typing, native TextEdit arrow keys). Cleanest architectural alternative would be to remove `syncFromTextEdit` entirely (route every cursor change through the chokepoint) — out of scope for this fix and would require auditing every `cursorPosition` write site. Frozen with explicit acceptance for the markoff-live freeze; redesign spec TBW alongside the existing `m_applyingTextUpdate` removal work (D7 of the freeze spec). The two guards solve adjacent problems (apply-text vs apply-selection) but the eventual unification probably belongs in one redesign.

- ~~2026-05-21 dogfood: cross-block copy/paste/cut puts only one block on the clipboard, regardless of how many were drag-selected.~~ → fixed same session. Root cause: a focused QML `TextEdit` natively handles `Ctrl+C`/`Ctrl+X`/`Ctrl+V`/`Ctrl+A` and operates on its own *within-block* selection, bypassing `LiveClipboardController` entirely. The `QAction`-backed shortcuts in `LiveActionController` never fire because `Keys.priority: Keys.BeforeItem` in `UnifiedInlineTextDelegate.qml` + `CodeBlockDelegate.qml` didn't include these chords in the explicit-handling list, so the event fell through to the TextEdit. Fix: both delegates' `Keys.onPressed` now intercept `Ctrl+C`/`X`/`V`/`A` (no Shift/Alt) and route to `liveBinding.clipboardController.{copy,cut,paste}` / `liveBinding.cursorState.selectAll()`, setting `event.accepted = true`. Falsifiable test `ctrl_c_after_three_block_drag_copies_all_three_blocks` sends a real `QTest::keyClick(window, Key_C, ControlModifier)` after a real-mouse drag — confirmed failing pre-fix (clipboard = single focused block's text), passing post-fix. Known follow-up: header kind sometimes doesn't survive structured paste (user dogfood); separate bug, not in this regression's scope.

- 2026-05-21 `libs/markoff-live/qml/LiveView.qml:hit()` — inv #4 — `clampedLocalX` clamps the click's local-x to the full delegate width, so a click in the trailing whitespace right-of-text (common for short paragraphs and headers) returns `qtPos = end-of-block` from `TextEdit.positionAt`. The selection anchor lands at end-of-block and any subsequent drag produces a degenerate range. Visible to the user as "selection only registers near the text glyphs." Surfaced 2026-05-21 while building `tst_live_render_cross_block_drag_selection_qml` — initial real-mouse tests clicked at delegate-centre and tripped this; tests now use `windowPointInTextAt(row, qtPos)` to anchor on actual glyphs. Production fix TBW: `hit()` should either clamp to the rendered text bbox or return a sentinel that the MouseArea can treat as "place caret at end-of-line" intentionally. Not blocking the regression closeout; logged here for the next pass at click-target UX.

- ~~2026-05-22 dogfood: ~20-block data loss on Delete + Enter at a paragraph adjacent to a table (kddw-evaluation-comparison.md, 307 lines → 167 → 146 blocks; saved file = 0 bytes after a separate save-bug intervened).~~ → fixed in spec `docs/specs/2026-05-22-cursor-authority-decision.md`. Root cause: `LiveCursorState::syncFromTextEdit` accepted cross-block cursor reports from non-focused delegates, treating them as user intent. After a structural edit shifts row indices, every visible delegate's text binding refreshes via `pushTextToDocument` → `setPlainText` → cursor reset → `cursorPositionChanged` → `syncFromTextEdit`. The last delegate to settle moved `m_cursor` to its anchor; combined with a stale `m_selectionAnchor` from the user's click, `hasSelection()` reported a phantom cross-block range that the subsequent Enter routed through `deleteSelection()` → `applyFlatEdit`, deleting everything in between. Fix landed via §5.1 (chokepoint same-block contract), §5.2 (begin/extend rewire to `request()`), §5.4 (anchor-clear on cross-block `request()`), §5.5 (`m_selectionExtended` flag → honest `hasSelection()`), §5.3 (delegate `onCursorPositionChanged` cleanup). Falsifiability via three regression tests: `syncFromTextEdit_rejects_crossblock`, `anchor_clears_on_crossblock_request`, `delete_then_enter_at_paragraph_before_table_preserves_block_count`. Also fixed adjacent save bug: `MainController::save()` now uses `serializeForSave()` instead of the deprecated `toMarkdownUtf8()`. Discovery instrumentation pass took two trace iterations to pin down the chain; full evidence in the spec §1 trace transcript.

- 2026-05-22 `libs/markoff-live/qml/delegates/TableDelegate.qml:cell-bindings` — inv #8 — column-count change (2→1 or 2→3 cols) via `MarkoffDocument::applyFlatEdit` SIGSEGVs during the QML Repeater rebuild. Crash address pattern (e.g. `0x0000006400650072` decodes to UTF-16 "r.e.d.") points at use-after-free of a string buffer interpreted as a pointer — likely a QQuickItem binding evaluating against a stale property during destroy. Surfaced 2026-05-22 building `tst_live_render_table_cell_edit::applyFlatEdit_preserves_cell_focus_and_re_renders_cells` (E4 C3); bounds-safe rewrites of cellText / cellCharRanges access / alignments lookup don't suppress it. C3's same-dimension test is what landed; structural-change falsifiability of `_restoreCellFocus` is dogfood-pending. Likely triage path: instrument cell delegate destruction; look for QString/QVariant captured by lambda or property that outlives the Item. Not blocking E4 phase D (cell-internal navigation), which doesn't exercise the rebuild path.

- 2026-05-22 `libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp:ctrl_shift_left/right_inside_block_returns_not_handled` — inv #8 — two slots fail on the current foundation-exploration HEAD pre-E4, not noted in CLAUDE.md baseline (which lists `tst_live_render_focus_chokepoint_invariant` + `tst_live_render_cursor_typing_invariant` as the known pre-existing two). Confirmed pre-existing via git-stash bisect during E4 A6: stashing the E4-only Table additions and re-running, both slots still fail. Likely fallout from recent audit-L4 (`0cbdf48`) Ctrl+Shift+Left/Right within-block word-extend work; the audit changed `LiveNavigationController` behavior and the expected "not-handled" return value drifted. Triage owner: whoever next pulls on `LiveNavigationController`. Not blocking E4.

- ~~2026-05-23 dogfood: persistent table-typing lag (~hundreds of ms per keystroke) despite the `5c67777` parser fast-path.~~ → fixed across four commits `1568972..634e813` on `exploration/new-foundation`. Root cause: `TableEditBinding::applyCellEdit` re-entered itself 20-40× per user keystroke. Each rebind of a non-focused cell's `cellText` (triggered by `parsedTable` identity change after `onD2Changed`) re-pushed `cellEdit.text`, fired `onTextChanged`, computed a no-op diff, called `applyCellEdit(byteOff, 0, "")`, which still triggered `flushPendingD2Changed → onD2Changed → ...` — cascade until the snapshot/short-circuit caught up. The dormant `m_applyingTextUpdate` re-entrance guard exposed by E4 C1 (`TableEditBinding.h:81-84`'s "value stays false until C2 wires the setter") had **never been wired** — a dropped E4 C2 task. Fix #1 short-circuits no-op `applyCellEdit` at the entry point. Fix #2 wires the guard. Fix #3 zeroes stale UTF-8 byte fields in `inlineSpansForCell`'s cell-frame projection (correctness; perf neutral). New bench `tst_live_render_table_typing_perf` is the regression net. Wall-clock per keystroke: 187 ms → 2.5 ms (small table) / 510 ms → 9.8 ms (large w/ inline syntax). Full diagnostic + the deeper "should the parser own structured table AST?" / "should we regenerate the grammar?" / "should incremental tree-sitter come back?" roadmap analysis in [`docs/handoff/2026-05-23-table-typing-perf-and-parser-roadmap.md`](handoff/2026-05-23-table-typing-perf-and-parser-roadmap.md). Open thread: ~75% of unchanged-cell setSpans calls still compare unequal even with Fix #3; some other SourceSpan field is varying (likely parent ranges in delimiter spans, or tree-sitter span ordering). Cost is harmless noise now (~3 ms/keystroke); investigate if a future session is already looking at binding-cascade artifacts.

- ~~2026-05-21 dogfood: Live-mode `Ctrl+F` showed correct match count but no visible highlights (Source mode worked).~~ → fixed in spec+plan `docs/specs/2026-05-21-live-find-highlighting-design.md` + `docs/plans/2026-05-21-live-find-highlighting.md`. Chain: `LiveFindAdapter` subscribes to `FindController::matchesChanged` + `currentMatchChanged`, diffs per-block, calls `LiveBlockModel::setFindSpans` (new `FindSpansRole`); `BlockRecord::findSpans` added (excluded from `operator==`); `InlineHighlighter` adds a find-pass after the inline-pass using the existing `Theme::SearchMatchBackground` + `SearchActiveMatchBackground` slots (already populated in `defaultLight()`). `UnifiedInlineTextDelegate.qml` passes `model.findSpans` to `InlineHighlighterAttached`. Two falsifiability proofs committed and reverted (adapter `rebuildAndPushSpans` stub + InlineHighlighter find-pass stub) — both made the unit + integration tests fail as expected. New `tst_live_find_adapter` binary (3 slots: populate / current-flip / detach-clears) + 3 slots in `tst_live_render_qml_integration`. CodeBlockDelegate find-highlight is a deferred follow-up (uses its own code-token highlighter, not `InlineHighlighter`). MathDelegate: matches count + navigate but no visible paint (per spec). 221/221 tests pass.

- 2026-05-21 `libs/markoff-live/qml/delegates/` — meta — every QML TextEdit in this library now has a documented intercept architecture. Before adding a new Ctrl-modifier chord or mutating-key handler in any delegate, route it through `qml/delegates/KeyDispatch.js` rather than duplicating logic. Audit doc: `docs/specs/2026-05-21-textedit-interface-audit.md`. Implementation plan: `docs/plans/2026-05-21-textedit-cleanup.md`. Three delegates currently adopt the dispatcher: UnifiedInlineTextDelegate (paragraph/heading/blockquote/list-item), CodeBlockDelegate, MathDelegate (latexEdit).

- ~~Audit L1 (cross-block-selection mutating-key bug)~~ → fixed. `LiveCursorState::deleteSelectionRange` now positions the cursor at the collapse point (first-corner block, first-corner qtPos) before clearing the selection anchor. `KeyDispatch.collapseSelectionIfMutating` intercepts Backspace/Delete/Return/printable on a non-empty cross-block selection and calls deleteSelection + flush; for Return and printable chars the dispatcher also positions the focused TextEdit's `cursorPosition` so TextEdit's residual handler acts at the collapse point. Falsifiable test `tst_live_render_cross_block_mutating_key` (4 slots). Falsifiability stub-then-revert pair committed.

- ~~Audit L5 + L6 (MathDelegate latexEdit Ctrl+Z / clipboard chords bypassing document layer)~~ → fixed by MathDelegate's latexEdit adopting `KeyDispatch.tryDispatchCtrlChord`. Explicit decision: math-source editing routes through the same document-layer clipboard / undo machinery as surrounding paragraphs.

- ~~Audit L8 (LiveActionController QActions with setShortcut() but no QML Shortcut element binding)~~ → fixed. `LiveView.qml` now binds window-level Shortcuts for Bold/Italic/Strike/InlineCode/Link/Heading1..6/Save in addition to the pre-existing zoom/dark-toggle bindings. Ctrl+0 deliberately stays bound to zoom-reset; the paragraph-demote action is reached via host menus / context menus.

- ~~Audit L7 (IME composition under D2)~~ → closed by `docs/specs/2026-05-21-audit-L7-ime-composition.md`. The audit's primary concern was "untested entirely" — that's now resolved with `tst_live_render_ime_composition_qml` (5 slots: commit-after-preedit, preedit-replace-commit, cancelled-composition, commit-into-non-empty, lifecycle probe). All slots pass against the existing implementation, confirming the wholesale-replace path produces correct final content for standard scenarios. Two known shape issues documented for future work: (a) coordinate-granularity loss (commit recorded as single d2ApplyBufferEdit, not per-character ops); (b) D5 collision risk (concurrent remote insert at the same block during local composition would be clobbered by the wholesale replace). Neither is a v1 blocker; both belong in the D5 spec when D5 lands. All 5 audit items now closed.

- ~~Audit L3 (drag-drop of external text)~~ → closed by `docs/specs/2026-05-21-audit-L3-drag-and-drop-text.md`. `LiveView.qml` now has a top-level `DropArea` (z above the MouseArea) that intercepts text drops before TextEdit's native handler sees them. Drop-at-point semantics: hit-test the drop position, move cursor there (collapsing any cross-block selection), insert via new `LiveClipboardController::pasteText`. Scope is text/plain only; structured paste from drag-drop (our own `application/x-markoff-blocks` MIME) deferred until dogfooded. `tst_live_render_drop_text_qml` (3 slots, all green). Bonus: factored `resolveSelectionByteRange` helper out of `pasteFrom` so `pasteText` shares the anchor-resolution path.

- ~~Audit L2 (middle-click PRIMARY-selection paste)~~ → closed by `docs/specs/2026-05-21-audit-L2-middle-click-primary-paste.md`. `LiveClipboardController::pastePrimary()` reads from `QClipboard::Selection` and routes through the same structured/flat paste machinery as `paste()`. `LiveView.qml`'s MouseArea now accepts `Qt.MiddleButton`: on middle-press, hit-test → move cursor to click → `pastePrimary()`. Paste-at-click, not paste-at-focus. `tst_live_render_middle_click_paste_qml` (3 slots, 2 skipped on offscreen QPA where `supportsSelection()` is false; the no-op safety test runs everywhere). Real-desktop dogfood pass needed to confirm the active-PRIMARY path end-to-end.

- ~~Audit L9 (Ctrl+Backspace/Delete at block boundary)~~ → closed by `docs/specs/2026-05-21-audit-L9-ctrl-backspace-boundary.md`. Decision: Ctrl modifier is ignored at the boundary (qtPos=0 for Backspace, qtPos=length for Delete); behaviour collapses to plain block-merge. Matches Qt's own document-boundary handling. No production code change — already the behaviour today; ratified explicitly. Pinned by `tst_live_render_ctrl_backspace_boundary_qml` (3 slots: in-block word-delete works, boundary Backspace merges, boundary Delete merges).

- ~~Audit L4 (Ctrl+Shift+Left/Right within-block word-extend)~~ → closed by `docs/specs/2026-05-21-audit-L4-ctrl-shift-word-extend.md`. Decision: `LiveNavigationController::tryHandle` now claims Ctrl+Shift+Left/Right within a block, computes the word boundary via `QTextBoundaryFinder` (matching Qt's WordLeft/WordRight semantics), and routes through `cursorState->begin (lazy) + extend` so the document-layer anchor is canonical. Resolves the divergence where TextEdit drew a within-block selection while `m_selectionAnchor` stayed empty, breaking Ctrl+C. Bonus fix: the cross-block fallthrough also needed the lazy-begin (anchor wasn't being set for at-qtPos=0 case either). Pinned by `tst_live_render_ctrl_shift_word_extend_qml` (5 slots).

- 2026-05-26 `libs/markoff-styled/src/StyleApplier.cpp:m_applyingFormats` — inv #7 — re-entry guard on format application — defended by QSignalBlocker + beginEditBlock/endEditBlock, but Qt format-change signals are genuinely re-entrant via observers; alternative (assume no observer) is fragile (per spec §7).

- 2026-05-26 `libs/markoff-styled/src/Editor.cpp:m_applyingFontScale` — inv #7 — declared but never actually written/read in Editor.cpp. Possibly dead. Audit when font-scale wiring is exercised by a UI feature (Ctrl+/-).

- 2026-05-26 `libs/markoff-styled/tests/tst_styled_inline_formats.cpp:inline_code_uses_monospace` — weak-assertion pattern — `|| fontFamilies().size() > 0` branch is tautological; tighten to `QVERIFY(cf.fontFixedPitch())` in v0.1.

- 2026-05-26 `libs/markoff-styled/src/LinkInteraction.cpp:handleMove` — cursor-shape setter fires `setCursor()` on every MouseMove within the same link — idempotent in Qt but wasted call; cache shape in v0.1.

- ~~2026-05-26 `libs/markoff-styled/tests/tst_styled_d2_integration.cpp:remote_edit_replays_text_and_restyles` — QEXPECT_FAIL: `applyFlatEdit` doesn't re-infer blockKind on prefix-changing edits — markoff-live has `KindTransition::inferBlockKind` in its `onD2Changed`; markoff-styled has no equivalent. Track via future micro-spec; fix options documented in `libs/markoff-styled/CLAUDE.md`.~~ → fixed in v0.1 Task 2 (`Cmd::changeKind` prefix-rule inference added to `StyleApplier::onD2DocumentChanged`; `QEXPECT_FAIL` removed).

- 2026-05-27 `libs/markoff-styled/src/StyleApplier.cpp` `applyPendingKindChanges`, invariant 6 — QTimer::singleShot(0) defers Cmd::changeKind out of d2DocumentChanged slot; the smell is justified per spec §4.4 (avoids synchronous CRDT re-entry during cascade). Mirrors markoff-live::LiveListModelBinding pattern.

- 2026-05-27 `libs/markoff-styled/src/StyleApplier.cpp` `applyFormats`, invariant 6 — QTimer::singleShot(0) defers scroll-position restore until after Qt's post-endEditBlock layout signals settle; spec §5.2's synchronous restore was wrong (overridden by Qt's ensureCursorVisible logic).

- ~~2026-05-18 `libs/markoff-core/src/SourceTextDocumentBinding.cpp:311-317` — inv #3 — separator-zone deletes (backspace at block start, cross-block selection delete) translated to a zero-length edit in no-separator space; model retained both blocks while QTextDocument had them merged; the subsequent `onD2DocumentChanged` reverted the user's edit.~~ → CLOSED 2026-05-27 by the robustness spec + RT3 plan. Sep-view dispatch via `Markoff::Detail::findBlockAtSepByte` routes separator-spanning deletes to direct D2 merge primitives. Guards: `tst_binding_forward::backspace_over_separator_merges_blocks` and `::cross_block_selection_delete_merges_content`. Reference spec `docs/specs/2026-05-27-markoff-core-binding-robustness-design.md`.

- 2026-05-27 `libs/markoff-core/src/SourceTextDocumentBinding.cpp:sepViewToNoSepByteForEdit` — inv #8 — calls `iterateBlocks()` twice: once directly and once inside `findBlockAtSepByte`. Inefficiency only; Tier-3 path, not hot. Optimize if profiling flags it.

- 2026-05-27 `libs/markoff-core/src/SourceTextDocumentBinding.cpp` and `applyFlatEdit` cross-block branch — inv #8 — a delete spanning ALL content of ≥2 blocks can leave one empty surviving block (both `applyFlatEdit`'s cross-block branch and the binding's Tier-2 path). Whole-document-select-delete edge case; renders as an empty paragraph. Harmless in normal use; revisit if dogfood surfaces it.

---

## #1 — E2.6: theme wire-up + zoom + color wiring ✅ COMPLETE 2026-05-18 (tag `v0.7.0-e2.6`)

**Effort:** ~1 week. **Status:** complete and tagged `v0.7.0-e2.6` at `e8514eb`. Dogfood confirmed Ctrl+Shift+D inverts colours, Ctrl+wheel zooms, keyboard zoom works.

The `Markoff::Theme` infrastructure exists and ships
`defaultLight()`/`defaultDark()`, per-slot fonts/colours, and a
`fontSizeMultiplier(Slot)` getter — but every QML delegate hardcodes
its `font.pixelSize`/`font.family`/heading-level switch. The theme
object is in practice a decoration over `InlineHighlighter` only. No
zoom infrastructure exists either.

**Scope (from `docs/handoff/2026-05-09-post-e2-scope.md` §2.E2.6):**

- Route `font.family`/`pixelSize`/`bold`/`italic` for every text-bearing
  delegate through `Markoff::Theme`. Concrete files:
  `qml/delegates/{Paragraph,Heading,Blockquote,CodeBlock,ListItem,Math}Delegate.qml`.
- Replace `HeadingDelegate.qml`'s literal switch (28/24/20/18/16/14)
  with theme-driven `font(Heading) * fontSizeMultiplier(HeadingN)`.
- Add `fontScale` Q_PROPERTY on `LiveListModelBinding` (or a sibling
  controller). All delegate font-size reads multiply by it.
- Wire `Ctrl+=` / `Ctrl+-` / `Ctrl+0` (reset) / `Ctrl+wheel` zoom into
  `LiveActionController`'s QAction set.
- Light/dark toggle action.

**Reading order for a fresh agent:**

1. `docs/handoff/2026-05-09-post-e2-scope.md` §1.2, §1.3, §2.E2.6 —
   audit + scope statement.
2. `libs/markoff-core/include/markoff/core/Theme.h` — what's already
   shipped.
3. `libs/markoff-live/qml/delegates/*.qml` — every delegate that needs
   updating; survey current font usage before designing.
4. `libs/markoff-live/src/InlineHighlighter.cpp` — already consumes
   `Theme::charFormat`; reference for the consumption pattern.
5. `libs/markoff-live/src/LiveActionController.cpp` — where zoom QActions
   should live (alongside Cut/Copy/Paste/Bold/etc).

**Open design questions to brainstorm:**

- Where does `fontScale` live? `LiveListModelBinding` (per-binding) or a
  new `LiveZoomController`? Per-binding matches `Capabilities` flag
  subtractability (E-arc framing §5).
- Is `fontScale` a multiplier on theme sizes, or does it feed into
  `Theme::fontSizeMultiplier` somehow?
- Light/dark toggle: a per-binding QAction or an app-level setting? How
  does the test app decide which to ship at launch?
- Does theme switching invalidate any caches in `InlineHighlighter`?

**Definition of done:** Tag `v0.7.0-e2.6`. All delegates draw fonts
through `Theme`; Ctrl+= and Ctrl+- visibly resize text; light/dark
toggle works; an interactive dogfood pass signs off.

---

## #2 — Cursor architecture cleanup

> **2026-05-16 — Tier 4c implemented.** Last queue #2 concern
> closed: **#10** (`LiveSelectionView` / `LiveCursorState` dual
> canonical stores). `LiveSelectionView` is now a stateless Q_OBJECT
> facade preserving the QML-exposed API; `m_selectionAnchor` (new)
> + `m_cursor` (existing) on `LiveCursorState` are the sole canonical
> store, keyed by `BlockAnchor`. Session bridge (`syncSelectionToSession`,
> `onSessionPrimarySelectionChanged`) moved with the state. The
> `m_applyingSessionSelection` re-entrance guard is retired via the
> equality short-circuit on the resolved (BlockAnchor, qtPos) pair
> (invariant 7 cleared at this site). Spec
> `docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md`;
> plan `docs/plans/2026-05-16-tier-4c-selection-cursor-unification.md`.
> Two falsifiability proofs in history (stub-then-revert pairs). New
> invariant binary `tst_live_render_selection_cursor_unification`
> with 7 slots covering click-then-shift-click, cross-block shift-arrow,
> double-click, clear-via-arrow, session round-trip no-echo,
> selection-survives-structural-edit-above, and orphaned-anchor cleanup.
> Queue #2 now has no remaining concerns.
>
> **2026-05-16 — Tier 4b implemented.** Concerns **#3** (three
> overlapping `requestTextCaretAt*` APIs — two unused variants
> `requestTextCaretAtNewRow` and `requestTextCaretAtAnchor` deleted;
> `requestTextCaretAtRow` retained as row-keyed convenience over
> `establishFocus`) and **#4** (the second pending slot `m_pendingRow`
> and its resolvers / slot handlers / signal connections deleted)
> both closed. Spec
> `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md`;
> plan `docs/plans/2026-05-16-tier-4b-pending-slot-consolidation.md`.
> Falsifiability proofs A (m_pendingRow inert; full suite still
> green) and B (initial-focus seed disabled; new
> `initial_focus_lands_on_textedit_not_delegate_root` slot fails) both
> committed and reverted in-history. Initial-focus seam closed via
> `LiveView.qml`'s `onCountChanged` calling
> `cursorState.requestTextCaretAtRow(0, 0)` once on first model
> population (spec §4.4 prescribed `Component.onCompleted`; the
> implementation moved to `onCountChanged` because `onCompleted` fires
> before the debounced model load completes — see commit `dde6413`
> message). Remaining concern: **#10**
> (`LiveSelectionView` / `LiveCursorState` dual canonical stores) →
> tier 4c.
>
> **2026-05-16 — Tier 4 (partial) implemented.** Concerns **#5**
> (cursor API poking doc flush — now routed through
> `LiveListModelBinding::flushPendingDocumentChanges`), **#9**
> (`tryResolvePending` unspecified transients — `validateVariant` is
> now doc-aware and null-safe; the bypass is gone), and **#12**
> (typed `currentTextCaret()` accessor — replaces the
> `cursor()` + `std::get_if<TextCaret>` pattern at the two call
> sites in `LiveListModelBinding`) all closed in tier-4 commits.
> Remaining concerns: **#3** (three overlapping `requestTextCaretAt*`
> APIs), **#4** (single-valued `m_pendingRow` plus the second
> `m_pendingFocus` slot — two pending mechanisms now coexist), **#10**
> (`LiveSelectionView` / `LiveCursorState` dual canonical stores).
> Those three are substantive design refactors and deserve their
> own spec-and-plan pass.
>
> **2026-05-15/16 — Tier 3 (kind-transition delegate architecture) implemented.** Spec
> `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`;
> plan `docs/plans/2026-05-15-tier-3-kind-transition-delegate-architecture.md`.
> Addressed the `kindOnlySwap`/`beginResetModel` workaround (discipline-log entry
> 2026-05-11, now closed at d60f896). Cursor concern **#9** (`tryResolvePending`
> unspecified transients) remains open — logged at discipline-log line 65 as the
> explicit next concern for a tier-4 cursor pass.
>
> **2026-05-15 — Tier 2 implemented.** Spec
> `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md`;
> plan `docs/plans/2026-05-15-tier-2-cursor-typing-authority.md`.
> Concerns **#1** (docstring honesty), **#2** (cachedQtPos rename),
> **#6** (per-keystroke invariant test — 5 slots on
> `LiveRealisticInputHarness`) all closed. Falsifiability proof
> committed + reverted per invariant 4. Concern **#9** deferred to
> tier 3 with discipline-log entry naming the unspecified
> transients in `tryResolvePending`. Remaining concerns: #3, #4, #5,
> #10, #12.
>
> **2026-05-11 — Tier 1 (focus-chokepoint) implemented.** Concerns
> **#1 (partial — structural side)**, **#7**, **#8**, **#11** are
> resolved. See `docs/specs/2026-05-11-focus-chokepoint-design.md`,
> `docs/plans/2026-05-11-focus-chokepoint.md`, and the dogfood
> request at `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`.
> Tag `v0.8.0-focus-chokepoint` held pending interactive sign-off.
> Remaining concerns (#1 full, #2, #3, #4, #5, #6 full, #9, #10,
> #12) tier into tier 2/3/4 per spec §10; no spec yet, gated on
> tier-1 dogfood. Bug C (click-path chokepoint bypass) surfaced
> only during the Task 16 revert and is also fixed; falsifiability
> proof now covers both sides of the seam (`establishFocus` stub
> `20dcaee` + `takeFocus` stub `2d609ba`).
>
> **2026-05-11 (dogfood re-pass):** D-fc-1 (HR focus loss) and
> D-fc-2 (new-heading impermeable to arrow keys) both fixed in
> commit `4fb711f`. The deeper root cause for D-fc-2 was an
> architectural one: `DelegateChooser` does not swap delegates on
> `dataChanged`, and the pool reuses the wrong template under
> remove+insert at the same row. `LiveBlockModel::applyOps` now
> detects the kind-change pattern and emits a `beginResetModel`/
> `endResetModel` for it. Chokepoint invariant suite 21/21.
>
> **2026-05-11 (D-fc-3):** Follow-on dogfood found arrows still
> didn't move past *already-existing* HRs. Root cause was the
> chokepoint always staging a `TextCaret` cursor variant —
> invalid for non-text-bearing kinds (HR, Image). Clicking on or
> near an HR left the cursor in an invalid TextCaret state that
> the HR delegate's `isSelected` guard rejected, breaking arrow
> navigation entirely. Fixed in commit `9b30d75` by making the
> chokepoint *variant-aware* via the existing
> `BlockKindRegistry.supportedCursorVariants` — the generalisable
> rule is "the chokepoint never stages a variant the target
> can't honour." `focusedAnchorRow()` extended to all variants
> for the same reason. Image and Math gain the same affordance
> for free. Chokepoint suite now 28 tests (was 21), with the new
> tests pinning the rule.
>
> **2026-05-13 — Block-only kinds spec + plan landed.**
> Spec: `docs/specs/2026-05-13-block-only-kinds-design.md`
> Plan: `docs/plans/2026-05-13-block-only-kinds.md`
> Work: 14-task chain on `exploration/new-foundation`. Delivers:
> - `BlockKindDescriptor::isBlockOnly` explicit flag (HR + Image = true, Math = false)
> - `BlockKindRegistry::isBlockOnly()` predicate — single decision point
> - `BlockOnlyDelegateBase.qml` shared base (HR + Image inherit; Math unchanged)
> - Navigation land-and-step (R-arrow-into/out): HR + Image are cursor stops
> - Merge fence (R-backspace/delete-adjacent): fixes D-fc-4 orphaned cursor
> - R-delete/enter/type/tripleclick: full block-only UX complete
> - 16 new invariant tests (8 HR + 8 Image), falsifiability stub in history

**Effort:** ~3 days. **Status:** critique captured (verbally during
S1/S2/S3 pass), no spec, no plan.

The S1/S2/S3 fix surfaced 12 architectural concerns in
`LiveCursorState`. The Tier 2 attempt to fix them all at once
overreached and caused multiple production regressions (typing reverses
chars; Shift+Enter inert; arrow up skips paragraphs; cursor lost on
Enter; column preservation broken). The partial revert in `6c44a07`
left these concerns standing.

**The 12 concerns** (from the critique notes; full text was in the
pre-clear conversation, but the headlines + repro live below; the agent
should read the relevant code to recover detail):

1. `LiveCursorState`'s docstring claims "single canonical cursor value"
   but `m_cursor` was never updated during typing pre-Tier-2; Tier 2
   added the sync but the architecture still assumes pre-Tier-2 in
   places.
2. `TextCaret::cachedByteOffset` is named for bytes but receives `qtPos`
   (UTF-16 code units) at every assignment site. Silent footgun for
   non-ASCII content. `Coordinates::qtPosToByte` exists in
   `LiveEditBinding.cpp:151` but isn't applied to TextCaret writes.
3. Three `requestTextCaretAt*` APIs with overlapping resolution
   semantics:
   - `requestTextCaretAtRow(row, qtPos)` — resolves immediately if row
     exists, else pending on `rowsInserted`.
   - `requestTextCaretAtNewRow(row, qtPos)` — pure-pending row-keyed.
   - `requestTextCaretAtAnchor(anchor, qtPos)` — pure-pending
     anchor-keyed.
   The choice depends on understanding the diff that's about to fire,
   with docstring tripwires ("do not use after d2ApplyBufferEdit that
   changes content") that aren't enforced.
4. The pending-request slot is single-valued (`std::optional<PendingRow>`).
   Latest-request-wins by convention, not by type.
5. `flushPendingD2Changed` inside `requestTextCaretAtRow`
   (`LiveCursorState.cpp:115`) is the cursor API poking the document's
   internal flush plumbing — leaky abstraction.
6. No invariant test asserts that `cursorState.focusedQtPos` matches the
   focused `TextEdit`'s `cursorPosition` after every keystroke.
7. `Component.onCompleted`'s match check is captured at construction
   time; if the structural signal resolves AFTER, the new delegate never
   focuses (suspected source of "cursor gone after Enter").
8. `Connections { onCursorChanged }` doesn't `forceActiveFocus` —
   non-VisualLineHint cross-block requests set `edit.cursorPosition` but
   don't migrate Qt focus. Same suspicion as #7.
9. `validateVariant` reads model state on read, not on write. A cursor
   with a wrong variant for the current kind is silently swallowed.
10. Selection state (`LiveSelectionView`) and cursor state
    (`LiveCursorState`) are independent canonical stores with their own
    sync paths; they overlap in concept.
11. The "kind transition return point clamps to `rec.text.size()`"
    pattern (just landed in `6c44a07`) is correct surgically but the
    same clamp is needed everywhere TextCaret is consumed in a
    delegate. Currently scattered between `focusEditAt`'s
    `if (qtPos <= edit.length)` guard and the request-site clamp.
12. `m_cursor` is exposed by-value (`Cursor cursor() const`) — callers
    in `LiveListModelBinding.cpp` had to `std::get_if<TextCaret>(&cur)`
    against a local copy. A typed `currentTextCaret() -> std::optional<TextCaret>`
    would be clearer.

**Reading order for a fresh agent:**

1. `libs/markoff-live/include/markoff/live/LiveCursorState.h` — the
   contract (read first, includes the API surface).
2. `libs/markoff-live/src/LiveCursorState.cpp` — implementation.
3. `libs/markoff-live/src/LiveListModelBinding.cpp` — kind-transition
   call sites and the `cursor()` consumers.
4. `libs/markoff-live/src/LiveEditBinding.cpp` — `syncFromTextEdit`
   producer.
5. `libs/markoff-live/qml/delegates/{Paragraph,Heading,Blockquote,CodeBlock,ListItem}Delegate.qml`
   — the QML consumer pattern (`Connections { onCursorChanged }` and
   `Component.onCompleted` both consume `focusedAnchorRow`/`focusedQtPos`).

**Open design questions to brainstorm:**

- Is concern #2 (bytes vs qtPos) a rename-only fix or does it require
  semantic migration (some call sites might actually want bytes)?
- Concerns #3 + #4 — is consolidating the three request APIs to one
  with a `ResolveMode` enum strictly an improvement, or does the
  variation reflect real differences worth preserving?
- Concern #8 — should `Connections.onCursorChanged` call
  `forceActiveFocus` on the matching delegate? What guards against
  stealing focus during transient cursorChanged emissions?
- Are concerns #7/#8 actually bugs in production, or theoretical? An
  interactive dogfood probe (queue item #4 might surface evidence)
  should inform priority within this plan.

**Definition of done:** All 12 items either fixed or deliberately
deferred with rationale in the spec. Invariant test added (#6).
Existing 186/186 tests still pass. No new functional regressions in an
interactive dogfood pass.

---

## #3 — QML integration-test harness ✅ IMPLEMENTED 2026-05-10

**Effort:** ~1 day. **Status:** implemented in
`tst_live_render_qml_integration` (commit chain ending Task 11).

Eight slots cover the queue-listed regression class. The harness loads
production Main.qml via the markoff-live-app-internal STATIC library
and drives input through LiveRealisticInputHarness (now wired up for
the first time since it was authored). Three-layer assertion
convention enforced: every edit asserts on buffer/model/delegate so
failures pinpoint the broken pipeline link.

Follow-ups:
- queue.md #4 (chop-\n): surfaced and guarded by
  `shift_enter_creates_visible_newline`'s two QEXPECT_FAIL markers.
- Theme QML method registration in test env may limit pixelSize
  assertions in `ctrl_wheel_zooms_font_scale` (graceful degradation).

---

## ~~#4 — Chop-trailing-`\n` investigation + fix~~ ✅ COMPLETE 2026-05-18 (B1 spec)

**Status:** complete. Landed in commits `a4df009` (markoff-core: B1 buffer convention) + `0f7de6c` (markoff-live: retire onD2Changed chop). Spec: `docs/specs/2026-05-18-b1-buffer-convention-design.md`. Plan: `docs/plans/2026-05-18-b1-buffer-convention.md`. Closes the long-standing soft-break regression in `shift_enter_creates_visible_newline`.

**Effort:** ~1 day (refactor + fan-out fixes).
**Status:** 2026-05-16 — investigation complete; chop's premise *is*
wrong but the fix has substantial blast radius. Two safe-now changes
landed (matchesSetextShape trailing-`\n` tolerance + clarifying
comment on the chop pointing at this entry). The full fix is a
buffer-convention refactor, written up below as the plan.

### Investigation findings (2026-05-16)

`materializeBlocksFromParsedDoc` in `libs/markoff-core/src/MarkoffDocument.cpp:1690`
stores `bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart)` as the
CRDT buffer for each block. Tree-sitter's byte range **does include**
a trailing `\n` when the source has one (e.g. `"first\n\nsecond"` →
block 0 buffer = `"first\n"`); it doesn't when the source doesn't
(`"Heading"` standalone → buffer `"Heading"`). So the load convention
is "buffer is whatever bytes tree-sitter delimited, including or
excluding a trailing `\n`".

The chop normalises that to "no trailing `\n` ever" at the display
edge. **But** the same storage representation collides with
user-typed soft breaks: after Shift+Enter at end of `"Heading"`,
`insertSoftBreak` produces buffer `"Heading\n"` — indistinguishable
from a loaded block whose source line ended with `\n`. The chop
strips both. Loaded blocks: correct. Soft-broken blocks: bug
(cursor can't park at pos 8, soft break invisible).

`isBlockTouched()` exists but doesn't help disambiguate — any edit
flips it (even unrelated character insertions), and a touched block
might still have a load-time `\n` mixed with user edits elsewhere.

The four downstream consumer categories of `BlockRecord.text`:

  * **Kind-transition inference** (`inferBlockKind`, `countLeadingHashes`,
    `matchesSetextShape`, `kPromoteMarker`): a few are sensitive to a
    trailing `\n`. `matchesSetextShape` is the most fragile — it
    `lastIndexOf('\n')` and treats the tail as the underline; with a
    trailing `\n` the tail is empty and the match fails. Fixed
    defensively in 2026-05-16 commit (trim trailing newlines first).
  * **Navigation length** (`LiveNavigationController`, `LiveStructuralKeyHandler`):
    use `text.length()` as "end of block". A chopped text gives a
    shorter length than the buffer — consistent with the delegate's
    `TextEdit.length` (which is also the chopped text), so today
    these are internally consistent.
  * **Clipboard / selection** (`LiveSelectionView`, `LiveClipboardController`):
    serialize chopped text to the clipboard. Correct for delimiter `\n`
    (user doesn't want it); wrong for soft-break `\n` (user expects
    the line break preserved).
  * **QML delegates** (`UnifiedInlineTextDelegate`, `CodeBlockDelegate`,
    `MathDelegate`): bind `TextEdit.text` to `model.text`. The chopped
    value is what renders. Soft breaks therefore can't be visible at
    all under the current convention.

### Attempted fix and its blast radius (2026-05-16)

Tried Option B-pure (strip trailing `\n` in
`materializeBlocksFromParsedDoc`, remove the chop in
`LiveListModelBinding`): **12 test binaries failed**, including two
subprocess aborts (`tst_markoff_doc_apply_structured_paste`,
`tst_d4_apply_flat_edit`). The convention "buffer carries its
delimiter `\n`" is load-bearing for `applyFlatEdit`'s text-as-flat
reconstruction, the merge cmds (`backspaceMerge`, `deleteMerge`)
both have explicit `stripsTrailingNewlineAtBoundary` tests, and
several round-trip serializers assume it.

Option A (kind-aware chop) doesn't separate the two cases either —
a paragraph that was Shift+Entered at end has the same buffer shape
as a paragraph that was loaded from `"text\n"`. Per-block "load-time
delimiter" tracking is the only way to distinguish, and that's a
substantial new piece of state.

Option C (no chop + audit) has the same blast radius as Option B
because the downstream consumers expect chopped text.

### Plan for the proper fix

1. **Pick a single buffer invariant.** Either
   * **B1**: buffer is *always* terminator-free; load strips, the
     serializer reconstructs the inter-block separator from
     `interBlockSeparator()` for every block (not just touched ones);
     `applyFlatEdit` and the merge cmds and `blockLoadTimeBytes` all
     stop carrying the trailing `\n`. — or —
   * **A1**: buffer always carries a single trailing `\n` (loaded
     blocks already do; load adds `\n` for sources that don't end
     with one; runtime adds `\n` on `d2InsertBlock` for new blocks).
     Soft-break inserts `\n` *before* the terminator. The chop
     becomes a strip of exactly the terminator; the soft-break `\n`
     survives because it's not the *last* `\n`.

   B1 is cleaner conceptually but touches more files. A1 is more
   surgical but introduces a buffer-storage detail callers must
   honour.

2. **Audit and update** the consumer surface enumerated above. Each
   consumer either gains an explicit "is the trailing `\n` part of
   content?" check or accepts the new invariant verbatim.

3. **Regression-test the soft-break case end-to-end.** The existing
   `shift_enter_creates_visible_newline` test already has two
   `QEXPECT_FAIL` markers ready to remove. Add a peer test that
   types Shift+Enter+`-` and asserts the buffer is `"Heading\n-"`
   not `"Heading-\n"` (the original symptom queue.md #4 was
   chasing).

4. **Round-trip verification.** Load a setext-heavy document, no
   edits, save, diff against source — must be byte-identical for
   untouched blocks. Then edit one paragraph (Shift+Enter at end),
   save, diff — the new `\n` should appear in the right place.

### Defense-in-depth fix (landed 2026-05-16)

`matchesSetextShape` no longer fails when the buffer has a trailing
`\n` — it strips trailing newlines before looking for the underline
line. Pure additive change; no other tests affected. Without this,
typing Shift+Enter inside a setext heading buffer (which can produce
`"Heading\n=\n"`) would fall out of the setext path and demote to
paragraph spuriously.

### Original investigation notes (preserved)



While debugging the cursor regressions, the chop in
`LiveListModelBinding::onD2Changed:311–312` turned up as suspicious:

```cpp
QByteArray raw = doc->blockText(id);
// Trim trailing newline. Per-item ListItem blocks have content-only
// buffers (the parser strips all trailing newlines from the per-item
// content). The single trailing '\n' is the block-delimiter convention.
if (raw.endsWith('\n'))
    raw.chop(1);
r.text = QString::fromUtf8(raw);
```

For a paragraph that just received a soft break (Shift+Enter), buffer
becomes `"Heading\n"`. The chop strips the `\n` → `model.text = "Heading"`
→ delegate's `edit.length = 7` → cursor cannot reach `qtPos=8` (Qt's
`QQuickTextEdit::setCursorPosition` rejects `pos > characterCount-1`).
Symptom: Shift+Enter+typing setext underlines lands the underline
character at the wrong byte offset (before the soft break, not after),
producing `Heading=\n` instead of `Heading\n=`.

The `typeShiftEnterDashes_producesSetextH2` unit test passes only
because it bypasses the cursor logic and calls `d2ApplyBufferEdit` at
byte 8 directly.

**Investigation tasks:**

1. Confirm with a test (either new harness from #3 or a
   focused-typing test in existing infrastructure) that production
   Shift+Enter+typing-`-` produces `Heading=\n` not `Heading\n-`.
2. Read `MarkoffDocument::loadFromMarkdown` and the parser's
   block-buffer convention — does load add trailing `\n` to all blocks?
   If yes, the chop is correct *for loaded blocks*. If no, the chop's
   premise is wrong.
3. Decide on a fix shape:
   - **Option A:** kind-aware chop. Paragraphs with internal `\n`
     content keep the trailing `\n`; other kinds chop as today.
   - **Option B:** change the buffer convention so blocks never end
     with `\n` (load + insertSoftBreak both normalise).
   - **Option C:** stop chopping; let the trailing `\n` show. Audit
     every consumer of `BlockRecord.text`.

**Reading order:**

1. `libs/markoff-live/src/LiveListModelBinding.cpp:308-313` — the chop.
2. `libs/markoff-core/src/Cmd/D2.cpp:49-54` — `insertSoftBreak`.
3. `libs/markoff-core/src/MarkoffDocument.cpp` (search for
   `loadFromMarkdown` / block insertion) — the load convention.
4. `libs/markoff-core/tests/d2/tst_d2_cmd_decomposition.cpp:50-56` —
   confirms `insertSoftBreak` produces a trailing `\n` buffer.
5. Every consumer of `BlockRecord.text` (grep `recordAt` /
   `record.text`) — Option C audit surface.

**Open design questions to brainstorm:**

- Is the original "block-delimiter convention" comment (in the chop
  code) still accurate? Was it ever?
- If Option B (normalise convention), what about `loadFromMarkdown` of
  documents already containing trailing `\n` — does the parser
  preserve them by intent?
- Can we test #1 without #3's harness, or is the harness a hard
  prerequisite?

**Definition of done:** Either the chop is provably correct (close
this item with a regression test guarding the convention) or it's
fixed (with a regression test for Shift+Enter+typing-`-` →
`Heading\n-` in the buffer).

---

## #5 — Code review of recent cursor-fix commits ✅ CLOSED 2026-05-10

**Effort:** <1 hour. **Status:** done. See banner at top for disposition.

Three commits implement the S1/S2/S3 fix and its pivots:

- `463fc36` — Tier 2 (canonical sync, focusedQtPos clamp, delegate hooks).
- `9ca7cb0` — defense-in-depth (silent qtPos-only emit, flush-then-sync).
- `6c44a07` — partial revert (drop clamp + silent-emit, restore Tier 1
  surgical fix on top of canonical sync).

**To execute:** dispatch the `qt-code-reviewer` agent in foreground:

> Review commits `463fc36..6c44a07` inclusive on branch
> `exploration/new-foundation` in
> `/home/clinton/dev/Markoff/.worktrees/foundation-exploration/`.
> Context: implementing setext/ATX kind-transition cursor re-anchoring
> (S1/S2/S3 from `docs/handoff/2026-05-09-setext-dogfood-findings.md`).
> The chain pivoted twice; please flag any leftover dead code,
> unit-test gaps, suspicious cross-call ordering, qtPos vs byte-offset
> confusion, and Qt6 best-practice deviations. Specifically scrutinise
> `LiveCursorState::syncFromTextEdit`, the kind-transition return
> blocks in `LiveListModelBinding::onD2Changed`, and the
> `onCursorPositionChanged` handlers added to the 5 text-bearing
> delegates. Read-only review; report findings as a punch list under
> 400 words.

**Disposition rule:** findings that overlap queue item #2 (cursor
architecture cleanup) get folded into that plan when it gets written.
Findings that don't (style, dead code, isolated bugs) get fixed inline
before the next dogfood.

---

## #6 — `nav_into_runtime_promoted_heading` regression (tier-3 fallout) ✅ CLOSED 2026-05-16

**Effort:** ~1 hour. **Status:** fixed — `LiveCursorState::tryResolvePending`'s
stale-registration check now compares **delegate classes** rather than
literal kind strings. Within-class transitions (paragraph ↔ heading,
all in the `text-inline` `delegateClass`) no longer falsely bail.
Cross-class staleness is preserved via the existing `delegateClassFor`
mismatch path; an explicit empty-currentKind guard handles unknown
blocks. Self-heals the registered kind once a class match is
confirmed. Two `tst_live_cursor_state_chokepoint` unit tests updated
to exercise post-tier-3 (cross-class) staleness — the pre-tier-3
literal-kind staleness check is no longer the contract. Side benefit:
the same fix recovers two pre-existing `tst_live_render_qml_integration`
stress-test failures (`stress_walk_paragraph_heading_listitem_chain`,
`stress_walk_enter_then_backspace_merge`). Falsifiability already
in-tree: the test failed prior to the fix and passes after.

(Historical investigation notes preserved below for context.)

`tst_live_render_focus_chokepoint_invariant::nav_into_runtime_promoted_heading`
fails on `exploration/new-foundation` post-tier-3 with the chokepoint
invariant violated:

```
delegateRow: 2
cursorRow  : 1
```

i.e. after the test promotes row 1 from paragraph to heading via typing
`# `, then walks Down→Up, `LiveCursorState::focusedAnchorRow()` reports
row 1 but `focusedDelegate()->property("modelIndex")` is row 2. Cursor
state and delegate focus disagree — exactly the class of bug the
chokepoint invariant suite was built to catch.

The originating regression D-fc-2 ("new heading impermeable to arrow
keys") was fixed in commit `4fb711f` (see
`docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md:153` for
the original symptom and §D-fc-2 disposition note in queue.md banner
near line 160). That fix relied on `LiveBlockModel::applyOps` synthesising
`beginResetModel`/`endResetModel` for the kind-change Delete+Insert
pattern — a heavy hammer logged as an invariant-7 smell at the top of
the Discipline Log.

**The tier-3 work (commits `4a7d63a..30891eb`) retired that heavy
hammer:** within-class kind transitions (paragraph↔heading,
paragraph↔list-item, etc.) are now `dataChanged` on a single
`UnifiedInlineTextDelegate` row rather than Delete+Insert. The
Discipline Log entry that previously logged the hammer is now closed
with `~~...~~ → fixed in d60f896`. The hammer is gone, but the chokepoint
invariant test for the exact scenario it was protecting (arrow nav into
a runtime-promoted heading) regressed at the same time.

**Hypothesis to test first:** `LiveListModelBinding` updates the
delegate-class on `dataChanged`, but `LiveCursorState::m_delegates`
caches the *old* `(kind, root)` pair under the BlockAnchor. After the
within-class kind transition, the new `Up` press resolves the pending
focus against the stale `m_delegates` entry → focus lands on whatever
delegate happens to claim activeFocus next (row 2's). Look for the
delegate-going-away / delegate-available registration ordering during
a within-class transition in
`LiveCursorState::{delegateAvailable, delegateGoingAway}` (lines 356–
404).

**Reading order:**

1. `tst_live_render_focus_chokepoint_invariant.cpp:259` — the test.
2. `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`
   — §5 (delegateClass authority) and §6 (kind-transition flow) describe
   the new within-class transition mechanism.
3. `libs/markoff-live/src/LiveCursorState.cpp:356–404` — delegate
   registration and the chokepoint invariant logic. Especially the
   `wasRegistered` re-stage at lines 371–381: that branch assumes the
   delegate-root POINTER survived the kind transition, which under tier-3
   it does (same UnifiedInlineTextDelegate). Does that path correctly
   route the pending focus to the same delegate after `kind` changed?
4. `libs/markoff-live/src/LiveBlockModel.cpp` — the retired
   `kindOnlySwap` path (Discipline Log entry, line 63 `~~...~~`); the
   D-fc-2 fix was the synthetic `beginResetModel`/`endResetModel` there.
5. `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md:153`
   — the original D-fc-2 symptom; the test was written FOR this scenario.

**Definition of done:** test green, with at least one paragraph in the
commit message explaining why the tier-3 delegate-identity-preservation
doesn't conflict with the chokepoint invariant for runtime-promoted
headings. If the fix requires re-introducing a kind-transition-aware
hammer, log a fresh invariant-7 entry in the Discipline Log explaining
why the tier-3 retirement wasn't sufficient — the smell trail matters.

**Open question:** are any of the other 5 baseline failing tests in
`tst_live_render_qml_integration` (per tier-3 spec §117-119) actually
the same regression? Worth running them after the fix lands to see if
the count drops.

---

## When this queue is empty / superseded

Delete the file or move it to `docs/archive/`. The CLAUDE.md banner
that points here should be removed at the same time.
