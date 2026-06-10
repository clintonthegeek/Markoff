# Work queue

> Open work items + the append-only Discipline Log. Ordered by
> **descending execution difficulty** — a fresh agent should pick the
> topmost item that fits the available time/energy budget.
>
> **For a fresh agent landing here:** each item below names enough
> context to draft a spec/plan. Use `superpowers:brainstorming` to
> resolve open questions, then `superpowers:writing-plans` to write
> the plan, then execute task-by-task. Specs live in `docs/specs/`,
> plans in `docs/plans/`, both dated `YYYY-MM-DD-<slug>.md`. Add a
> back-reference here once the plan exists.
>
> **Current branch:** `master` (single line of development since the
> foundation merge at `3c7afa9`, 2026-05-25).
>
> **Closed items** (#1–#7, #9, and the closed sub-items of #8, plus the
> historical header banners) were archived 2026-06-09 to
> [`docs/archive/2026-06-09-queue-closed-items.md`](archive/2026-06-09-queue-closed-items.md)
> — verbatim snapshot. Live status board: [`docs/STATUS.md`](STATUS.md).
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

- `libs/markoff-core/src/Detail/FlatBlockResolve.cpp:findBlockAtSepByte`
  separator-zone underflow — fixed in `eb685f0` while landing the 2026-05-27
  caret-authority work (a sep-view byte strictly inside `\n\n` underflowed
  `byteInBlock` and silently mis-attributed the hit, bypassing cross-block
  merge). Covered indirectly by the new
  `tst_styled_dogfood_invariants::backspace_at_block_start_merges_with_caret_at_join`;
  a dedicated unit test on `findBlockAtSepByte` separator-zone boundaries
  would prevent regression and is worth adding (qt-code-review minor flag).
- `libs/markoff-core/src/Detail/FlatBlockResolve.cpp` — `SEP_LEN`
  reduced 2 → 1 under WP unification
  (`docs/specs/2026-05-28-flat-view-wp-unification-design.md`). The
  separator-zone branch in `findBlockAtSepByte` is now unreachable for
  valid inputs (one-byte separator has no interior position); kept as
  defensive code. A direct unit test on the boundary cases of
  `findBlockAtSepByte` remains worth adding as a small follow-up — same
  recommendation as the 2026-05-27 underflow find.
- ~~2026-05-30 `libs/markoff-source/src/Editor.cpp:502` — inv #8 —
  `setHeadingLevel` carried a bespoke block-walk hardcoding `SEP_LEN = 2`
  while its `lineStartSep` came from the live single-'\n' `toPlainText()`;
  `byteInBlock` underflowed for any heading below the first block.~~ →
  fixed 2026-05-30 (queue #8.6 closeout) by reusing `Detail::findBlockAtSepByte`
  (single shared `SEP_LEN == 1`), deleting the duplicate constant. Class:
  "one flat-text view changed separator width (`fdb68bf`), a sibling
  byte-walk didn't."
- ~~2026-05-30 `libs/markoff-source/src/Detail/SourceFindAdapter.cpp:104` —
  inv #3 — **same bug class as the setHeadingLevel fix above.**
  Global-char mapping advanced `globalChar += 2` for the `\n\n`
  `interBlockSeparator()` (the canonical `flatView()`/serialize separator),
  but the source widget *displays* `widgetFlatView()` with a single '\n'
  between blocks (WP unification) — multi-block find highlights drifted
  by one char per preceding boundary (third instance of the "one
  flat-view changed separator width, a sibling didn't" class).~~ →
  fixed 2026-06-09; regression test `tst_source_find_adapter` (2 slots,
  proven failing pre-fix) pins highlight + navigation positions against
  the visible text.
- `libs/markoff-styled/src/StyleApplier.cpp` — `baseBlockFormat` 5pt
  margins are dead in practice: every kind dispatch
  (Paragraph/Heading/CodeBlock/BlockQuote/ListItem/HR plus the `else`
  fallback to `applyParagraph`) overrides the base with kind-specific
  values. The 5pt base matches the WP-unification spec literally but
  acts only as a safety fallback for hypothetical future unhandled
  kinds. Consider centralising margins through the base (kind-specific
  deltas) once dogfood pins down the desired values.


- 2026-05-31 styled-table SIGSEGV post-mortem (lesson entry; fix shipped in `b1b238f`) — inv #4/#5 — `FormatPass` computed QTextDocument positions from flat pipe-source bytes; once a table is a compact `QTextTable` frame every later block overran the document and an invalid `QTextBlock` reached `QTextList::add()`. The spec/plan flagged exactly this divergence, but the guard test used a *paragraph* after the table, which tolerates a bad position — only a *list* dereferences it. **Lesson: a guard test for "coordinates after an opaque frame" MUST include a list.** Fix: frame-aware lockstep walk (`QTextFrame::iterator`), invalid-block guard in `manageListMembership`, frame-key format fix. Regression test `tst_styled_table_render::list_after_table_does_not_crash_and_renders`. (Moved from the orphaned bottom-of-file log section, 2026-06-09.)

- 2026-06-09 `libs/markoff-core/src/MarkoffDocument.h:18-20` — inv #8 (audit finding) — public header includes `crdt/Anchor.h`/`Clock.h`/`Operations.h`, so view leaves transitively import CRDT types despite their own signatures being CRDT-clean. Fix shape: forward declarations + private-side includes. Related: `Session.h` uses raw `Crdt::Anchor` in public signatures while `Styled::Editor` exposes `Q_PROPERTY(Markoff::Session*)` — legal for consumers (heavy-CRDT policy) but one hop from a view-leaf API; needs an explicit written decision.

- 2026-06-09 `libs/markoff-live/src/LiveCursorState.cpp:selectAllBlocks` — inv #8 — sets `m_selectionExtended = true` AFTER `request()` has already emitted `selectionChanged`, so `LiveActionController::updateEnabledStates` (connected to that signal) evaluates `hasSelection()` as false on Ctrl+A: cut/copy stay disabled until the next selection event. Found during Task-8 contract-test triage; the test primes via `begin()/extend()` instead. Fix shape: set the flag before the `request()` call (or re-emit once after).

---

## #8 — Flat-view kind follow-ups (one sub-item open)

Cluster from the 2026-05-29 WP-unification dogfood arc. Sub-items
1 (BlockQuote split), 2 (setext canonicalisation), 4 (ordered-list
numbering), 5 (attr hash gate), 6 (`tst_source_widget_format_ops`
triage + `setHeadingLevel` SEP_LEN fix), 7 (`tst_styled_block_formats`
triage), and 8 (styled structural-key authority) are **all closed** —
full records in the archive snapshot and their specs/plans.

**#8.3 — Source-view list-item markers (OPEN).** `markoff-source`
consumes `widgetFlatView()`, which for ListItem yields the post-marker
buffer — so source shows `foo` instead of `- foo` for a `- foo` item.
Source view's whole point is "raw markdown visible." Either (a) source
uses `serializeForSave()` for its initial seed plus a separate
marker-aware widgetFlatView variant, or (b) keep `widgetFlatView()` and
have the binding prepend markers via per-block decoration. Architectural
spec needed; touches `SourceTextDocumentBinding` and the
`markoff-source` Editor.

---

## #10 — Deterministic live-test failures (the former "3 known offscreen flakes")

**Filed 2026-06-09.** The 2026-06-09 audit ran the three failing
binaries in isolation twice: **all 6 failing slots are deterministic
under offscreen** — identical failures across runs. The "offscreen
flake" label was masking real triage debt (some slots have been failing
since ~2026-05-21). Classify-before-fixing applies per slot:

1. `tst_live_render_e2_nav_shift_extend` —
   `ctrl_shift_left_inside_block_returns_not_handled` +
   `ctrl_shift_right_...` (2 slots). **Already classified** as
   behavioral drift from the audit-L4 word-extend rework (`0cbdf48`);
   see the 2026-05-22 Discipline Log entry. Either the
   `LiveNavigationController` "claims the chord" behavior is correct
   and the test contract should be renamed/reshaped, or the
   not-handled return was load-bearing for a consumer. Owner: whoever
   next pulls on `LiveNavigationController`.
2. `tst_live_render_focus_chokepoint_invariant` — `undo_after_enter`,
   `redo_after_undo`, `nav_into_runtime_promoted_heading` (3 slots).
   Undo/redo caret restoration through the chokepoint; partial
   investigation log in the archive snapshot (§#6 historical notes).
   Suspect real gaps in undo-path caret authority
   (VIEW-IMPLEMENTORS-GUIDE §B.4 partial).
3. `tst_live_render_cursor_typing_invariant` —
   `cursor_mirrors_textedit_through_emoji_typing` (1 slot). Surrogate-
   pair (UTF-16) qtPos mirroring during typing; relates to queue-#2
   concern #2 (bytes-vs-qtPos naming) which closed without a semantic
   migration.

**Definition of done:** each slot either green or its contract
explicitly renamed/reshaped with rationale (classify first); the
"known failures" language is removed from STATUS.md/CLAUDE.md when the
count reaches 262/262.

---

## #11 — Legacy flat-buffer APIs: retire or migrate

**Filed 2026-06-09.** `SearchEngine::findAll`/`findNext`/`findPrevious`
and `CompletionDetector::detect` read `toMarkdown()`/`toMarkdownUtf8()`
(the legacy flat buffer) and anchor results in that buffer's coordinate
space. On a D2-loaded document the legacy buffer is stale-or-empty, so
they return nothing useful. **They have zero production callers**
(D2-native find is `SearchEngine::findByBlock` via `FindController`;
live-leaf completion went with the retired view) — only their unit
tests exercise them. Header doc-comments added 2026-06-09 state the
constraint.

Resolution options when next touched: (a) delete them with their tests
(API break — fine pre-1.0, nothing consumes them); (b) rewrite over
`iterateBlocks()`/`blockText()` when a consumer actually wants
whole-document search-with-Session-selections or completion. Blocked
on nothing; bundle with any future legacy-buffer retirement pass
(`toMarkdown` deprecation, `SourceTextDocumentBinding.cpp` legacy
fallback, `version()`).

---

## #12 — `EmbedRegistry` test coverage

**Filed 2026-06-09.** `EmbedRegistry` + `MarkdownRenderChild` +
`EmbedDepthGuard` were restored for the Corbomite port (`47f62c4`),
then grew `hasExtension`/`unregisterExtension` (`e8986f8`) — all with
**zero test references**. Corbomite consumes the registry
(`MainWindow` + `HoverPopover`). Needs a small unit binary:
register/unregister/has, extension dispatch, depth-guard recursion
cap. Also untested: `Gutter` (markoff-source), `StyledTableRenderer`
(indirect only via `tst_styled_table_render`'s editor path).

---

## #13 — Source-view cursor/selection translation rewrite (from retired TODO.md)

Carried over from `docs/TODO.md` (archived 2026-06-09). The 2026-05-21
source-view cleanup follow-ups
(`docs/specs/2026-05-21-source-view-cleanup-followups.md`) listed four
items: #1 `insertLink` block-aware port (done `a713f04`), #2 dead
`toMarkdownUtf8()` fallback removal (done `9fad37f`), #4
separator-delete merge gap (closed by the 2026-05-27 binding-robustness
arc). **#3 remains:** cursor/selection translation rewrite via a
block-aware intermediate (largest item; own spec+plan). Pull in
opportunistically when next touching the source view.

---

---

## #14 — Find-highlight color: theme integration (follow-up from contract-v2 arc)

**Filed 2026-06-10.** All three leaf find adapters
(`SourceFindAdapter`, `StyledFindAdapter`, `LiveFindAdapter`) use a
**hardcoded soft yellow** for the find-highlight background (the
`QTextCharFormat` / `ExtraSelection` color is a literal `QColor`
constant in each adapter, not drawn from `Markoff::Theme`). The active
match uses a slightly brighter variant, also hardcoded. `Theme` already
has `SearchMatchBackground` and `SearchActiveMatchBackground` slots
(used by the live delegate's `InlineHighlighter`). The fix is to have
each adapter's `rebuildAndPushSpans`/`updateExtraSelections` read those
slots from the current theme (subscribe to `MarkdownView::themeChanged`
or accept a `Theme` reference on attach). Affects all three leaves
uniformly — bundle as one small task.

---

## #15 — contextChanged staleness: kind-change without caret move (source + styled)

**Filed 2026-06-10.** On `markoff-source` and `markoff-styled`,
`contextChanged` is recomputed only on `cursorPositionChanged` (see
spec §7 deviation note: connecting `d2DocumentChanged` caused false-fires
from the `StyleApplier`'s format-only passes). A programmatic
`Cmd::changeKind` that does not move the caret therefore leaves the
`EditorContext` stale until the next caret move. In normal interactive
use every structural key that changes a block kind also moves the caret,
so the window is narrow. Severity: low. Resolution options: (a) connect
a dedicated `d2StructuralChanged` signal (fired on kind/block-boundary
changes only, not content edits); (b) post-filter the existing
`d2DocumentChanged` to skip format-only passes (kind-change always bumps
`d2EditSequence`; a format-only pass does not). Not blocking any current
consumer. Fix in the same pass as any future structural-signal
refactoring.

---

## #16 — styled fontScale path to StyledTableRenderer: test coverage gap

**Filed 2026-06-10.** `Markoff::Styled::Editor::setFontScale()` forwards
the new scale to `m_tableRenderer->setFontScale()` (Task 12,
`libs/markoff-styled/src/Editor.cpp`). This path is untested:
`tst_view_contract_styled` exercises fontScale via signal assertions and
a `QPlainTextEdit` font-size check, but does not load a document
containing a table block and verify that `StyledTableRenderer` was
notified. Add a slot: load a doc with a table, call `setFontScale(1.5)`,
assert `editor->styleApplierHashSkips() == 0` (i.e. a restyle ran) and
optionally check that the renderer's internal scale matches. Binary:
`tst_styled_table_render` or `tst_view_contract_styled`.

---

## When this queue is empty / superseded

Delete the file or move it to `docs/archive/`. Update `docs/STATUS.md`
and the CLAUDE.md pointer at the same time.
