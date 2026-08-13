# Work queue

> Dormant work items + the append-only Discipline Log. **The active
> workfront (canvas spike) is NOT tracked here** — see
> [`docs/plans/2026-08-13-markoff-canvas-spike.md`](plans/2026-08-13-markoff-canvas-spike.md).
> Nothing in this file is to be worked while the spike standstill is in
> effect, except bug fixes that a failing test forces.
>
> **Trimmed 2026-08-13** for the canvas-spike reset: all struck
> Discipline-Log entries and closed items #8/#10/#11/#12/#14/#15/#16
> (with their full closure records) moved verbatim to
> [`docs/archive/2026-08-13-queue-pre-canvas-snapshot.md`](archive/2026-08-13-queue-pre-canvas-snapshot.md).
> Earlier snapshot (items #1–#7, #9): `docs/archive/2026-06-09-queue-closed-items.md`.

---

## Discipline Log (open smells only — struck history in the snapshot)

> One line per smell noticed in passing; no fix required in the same
> session (invariant 8, `docs/INVARIANTS.md`). Format:
> `- YYYY-MM-DD <file:line> — inv #N — <one phrase>`. Close by
> prepending `~~` and appending `→ fixed in <commit>`; when this file
> is next trimmed, struck entries move to the archive snapshot.
> Scope note: these govern the live/styled/source seam. The canvas
> leaf is governed by its constitution (spike spec §6) instead — a
> canvas smell is a spike finding, not a log entry.

- 2026-05-13 `libs/markoff-live/src/BlockKindRegistry.cpp:Math` — inv #8 — `isBlockOnly` is explicit-false for Math despite Math having no `TextCaret` in `supportedCursorVariants`. Transitional asymmetry; will be removed when Math becomes text-bearing in its own spec.
- 2026-05-18 `libs/markoff-source/src/Editor.cpp:hideFindBar` — inv #7 — `isVisible()` early-return acts as a re-entrance guard for the `closed → hideFindBar` signal cycle. Cleaner future shape: split `FindBar::deactivate` into `clear()` + `requestClose()`. Documented in commit `0ec907d`.
- 2026-05-19 `libs/markoff-live/src/LiveEditBinding.cpp:73` — inv #7 — `m_applyingTextUpdate` re-entrance guard + public `Q_INVOKABLE isApplyingTextUpdate()`, read by 2 production QML delegates to suppress echo reactions. Removal requires re-architecting the QTextDocument↔MarkoffDocument echo loop. Frozen with explicit acceptance (D7 of `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`).
- 2026-05-19 `libs/markoff-live/qml/delegates/MathDelegate.qml:125` — inv #6 — `Qt.callLater(latexEdit.forceActiveFocus)` defers focus takeover after a `BlockInternalEdit` variant change. Frozen with explicit acceptance (D8 of the markoff-live freeze spec).
- 2026-05-21 `libs/markoff-live/include/markoff/live/LiveCursorState.h:isApplyingSelection` — inv #7 — re-entrance guard around the synchronous `selectionChanged` dispatch window (delegates' `applySelection` writes `TextEdit.cursorPosition` for visual purposes; each write fires `onCursorPositionChanged`). Frozen with explicit acceptance for the markoff-live freeze; eventual unification with `m_applyingTextUpdate` removal belongs in one redesign.
- 2026-05-21 `libs/markoff-live/qml/LiveView.qml:hit()` — inv #4 — `clampedLocalX` clamps click local-x to full delegate width, so clicks in trailing whitespace return `qtPos = end-of-block`; selection "only registers near the text glyphs." Production fix TBW (clamp to text bbox or sentinel for caret-at-EOL).
- 2026-05-21 `libs/markoff-live/qml/delegates/` — meta — every QML TextEdit has a documented intercept architecture; route new Ctrl-chords / mutating-key handlers through `qml/delegates/KeyDispatch.js`, never duplicate. Audit: `docs/specs/2026-05-21-textedit-interface-audit.md`.
- 2026-05-22 `libs/markoff-live/qml/delegates/TableDelegate.qml:cell-bindings` — inv #8 — column-count change (2→1 or 2→3) via `applyFlatEdit` SIGSEGVs during QML Repeater rebuild; crash pattern points at use-after-free of a string buffer interpreted as a pointer. Triage path: instrument cell-delegate destruction; look for QString/QVariant captured by a binding that outlives the Item.
- ~~2026-05-22 `libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp:ctrl_shift_left/right_inside_block_returns_not_handled` — inv #8 — two slots fail pre-E4, contract drift from audit-L4.~~ → folded into queue #10 item 1, closed 2026-08-12 (slots renamed `..._claims_the_chord`, reshaped to assert `Handled`).
- 2026-05-26 `libs/markoff-styled/src/StyleApplier.cpp:m_applyingFormats` — inv #7 — re-entry guard on format application — defended by QSignalBlocker + edit blocks, but Qt format-change signals are genuinely re-entrant via observers (spec §7).
- 2026-05-26 `libs/markoff-styled/src/Editor.cpp:m_applyingFontScale` — inv #7 — declared but never written/read. Possibly dead; audit when Ctrl+/- font-scale UI is exercised.
- 2026-05-26 `libs/markoff-styled/tests/tst_styled_inline_formats.cpp:inline_code_uses_monospace` — weak-assertion — `|| fontFamilies().size() > 0` branch is tautological; tighten to `QVERIFY(cf.fontFixedPitch())`.
- 2026-05-26 `libs/markoff-styled/src/LinkInteraction.cpp:handleMove` — `setCursor()` fires on every MouseMove within the same link; cache the shape.
- 2026-05-26 `libs/markoff-core/src/MarkoffDocument.cpp:buildD2FromBytes step 5` — inv #3 — the tail clears are redundant with `wipeD2State()` at both call sites; safe to delete in one follow-up touch.
- 2026-05-27 `libs/markoff-styled/src/StyleApplier.cpp:applyPendingKindChanges` — inv #6 — `QTimer::singleShot(0)` defers `Cmd::changeKind` out of the `d2DocumentChanged` slot; justified per spec §4.4 (avoids synchronous CRDT re-entry during cascade).
- 2026-05-27 `libs/markoff-styled/src/StyleApplier.cpp:applyFormats` — inv #6 — `QTimer::singleShot(0)` defers scroll-position restore until Qt's post-endEditBlock layout settles.
- 2026-05-27 `libs/markoff-core/src/SourceTextDocumentBinding.cpp:sepViewToNoSepByteForEdit` — inv #8 — calls `iterateBlocks()` twice. Inefficiency only; optimize if profiling flags it.
- 2026-05-27 `libs/markoff-core/src/SourceTextDocumentBinding.cpp` + `applyFlatEdit` cross-block branch — inv #8 — a delete spanning ALL content of ≥2 blocks can leave one empty surviving block. Whole-document-select-delete edge; harmless; revisit if dogfood surfaces it.
- `libs/markoff-core/src/Detail/FlatBlockResolve.cpp:findBlockAtSepByte` — a dedicated unit test on separator-zone boundary cases remains worth adding (flagged twice: the `eb685f0` underflow fix and the WP-unification `SEP_LEN` 2→1 change, whose separator-zone branch is now defensive-only).
- 2026-05-31 styled-table SIGSEGV post-mortem (lesson entry; fix `b1b238f`) — inv #4/#5 — a guard test for "coordinates after an opaque frame" MUST include a *list* after the frame (a paragraph tolerates a bad position; only a list dereferences it). Regression net: `tst_styled_table_render::list_after_table_does_not_crash_and_renders`.
- 2026-06-09 `libs/markoff-core/src/MarkoffDocument.h:18-20` — inv #8 — public header includes `crdt/Anchor.h`/`Clock.h`/`Operations.h`, so view leaves transitively import CRDT types. Fix shape: forward declarations + private-side includes; `Session.h`'s raw `Crdt::Anchor` in public signatures needs an explicit written decision.
- 2026-06-09 `libs/markoff-live/src/LiveCursorState.cpp:selectAllBlocks` — inv #8 — sets `m_selectionExtended = true` AFTER `request()` emitted `selectionChanged`, so cut/copy stay disabled until the next selection event after Ctrl+A. Fix shape: set the flag before `request()` (or re-emit once).
- 2026-06-16 `libs/markoff-live/src/LiveClipboardController.cpp:flatByteOffset` — inv #8 — yet another ad-hoc flat-byte coordinate space (zero-separator sum), distinct from `flatView()` `\n\n`, `widgetFlatView()` `\n`, and the CRDT global space. Internally consistent (writer and sole reader agree) so not currently broken; fourth instance of the separator-width bug class.

---

## #13 — Source-view cursor/selection translation rewrite

Carried from the retired TODO.md. Items #1/#2/#4 of
`docs/specs/2026-05-21-source-view-cleanup-followups.md` are done;
**#3 remains:** cursor/selection translation via a block-aware
intermediate (largest item; own spec+plan). Pull in opportunistically
when next touching the source view — *after* the spike closes.

## #17 — markoff-canvas spike *(CLOSED 2026-08-13 — PASS)*

Decision record `docs/specs/2026-08-13-view-authority-direction-decision.md`
(qtbase fork rejected permanently; spike authorized). Spec
`docs/specs/2026-08-13-markoff-canvas-spike-design.md` (constitution
C1–C4, exit criteria E1–E10, findings §9, verdict §10). Plan with the
completed task checklist + SHAs:
`docs/plans/2026-08-13-markoff-canvas-spike.md` — T0–T11 all done.

All ten exit criteria met with falsification proof; constitution
intact end to end. Per the decision record §6 the pass consequence —
opening the **D5 design** (candidate architecture + contingent
retirement of markoff-live/styled, §5.3) — is a **user decision, not
an implementer default**. Until D5 opens, the whole tree including
`libs/markoff-canvas/` is bug-fix-only. Post-spike findings from
hands-on use: **#18** below.

## #18 — canvas: findings from first hands-on use *(CLOSED 2026-08-13 — absorbed into the production arc)*

The four post-spike findings (delimiter reflow, cross-table selection,
typed/setext heading level, setext test gap — full write-up in spike
spec §9 "Post-spike") are now plan tasks in the canvas production arc:
reflow → P2.1/P2.2, cross-table selection → P2.3, heading level +
setext test → P1.1. Spec ruling on the projection map's C4 status:
`docs/specs/2026-08-13-canvas-production-design.md` §3/§4.2.

**Decided and closed, not an open item:** plain Enter stays
word-processor new-paragraph. No backward-reaching input rule that
merges a bare `===` block into the paragraph above — see spike spec §9
for why. Source mode is the escape hatch.

## Other dormant (one line each)

- Styled tables: in-grid cell edit, row/col ops, alignment menu,
  source-reveal flip (seam landed 2026-05-30).
- Release scaffolding: install/export rules + header tiering for a
  non-submodule consumer (LICENSE/README landed 2026-06-09).
- E-arc: dormant since 2026-05-25 (`docs/e-arc/`, closed board).

---

## When this queue is empty / superseded

Delete the file or move it to `docs/archive/`. Update `docs/STATUS.md`
and the CLAUDE.md pointer at the same time.
