# MarkdownView contract v2 — API finalization for Corbomite

**Date:** 2026-06-09
**Status:** approved (brainstormed with user; decisions §0)
**Plan:** `docs/plans/2026-06-09-markdownview-contract-v2.md`
**Driving consumer:** Corbomite (`/home/clinton/dev/Corbomite`), which swaps
three Markoff view leaves behind `MarkdownView *activeLeaf()` and today
bridges every gap with `qobject_cast` switches and escape hatches.
**Audit provenance:** the 2026-06-09 full audit (cross-leaf API matrix in
the audit record; summary in `docs/STATUS.md` § Open items).

---

## 0. Decisions taken during brainstorming

| Question | Decision |
|---|---|
| Scope | **One spec** covering base contract + live-leaf honesty + styled find + format-ops hoist + EditorContext feed. Phased internally so the plan lands incrementally. |
| Live `setReadOnly` depth | **Block at mutation ingress** (gates at the ~6 model-mutation chokepoints), not full QML `Capabilities::Editable` plumbing (stays withdrawn per the live-freeze D11 record). |
| Corbomite adoption | **Not in this arc.** Markoff-side API + tests land here, plus a written adoption brief (`docs/handoff/2026-06-09-corbomite-api-adoption-brief.md`). Corbomite migrates in its own session. |
| Contract shape | **Virtuals on `MarkdownView`** with safe defaults. Not capability interfaces (formalizes the cast instead of killing it), not an actions-object (can layer on later). |

## 1. Problem

The three leaves present three dialects. From the audit's consistency
matrix:

- `Live::EditorWidget` **fakes** the base contract: `cursorPosition()`
  returns `{0,0}`, `setReadOnly()` stores a bool and disables nothing
  (documented degradations, `EditorWidget.h:36-44`). This is the root
  of Corbomite's largest `TODO(port-foundation-exploration)` stub
  cluster (ephemeral-state restore, goToLine, line/col statusbar).
- Find attach exists on Live + Source but not Styled (Corbomite's
  Reading mode silently has no find) and not on the base (Corbomite
  switches on leaf type, `NoteEditorWidget.cpp:489-505`).
- Undo: all three leaves route **keyboard** Ctrl+Z to `undoD2`
  (`LiveActionController.cpp:96`, `StructuralTextEdit.cpp:56`,
  `markoff-source/Editor.cpp:200`), but there is no programmatic API —
  so Corbomite's Edit menu calls `plainTextEdit()->undo()`
  (`MainWindow.cpp:1275`), invoking **QPlainTextEdit's widget undo
  stack instead of `undoD2`**. That is the dual-authority pattern
  INVARIANTS §3 exists to prevent, exported to the consumer.
- Theme: `Q_PROPERTY` on Styled + Source; Live only via
  `binding()->setTheme()`.
- fontScale/zoom: property on Styled; QActions on Live; absent on Source.
- Format ops: `Q_INVOKABLE`s on Source (321 lines of bespoke logic);
  QActions on Live; absent on Styled.
- `EditorContext`/`contextChanged`: consumed by Corbomite's toolbar
  (`MainWindow.cpp:623-633`), produced by nothing.
- `cursorPositionChanged`/`scrollPositionChanged` exist on the base but
  leaf emission is patchy.

## 2. Goal

One honest contract on `Markoff::MarkdownView` such that Corbomite's
swap dispatches polymorphically with **zero `qobject_cast` switches and
zero escape-hatch calls** for: document, cursor, scroll, read-only,
find, undo/redo, theme, fontScale, format operations, and editor
context. The escape hatches (`binding()`, `plainTextEdit()`) remain for
genuinely leaf-specific needs but stop being load-bearing for common
operations.

### Non-goals

- Word count / text-changed convenience (model-level; consumers connect
  to `MarkoffDocument::d2DocumentChanged` / compute from
  `serializeForSave()`).
- Install/export packaging, header tiering (separate release work).
- Corbomite-side migration (handoff brief instead).
- Session unification across leaves (Live auto-creates; Styled accepts;
  Source internal — unchanged this arc).
- Finishing the B.2/B.4 collab/undo **caret** partials
  (VIEW-IMPLEMENTORS-GUIDE §B): `undo()` restores content; caret
  placement after undo keeps today's per-leaf behavior.
- QML `Capabilities::Editable` (withdrawn live-freeze D11 amendment
  stays withdrawn; ingress gates supersede it for read-only).
- In-table find highlighting in the styled leaf (§6 degradation).

## 3. The base contract (`markoff-core`)

`MarkdownView` (`include/markoff/core/MarkdownView.h`) grows:

```cpp
// Find. Default: qWarning + no-op (a leaf that doesn't override
// advertises the gap loudly instead of silently).
virtual void attachFindController(FindController *fc);
virtual void detachFindController();

// Undo/redo. BASE-IMPLEMENTED: routes to document()->undoD2()/redoD2(),
// no-op while isReadOnly() (undo is a mutation; a read-only view must
// not change the model through the Edit menu either). Undo is
// model-level (all leaves' keyboard paths already call undoD2);
// virtual so a leaf can wrap for caret restoration later (§2 non-goal).
virtual void undo();
virtual void redo();

// Theme. Base stores the value and emits themeChanged(); leaves
// override to apply (calling the base first to keep the store).
virtual Theme theme() const;
virtual void setTheme(const Theme &);

// Font scale (zoom). Same store-in-base / apply-in-leaf split.
// Clamped to [0.25, 4.0] at the base.
virtual qreal fontScale() const;
virtual void  setFontScale(qreal);

// Format verbs. Default no-op; hasEditing() advertises capability.
virtual void toggleBold();
virtual void toggleItalic();
virtual void toggleStrikethrough();
virtual void toggleInlineCode();
virtual void insertLink();
virtual void setHeadingLevel(int level);   // 0 strips, 1..6 sets

signals:
    void themeChanged();
    void fontScaleChanged(qreal scale);
    void contextChanged(const Markoff::EditorContext &ctx);
```

Existing leaf methods with these names (Styled/Source `setTheme`,
Source format ops, Live/Source `attachFindController`) become
`override`s — no consumer-visible renames. Styled/Source `Q_PROPERTY`
declarations stay on the leaves (Qt properties can't live on the base
without moving the NOTIFY signals; the virtuals are the contract, the
properties are leaf sugar).

**CursorPos coordinate definition (now normative):** `line` = 1-based
index of the **visual flat line** — the QTextBlock index in a
widgetFlatView-seeded QTextDocument. Each model block contributes one
line plus its internal `\n` count (CodeBlock/Math retain internal
newlines; Paragraph/ListItem/setext-Heading/BlockQuote are collapsed at
load). `column` = 1-based UTF-16 position within that line. This is
what Styled/Source already implement
(`blockNumber()+1, positionInBlock()+1`); Live must match it (§4).

## 4. Live leaf honesty (`markoff-live`)

Invariant-1 citation: the cursor work reads
`docs/handoff/2026-05-07-live-binding-developmental-history.md` §A
(chokepoint) and `docs/specs/2026-05-22-cursor-authority-decision.md`
(L3: chokepoint canonical; `syncFromTextEdit` same-block contract).

### 4.1 `cursorPosition()` / `setCursorPosition()`

- **Read:** from `LiveCursorState::currentTextCaret()` (the canonical
  store — no new store, invariant 3 clean): caret block row + qtPos.
  Map to CursorPos by summing line contributions of preceding model
  blocks (1 + internal-`\n` count each, via `iterateBlocks()` +
  `blockText()`), then locating qtPos within the caret block's own
  lines. Non-TextCaret variants (BlockSelected, BlockInternalEdit,
  NoCursor) report the block's first line, column 1.
- **Write:** reverse the mapping (line → block row + intra-block qtPos)
  and route through the existing chokepoint
  `LiveCursorState::requestTextCaretAtRow(row, qtPos)`. Out-of-range
  lines clamp to the last block (matching `findBlockByNumber`-invalid
  → no-op on the widget leaves is NOT matched — clamping is more useful
  for goToLine; the contract test pins clamping for all three leaves,
  and Styled/Source gain the same clamp).
- `cursorPositionChanged(line, column)` emitted from the widget on
  `LiveCursorState::cursorChanged`, mapped the same way.

### 4.2 `setReadOnly()` — mutation-ingress gates

Single authority: a `readOnly` flag on `LiveListModelBinding`
(Q_PROPERTY, NOTIFY). `EditorWidget::setReadOnly` calls the base
(store) then pushes to the binding. Gates — each early-returns when
set:

| Gate | Site |
|---|---|
| Text edits | `LiveEditBinding::onContentsChange` (drop the edit; push canonical text back so the TextEdit can't drift) |
| Structural keys | `LiveStructuralKeyHandler::tryHandle` (consume + no-op for mutating keys) |
| Clipboard mutate | `LiveClipboardController::paste/pasteText/pastePrimary/cut`; `deleteSelection` |
| Table cell edits | `TableEditBinding::applyCellEdit` |
| QActions | `LiveActionController` disables cut/paste/delete/undo/redo/format/heading actions while read-only (copy/select-all/zoom stay enabled) |

QML TextEdits also get `readOnly: binding.readOnly` where cheap, as
belt-and-braces UX (caret stops blinking as editable) — but the gates
are the contract; the QML binding is cosmetic and not relied on by
tests. Navigation, selection, copy, link activation, find all keep
working.

### 4.3 Theme / fontScale forwarding

`EditorWidget` overrides `setTheme`/`setFontScale` to call the base
store then forward to the binding's existing
`setTheme`/`fontScale` machinery. Kills Corbomite's
`binding()->setTheme()` escape hatch (`NoteEditorWidget.cpp:132`).

### 4.4 Find / format verbs

`attachFindController` becomes an override of the new base virtual
(implementation unchanged). Format verbs override to trigger the
existing `LiveActionController` actions (single path — the QActions
remain the implementation; the virtuals delegate to them).

## 5. Format-ops hoist (`markoff-core` ← `markoff-source`)

New public `Markoff::FormatOps` (namespace of free functions,
`include/markoff/core/FormatOps.h`):

```cpp
namespace Markoff::FormatOps {
struct QtRange { int start = 0; int end = 0; };  // UTF-16 positions over widgetFlatView
// Each op takes the document, the current flat text (the widget's
// toPlainText(), which IS widgetFlatView), and the selection/caret in
// UTF-16 positions; mutates the model via d2 primitives/applyFlatEdit;
// returns the caret/selection the leaf should re-apply after the
// binding's reverse sync. Widget-free: testable headlessly.
//
// DEVIATION (shipped, reviewed OK in Task 3): all three ops return
// std::optional<QtRange> rather than QtRange. std::nullopt means no edit
// was performed and the caller must leave its cursor untouched (matching
// the donor's early-return paths, which never called setTextCursor —
// e.g. setHeadingLevel with level == current level must not collapse an
// existing selection).
std::optional<QtRange> wrapToggle(MarkoffDocument *doc, const QString &flatText,
                                  QtRange sel, const QByteArray &delim);   // ** _ ~~ `
std::optional<QtRange> insertLink(MarkoffDocument *doc, const QString &flatText,
                                  QtRange sel);
std::optional<QtRange> setHeadingLevel(MarkoffDocument *doc, const QString &flatText,
                                       int caretQtPos, int level);
}
```

> **§5 deviation note (Task 3, reviewed-and-approved):** the shipped return
> type is `std::optional<QtRange>` for all three ops. The spec showed `QtRange`
> (for `wrapToggle`/`insertLink`) and `void` (for `setHeadingLevel`). The
> `optional` shape was adopted because the donor's early-return paths never
> called `setTextCursor` — returning a value that means "I did nothing, leave
> the cursor alone" is more correct than returning a dummy range or requiring
> callers to compare before/after. The `FormatOps.h` header is authoritative.

(Signature shape matches the existing implementation — the source
leaf's free functions already work in `(toPlainText, qtPos)` terms and
convert via `qtPosToByteOffset` + `findBlockAtSepByte` internally; the
hoist lifts, it does not rewrite.)

- Implementation is the existing logic lifted from
  `markoff-source/src/Editor.cpp:240-563`, already block-aware via
  `Detail::findBlockAtSepByte` (post queue-#8.6). It moves; it is not
  rewritten. Coordinate space is sep-view bytes over the single-`\n`
  `widgetFlatView()` — the one coordinate both widget leaves share.
- `Source::Editor`'s format ops become thin wrappers: translate
  QTextCursor selection → sep bytes (existing helpers), call FormatOps,
  re-apply returned range.
- `Styled::Editor` overrides the base verbs the same way (delimiters
  are visible in styled v0, so wrap-toggle semantics are identical to
  source). Verbs no-op with a `qWarning` when the caret is inside a
  table frame (opaque block).
- Live keeps its own implementation path (`LiveActionController` /
  per-block d2 ops) — unifying live's per-block coordinate world onto
  sep-view FormatOps is NOT attempted this arc (different coordinate
  authority; forcing it would violate the "no new authority" rule for
  a refactor with no behavioral payoff).

## 6. Styled find (`markoff-styled`)

`Styled::Detail::StyledFindAdapter`, mirroring `SourceFindAdapter`
(attach/detach, extra-selections, non-focusing `navigationRequested`
seek), with one structural difference: **positions are computed by a
frame-aware lockstep walk, not byte arithmetic.** With a `QTextTable`
frame in the document, flat-char positions diverge after the frame
(the exact divergence behind the 2026-05-31 SIGSEGV). The walk
discipline lands as a shared helper extracted from `FormatPass`
(`blockPositionWalk`: model block → QTextBlock position, skipping
frames and Qt's artifact blocks) so the adapter and FormatPass cannot
drift apart (queue's "sibling drift" class).

Degradation (documented in the class doc + CLAUDE.md): matches whose
block is a rendered table frame produce no highlight in v1 (the frame
is read-only/opaque); `FindController` still counts them and
navigation scrolls to the frame.

`Styled::Editor::attachFindController` overrides the base virtual.
This gives Corbomite's Reading mode find.

## 7. EditorContext feed

Each leaf emits `contextChanged(EditorContext)` when the caret's block
or that block's kind changes (derived state, fired from existing
change signals — it must NOT become a second cursor authority;
invariant 3):

- **Styled/Source:** recompute on `QTextEdit/QPlainTextEdit::cursorPositionChanged`
  only. ~~+ `d2DocumentChanged`~~ — **deviation (shipped, Tasks 10/12):**
  `d2DocumentChanged` is intentionally NOT connected on these two leaves.
  Reason: `d2DocumentChanged` fires during the `StyleApplier`'s deferred
  format-only pass (triggered by the syntax highlighter's `contentsChange`
  notifies that reach `d2DocumentChanged` via the binding's no-op edit path).
  Connecting it would false-fire during those no-op passes and defeat the
  change-gate, resulting in stale-context spam with no real block change.
  Cursor-position-based triggering is sufficient: in practice every structural
  key (Enter, Backspace, Tab, `#`-prefix typing) that changes the block kind
  also moves the caret, so the context refreshes immediately.
  **Known low-severity gap:** a kind-change that does NOT move the caret (e.g.
  a programmatic `Cmd::changeKind` without a cursor op) leaves the emitted
  context stale until the next caret move. Tracked in queue.
- **Live:** recompute on `LiveCursorState::cursorChanged` +
  kind-transition `dataChanged` (the model emits `dataChanged` for the
  affected row on every `Cmd::changeKind` in `onD2Changed`): kind from the
  model record; `inTable`/`tableRow`/`tableCol` from `TableEditBinding` when
  the caret is in a cell. Live does not have the source/styled staleness gap.
- Emission is change-gated (compare against last emitted struct) so
  per-keystroke no-op emits don't spam the host toolbar.

`EditorContext` gains `BlockKindNames::Table` (exists as a kind in the
model but not in the names list) — additive.

## 8. Source fontScale

`Source::Editor` implements `setFontScale`: scales the inner
QPlainTextEdit font point size from a captured base size, rescales the
gutter font, re-applies paragraph margins. Mirrors styled's
`kBaseBodyPt × fontScale` discipline where applicable.

## 9. Signal consistency

All three leaves emit the base's `cursorPositionChanged(line, column)`
(coordinate per §3) and `scrollPositionChanged(float)` on their native
change signals. Audit of current emission happens in the plan's first
test task; missing emits are wired.

## 10. Testing & falsifiability

New per-leaf contract suites — `tst_view_contract_source`,
`tst_view_contract_styled`, `tst_view_contract_live` — asserting the
SAME behaviors (shared header of test-body templates so contracts
can't drift):

1. CursorPos round-trip incl. multi-line code block + clamping.
2. `setReadOnly(true)`: typing, Enter, paste, (live: cell edit) leave
   `serializeForSave()` byte-identical; navigation/selection/copy still
   work; `setReadOnly(false)` restores editing.
3. `undo()`/`redo()` via the BASE pointer round-trips a d2 edit; both
   no-op while read-only.
4. Find attach renders highlights at visible-text positions
   (styled: a match *after a rendered table* — the frame-aware case).
5. Theme + fontScale apply observably; signals fire.
6. Format verbs via the base pointer mutate the model identically
   across source/styled (same fixture, same expected
   `serializeForSave()`).
7. `contextChanged` fires with correct kind/heading/table fields and
   is change-gated.

Live's suite drives the QML production path via
`QmlIntegrationFixture`/`LiveRealisticInputHarness` (invariant 5).
Falsifiability proofs (invariant 4, stub-then-revert in history) for
the two seam-touching pieces: (a) live cursor mapping — break the
line-summation off-by-one, contract test must fail; (b) read-only
gates — disable one gate (structural keys), the read-only test must
fail.

## 11. Phases (→ plan tasks)

- **A** Base contract + undo/redo + theme/fontScale stores (core).
- **B** FormatOps hoist + source refactor onto it (no behavior change;
  format-ops tests stay green).
- **C** Styled: format verbs + StyledFindAdapter (+ shared frame-walk
  helper extraction).
- **D** Live: theme/fontScale forwarding; cursor mapping; read-only
  ingress gates; verb delegation.
- **E** EditorContext feed, all leaves + signal-consistency wiring.
- **F** Source fontScale.
- **G** Contract suites + falsifiability proofs (written first per
  phase where seam-touching; G is the consolidation pass).
- **H** Corbomite adoption brief + STATUS.md/queue.md updates.

## 12. Risks

- **Live cursor mapping on large docs** is O(blocks) per read — fine
  (reads are user-initiated). Do not cache (a cache is a second store).
- **Styled find before tables ships highlights** depends on the shared
  frame-walk helper — extraction must not change FormatPass behavior
  (its tests are the guard).
- **FormatOps hoist** is a move of recently-debugged code
  (queue #8.6); the existing 16/16 `tst_source_widget_format_ops` is
  the no-regression guard.
- **Read-only gates** touch the focus/caret seam's neighbors; gates
  are early-returns only — no new state machines, no reordering of
  existing signal flows.
