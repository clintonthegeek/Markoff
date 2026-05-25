# E2 — Cursor-aware view: auto-hide markers + cross-block keyboard nav

**Date:** 2026-05-08
**Branch:** `exploration/new-foundation`
**Phase:** E2 (per E-arc framing)
**Predecessor:** E1 — inline-format highlighter (`v0.7.0-e1`, 2026-05-08)
**Companion plan:** `docs/plans/2026-05-08-e2-cursor-aware-view.md` (TBW immediately after this spec)
**Tag on completion:** `v0.7.0-e2` (anticipated)

---

## 0. Inputs and prior art

- `docs/specs/2026-05-08-e-arc-framing.md` §2.E2 — constitutional framing of this phase
- `docs/specs/2026-05-08-e1-inline-highlighter-design.md` — E1 substantive design (the carrier this phase extends)
- `docs/specs/2026-04-30-live-editing-design.md` — names `"Ctrl+Home/End/arrow crossings"` in `LiveStructuralKeyHandler`'s scope (intent stated, never implemented; folded into E2 here)
- `docs/specs/2026-04-29-cross-block-selection-spike-findings.md` — hit-testing primitives reused for Page-Up/Down
- `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — `BlockKindRegistry` + per-kind cursor-variant model
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` §A.7 erratum — `inlineSpansFor` as load-bearing infrastructure

---

## 1. Scope

E2 turns Markoff Live from "inline formatting visible at all times" (E1) into a **cursor-aware view**: markers reveal under the caret and disappear elsewhere, and the keyboard navigates fluidly across block boundaries.

The phase has **two deliverables on a shared per-delegate caret-watcher substrate**:

1. **Auto-hide of inline-span markers, heading prefixes, and code-fence lines**, with true zero-width collapse — the line widens when markers reveal and shrinks when they hide. Per-kind policy locked (§4.1).
2. **Full desktop-editor keyboard navigation across blocks** — Up/Down/Left/Right with column preservation; Home/End/Ctrl+Home/Ctrl+End; Ctrl+Left/Right (word boundaries); Page-Up/Page-Down; Shift+ extends selection across blocks; Ctrl+Shift+ word-extends across blocks.

Both deliverables share infrastructure (per-delegate caret watcher, `LiveCursorState::desiredVisualX`, edge-detection logic). Splitting them into separate phases would force the same machinery to be built twice; folding them into one phase is the natural unit.

### 1.1 Why fold nav into the auto-hide phase

The framing-doc names E2 as "cursor-aware delimiter visibility (auto-hide)." Inter-block keyboard nav was named once (in `2026-04-30-live-editing-design.md` row for `LiveStructuralKeyHandler`: *"Ctrl+Home/End/arrow crossings"*) and never delivered. It is a regression-of-omission, not a new feature.

Auto-hide and cross-block nav share their substrate (per-delegate caret-position observer, `LiveCursorState`-resident column state, edge detection). Auto-hide's reveal-on-entry contract becomes degenerate at block boundaries when the user can only enter a span by clicking — typing into one block and arrowing through to the next is the dominant editing motion. Both deliverables together realize the cursor-aware view; either alone is half-built.

### 1.2 What does NOT ship in E2

- **Speculative open-delimiter highlighting** (deferred from `2026-04-30-live-editing-design.md` §1) — typing `**` does not pre-style the trailing content as bold until the parser confirms with a real inline node.
- **Visual-line-break heuristics inside wrapped paragraphs** beyond what Qt's `QTextCursor::movePosition(Up/Down)` natively supports.
- **Touch / mobile gesture nav, virtual-keyboard interactions** (E-arc out-of-scope per framing §1.2).
- **Custom a11y semantics for nav** (Qt defaults inherited; a11y arc is post-distillation per framing §1.2).
- **RTL / bidi nav semantics.** `tr()` is the i18n convention; deeper i18n is out per framing §1.2.
- **Caret broadcast latency tuning for D5.** E2 routes nav through existing `LiveCursorState` primitives, which already broadcast to peers; no D5 changes.
- **Nav inside Math `BlockInternalEdit` mode.** Pre-existing internal nav for Math stays; E2 doesn't touch it. Cross-block nav from outside the Math block continues to land it as `BlockSelected`.

---

## 2. Architecture

### 2.1 Layer placement

E2 sits across L3 (Cursor + selection) and L4 (Block editing) of the markoff-live stack documented in `libs/markoff-live/CLAUDE.md`. No L0–L2 changes. Delegate QML (L8/L7/L6) gains key forwarders and caret-change wiring; no delegate becomes structurally different.

### 2.2 Components

| Component | New / Existing | Responsibility |
|---|---|---|
| `Markoff::Live::LiveNavigationController` | **New** (C++ QObject; sibling to `LiveStructuralKeyHandler` and `LiveEditBinding`) | Edge-detection across all 11 navigation key combos, column-preservation arithmetic, word-boundary computation across blocks, page-jump arithmetic via `LiveView.hit()`, Shift-extend selection routing through `LiveSelectionView`. |
| `Markoff::Live::InlineHighlighter` | **Extend** (E1 baseline) | Gains `setLocalCaretPosition(int qtPos)` slot. `highlightBlock` re-runs cheaply on caret motion, painting marker chars with `Theme::HiddenMarker` format when no containing span includes the caret, normal kind format when a span contains/abuts. |
| `Markoff::Live::InlineHighlighterAttached` (QML shim) | **Extend** | Adds `caretPosition` property bound to delegate's `TextEdit::cursorPosition`. Plumbs through to the C++ highlighter. |
| `Markoff::Live::LiveCursorState` | **Extend** | New `qreal desiredVisualX` field — the cross-block column-preservation state. Cleared on Left/Right or on click (`Q_INVOKABLE clearDesiredVisualX()`); set by `LiveNavigationController` before each Up/Down cross. Read by the controller when computing target qtPos in the destination block. |
| `Markoff::Theme` | **Extend** | New slot `HiddenMarker` — a `QTextCharFormat` template carrying the negative letter-spacing that absorbs marker glyph advance. Theme-defined so dark/light themes can vary if needed. Default value: `QFont` with `letterSpacing(QFont::AbsoluteSpacing, -<measured-glyph-width>)`. See §2.4 for the implementation-risk note. |
| Per-delegate QML (Paragraph / Heading / CodeBlock / ListItem / Blockquote) | **Extend** | `Keys.onPressed` forwarded set grows from `{Return, Enter, Esc, Backspace, Delete}` to also include `{Up, Down, Left, Right, Home, End, PageUp, PageDown}` plus their Ctrl- and Shift-prefixed combos. Forwarding goes to `LiveNavigationController::tryHandle` rather than `LiveStructuralKeyHandler::tryHandle`. `cursorPositionChanged` and `selectionChanged` on the inner `TextEdit` wire to `inlineHighlighter.setLocalCaretPosition` (and `setSelection` for selection-cover reveal). |
| Per-delegate QML (HR / Image / Math) | **Extend (light)** | Existing structural Up/Down/Delete handlers stay. Add Shift-extend forwarding so Shift+arrow at non-text blocks calls `LiveSelectionView::extend()`. No other behavioral change. |
| `LiveView.qml` | **No change** | Existing `Keys.onPressed` (Ctrl+C only) stays. Hit-testing machinery (`hit()` lines 63–108) is reused by `LiveNavigationController` for Page-Up/Down. |

### 2.3 What does NOT change

- `LiveStructuralKeyHandler` keeps its current scope (Enter / Backspace-merge / Delete-merge / Tab-promote / Esc / structural transitions). Nav is its sibling, not its extension. Rationale: "structural" denotes "mutates document structure"; nav doesn't mutate structure, so conflating them dilutes the abstraction. The dispatch tables stay focused.
- `LiveEditBinding` is untouched. E2 is read-side (caret-position observer) and routing-side (key forwarding); it never mutates the document.
- `LiveSelectionView` is untouched at the API level — `extend(blockIndex, qtPos)` already exists for mouse drag; keyboard Shift-extend calls it the same way.
- `LiveListModelBinding` is untouched.
- `MarkoffDocument` is untouched. **No new `Cmd::*` ops** because nothing in E2 mutates document state.

### 2.4 Implementation risk: zero-width marker rendering

Qt's `QTextCharFormat` does not have a native "hidden" property that affects layout. The cleanest path to true zero-width collapse is **negative `QFont::letterSpacing` in `AbsoluteSpacing` mode**, sized to absorb each marker glyph's advance.

**Risk:** glyph advance is font- and char-dependent. `**` has different advance than `__`; `==` differs from both; `[` and `]` differ from each other. The implementation must measure per-marker glyph advance via `QFontMetrics::horizontalAdvance(ch)` and apply the negative letter-spacing in matching units.

**Mitigation strategy (to confirm during plan execution, not now):**

1. **Primary path:** per-marker `QTextCharFormat` with `font.setLetterSpacing(QFont::AbsoluteSpacing, -metrics.horizontalAdvance(ch))`. Validate in `tst_live_render_e2_perf_caret_move` that resulting line widths match expected post-collapse widths within ±1px.
2. **Fallback A:** if negative letter-spacing alone leaves residual width on some platforms, combine with `font.setStretch(1)` (1% width font stretch).
3. **Fallback B (last resort, breaks Approach 3):** introduce a per-delegate display-buffer `QTextDocument` that omits hidden chars; route edits through a translation layer. This re-introduces the gap-buffer-style mapping the live-editing-design explicitly avoided. **The spec does not authorize Fallback B without an explicit user decision.** If both Primary and Fallback A fail, the implementation halts and surfaces the question.

The plan must include a short measurement step before Phase B implementation begins.

---

## 3. Data flow

### 3.1 Flow A — caret motion → reveal/hide (intra-block)

```
TextEdit::cursorPositionChanged(qtPos)
  → InlineHighlighterAttached.caretPosition = qtPos       (QML binding)
  → InlineHighlighter::setLocalCaretPosition(qtPos)        (C++ slot)
      → containingSet = {span ∈ inlineSpans :
                         qtPos ∈ [span.start - 1, span.end + 1]}
      → diff = symmetricDifference(prevContainingSet, containingSet)
      → for each span in diff: rehighlightBlock for span.range
        (or, simpler: rehighlightBlock once if diff non-empty)
      → prevContainingSet = containingSet
```

Cost = O(|inlineSpans in block|) for the containment scan, O(|diff|) for the re-highlight. Both bounded; on typical blocks the count is small (1–10 spans).

`highlightBlock` is unchanged in shape from E1: char-by-char iteration applying `QTextCharFormat`s. The new wrinkle: when iterating a marker char (a SourceSpan flag with `isDelimiter == true` per the SourceSpan model), if the span containing it is NOT in `containingSet`, apply `Theme::HiddenMarker` instead of the kind's normal format. Content chars are unaffected; their format is unchanged from E1.

### 3.2 Flow B — arrow key at block edge → cross-block crossing

```
TextEdit::Keys.onPressed(Qt.Key_Up, modifier=NoModifier)
  → forward to LiveNavigationController::tryHandle(
        key=Up, mods=None, blockIndex, qtPos, edit, cursorRect)
      → if not at visualTopLine(edit, qtPos):
          return NotHandled       (TextEdit handles intra-block Up)
      → at edge:
          desiredX = cursorRect.x   (delegate-local coords)
          cursorState.desiredVisualX = desiredX
          targetRow = previousNavigableRow(blockIndex)
          if targetRow < 0: return Handled (no-op, top of doc)
          if blockKind(targetRow) is text-bearing:
              cursorState.requestTextCaretAtRow(
                  targetRow,
                  qtPosFromVisualX(targetRow, desiredX,
                                    "lastVisualLine"))
          else:  // HR / Image / Math
              cursorState.requestBlockSelected(targetRow)
          return Handled
```

Visual-top-line detection: `edit.cursorRectangle().y < edit.lineHeight + epsilon` (the cursor's Y-coord is at or above the first visual line). Symmetric for visual-bottom-line: `cursorRectangle().y + cursorRectangle().height > edit.contentHeight - lineHeight - epsilon`.

`qtPosFromVisualX(targetRow, desiredX, line)` first focuses the target delegate's `TextEdit` (via the existing `cursorState.requestTextCaretAtRow` resolution path which already calls `focusEditAt`), then computes `targetEdit.positionAt(desiredX, lineY)` where `lineY` is `0` for first-visual-line or `targetEdit.contentHeight - lineHeight/2` for last-visual-line. The existing `LiveView.qml` `Connections` on `cursorState.cursorChanged` (lines 120–137) handles the focus routing.

`previousNavigableRow` walks backward from `blockIndex - 1` accepting any block (text-bearing OR non-text — non-text blocks land as BlockSelected per the existing pattern). Symmetric for `nextNavigableRow`.

Left/Right at qtPos 0 / length:

```
TextEdit::Keys.onPressed(Qt.Key_Left, modifier=NoModifier)
  → if qtPos > 0: NotHandled          (TextEdit handles)
  → at qtPos 0:
      cursorState.clearDesiredVisualX()
      targetRow = previousNavigableRow(blockIndex)
      if targetRow < 0: Handled (no-op)
      if text-bearing:
          cursorState.requestTextCaretAtRow(
              targetRow, model.recordAt(targetRow).text.length())
      else:
          cursorState.requestBlockSelected(targetRow)
      return Handled
```

### 3.3 Flow C — Shift+arrow extending selection

`LiveNavigationController::tryHandle(key, modifiers=Shift, ...)` computes the target `(blockIndex, qtPos)` exactly as Flow B does, but instead of calling `requestTextCaretAtRow`, it calls:

```
liveBinding.selectionView.extend(targetRow, targetQtPos)
```

`LiveSelectionView::extend` already exists (used by mouse drag at `LiveView.qml:217-219`). The selection-rendering pipeline draws the cross-block highlight with no further change. The local caret position itself moves with the selection (the active end of the selection IS the caret), so `cursorState.requestTextCaretAtRow` is also called *after* `extend` to position the caret at the new active end.

Ctrl+Shift+Left/Right: same shape as Flow C, but target computation uses word-boundary logic from Flow E.

### 3.4 Flow D — Ctrl+Home/End, Page-Up/Down

**Ctrl+Home:** target = first text-bearing block; qtPos = 0. Walk forward from row 0 until a text-bearing block is found (skipping leading HR or Image rows if any). Emit `requestTextCaretAtRow(targetRow, 0)`.

**Ctrl+End:** symmetric — last text-bearing block; qtPos = its text length.

**Page-Up:**

```
currentY = absolute Y of caret in viewport coords
targetY = currentY - viewport.height
hit = LiveView.hit(probeX = currentX, probeY = targetY)
if hit:
    if text-bearing: requestTextCaretAtRow(hit.blockIndex, hit.qtPos)
    else: requestBlockSelected(hit.blockIndex)
```

**Page-Down** symmetric. The `LiveView.hit()` machinery already handles realized vs unrealized delegates and gap-walking (see `LiveView.qml:63-108`); reusing it is the only correct path.

### 3.5 Flow E — Ctrl+Left/Right (word boundary across blocks)

```
TextEdit::Keys.onPressed(Qt.Key_Left, modifier=ControlModifier)
  → if not at qtPos 0:
      prevWordPos = delegate.computePrevWordPos(qtPos)
      requestTextCaretAtRow(blockIndex, prevWordPos)
      return Handled
  → at qtPos 0:
      cursorState.clearDesiredVisualX()
      targetRow = previousNavigableRow(blockIndex)
      if not text-bearing or targetRow < 0: route as Flow B Left
      else:
          targetQtPos = targetDelegate.computePrevWordPos(targetText.length)
          requestTextCaretAtRow(targetRow, targetQtPos)
      return Handled
```

The word-boundary computation is delegate-internal — Qt's `QTextCursor::movePosition(QTextCursor::PreviousWord)` is the source of truth. Each text-bearing delegate exposes `Q_INVOKABLE int computePrevWordPos(int qtPos)` and `Q_INVOKABLE int computeNextWordPos(int qtPos)` that wrap a `QTextCursor` constructed against the TextEdit's `QTextDocument`. The controller calls these through `LiveBlockModel`'s row-keyed delegate lookup (existing `LiveView::itemAtIndex` pattern).

### 3.6 No new collab/D5 contracts

Every E2 caret motion routes through `LiveCursorState::requestTextCaretAtRow` or `requestBlockSelected`, both of which already broadcast cursor position to peers. Selection extension routes through `LiveSelectionView::extend`, the same primitive mouse drag uses. No new `Cmd::*` ops, no new sibling-causal-LWW maps, no new D5 conformance work.

---

## 4. Policy matrices

### 4.1 Auto-hide policy by inline kind

All kinds: selection touching the span anywhere → markers reveal regardless of caret position (selection should never lie about source).

| Kind | Markers | Caret-out behavior | Reveal trigger | Notes |
|---|---|---|---|---|
| Bold | `**` `__` | Hidden, zero-advance | qtPos ∈ [span.start - 1, span.end + 1] | Symmetric pair |
| Italic | `*` `_` | Hidden | Inside / adjacent | Symmetric pair |
| Strikethrough | `~~` | Hidden | Inside / adjacent | Symmetric pair |
| Highlight | `==` | Hidden | Inside / adjacent | Symmetric pair |
| Inline code | `` ` `` | Hidden | Inside / adjacent | Symmetric pair |
| Link | `[`, `]`, `(`, URL, `)` | Hidden — only display text shown | Caret anywhere in `[…](…)` range OR adjacent → reveal **all** of `[`, `]`, `(`, URL, `)` atomically | Atomic — no per-portion subdivision |
| Wikilink | `[[`, `]]`, optional `\|` + Page | Hidden — alias (or Page if no alias) shown | Caret anywhere in `[[…]]` range OR adjacent → reveal full source atomically | Same atomic shape as Link |
| Tag | `#` | **Always shown** | n/a | `#` is the tag's visual identity; never auto-hides |

### 4.2 Auto-hide policy by block kind

| Block kind | Prefix marker(s) | Auto-hide behavior |
|---|---|---|
| Heading 1–6 | `#`, `##`, … (+ trailing space) | Hides when caret outside the heading block; reveals when caret enters the block (any position within) |
| Paragraph | (none) | n/a |
| Code block (fenced) | ```` ``` ```` opening line (with optional language tag), ```` ``` ```` closing line | Marker chars on both fence lines collapse to zero advance when caret outside the block; reveal when caret enters block (any position). The fence lines themselves remain present (one empty visual line each when collapsed). Whether to collapse the line height entirely is an open question §9.Q1. |
| List item | `-` / `*` / `1.` (+ space) | **Always shown** — list bullets are structural rendering, not redundant markup |
| Blockquote | `>` (+ space, possibly nested `> >`) | **Always shown** — `>` produces the quote indent/bar, which is the rendering itself |
| Horizontal rule | `---` | n/a — entire block is its own visual; no markers to hide |
| Image / Math | `![…](…)` / `$$…$$` | Out of E2 scope — these become BlockSelected when caret enters; rendering is delegate-internal |

### 4.3 Nested spans

Reveal-set = every span S such that `qtPos ∈ [S.start - 1, S.end + 1]`. If caret is in italic-inside-bold, both spans satisfy the predicate and both reveal. The InlineHighlighter computes this set on every caret-position-change and re-highlights any spans whose membership changed since the previous caret position.

### 4.4 Peer cursor

Auto-hide watches **local caret only**. Remote-cursor positions broadcast by D5 do not trigger reveal. This satisfies the framing-doc §5 invariant: *"E2's auto-hide reveals on local caret entry only, not on peer cursor entry."* The implementation: `InlineHighlighter::setLocalCaretPosition` is wired only from the local `TextEdit::cursorPositionChanged`, never from `LiveCursorState`'s peer-cursor stream.

### 4.5 Navigation policy by key

| Key combo | At intra-block position | At block edge |
|---|---|---|
| Up | Move up one visual line (Qt default) | Cross to prev navigable block; caret at column matching `desiredVisualX` on prev block's last visual line |
| Down | Move down one visual line | Cross to next navigable block; caret at column matching `desiredVisualX` on next block's first visual line |
| Left | Move one char left | At qtPos 0 → cross to prev navigable block; caret at end of prev block's text (qtPos = length). Clears `desiredVisualX` |
| Right | Move one char right | At qtPos length → cross to next navigable block; caret at qtPos 0 of next block. Clears `desiredVisualX` |
| Home | Start of visual line (Qt default) | n/a — no-op at qtPos 0 |
| End | End of visual line | n/a — no-op at qtPos length |
| Ctrl+Home | (no intra-block override) | Caret at qtPos 0 of first text-bearing block |
| Ctrl+End | (no intra-block override) | Caret at end of last text-bearing block |
| Ctrl+Left | Previous word boundary in block | At qtPos 0 → cross to prev block; word-boundary going backward from end |
| Ctrl+Right | Next word boundary in block | At qtPos length → cross to next block; word-boundary going forward from start |
| Page Up | (no intra-block override) | Hit-test via `LiveView.hit(probeX, currentCaretY - viewportHeight)`; place caret/selected at result |
| Page Down | (no intra-block override) | Hit-test at `(probeX, currentCaretY + viewportHeight)` |
| Shift + (any of above) | Same target computation; result calls `LiveSelectionView.extend(target)` instead of `requestTextCaretAtRow` (caret follows the active end) | |
| Ctrl+Shift+Left/Right | Same as Ctrl+Left/Right; result extends selection | |

**`desiredVisualX` lifecycle:** set by the controller before each Up/Down cross; cleared on Left/Right, on Home/End, on click (`LiveView.qml:onPressed`), on any non-vertical-arrow keystroke. Persisted across runs of consecutive Up or Down so e.g. "Up Up Up" through three blocks of varying width returns to the same target column.

**Crossing through non-text blocks:** First press lands the caret on the non-text block as `BlockSelected`. Second press of the same direction continues to the next text-bearing block (existing HR `Up`/`Down` handler at `LiveStructuralKeyHandler.cpp:374-379` already does this; new arrow-nav from text blocks composes with the existing primitive).

---

## 5. Acceptance criteria

### 5.1 Auto-hide

1. With no caret in any span and no selection touching the span, every inline marker (per §4.1) renders with zero glyph advance — the line's pixel width matches the line width without those marker chars within ±1px tolerance.
2. With caret inside a span, all that span's markers render with the kind's normal `QTextCharFormat` (matching E1's appearance with markers visible).
3. With caret immediately before opening marker (qtPos == span.start - 1) or immediately after closing marker (qtPos == span.end + 1), markers reveal.
4. Caret crosses span boundary by typing or by arrow key → markers reveal/hide synchronously with caret motion (no perceptible lag).
5. Selection partially or wholly covering a span → that span's markers reveal regardless of caret position.
6. Nested spans: caret inside italic-inside-bold → both italic AND bold markers reveal.
7. Peer cursor (broadcast by D5) inside a span → markers do NOT reveal. Local caret moving into the same span does reveal.
8. Heading: caret in another block → `#` prefix hidden (line widens to start at the heading text). Caret enters heading → `#` reveals.
9. Code block: caret outside → both fence lines collapse markers to zero advance (line still present, content empty). Caret enters block → fence content (including language tag) reveals.
10. List item bullet, blockquote `>`, tag `#`: visible at all times regardless of caret.
11. Switching between Theme variants does not regress §5.1.1 (each theme defines its own `HiddenMarker` slot).

### 5.2 Navigation

12. Up at visual-top-line of a paragraph crosses to the previous block; caret lands at the visual-X column matching where it left, on the prev block's last visual line.
13. Down at visual-bottom-line crosses to next block; caret lands at matching column on first visual line.
14. Left at qtPos 0 crosses to prev block, caret at end-of-text. Right at qtPos length crosses to next block, caret at qtPos 0.
15. Three consecutive Up presses through blocks of varying width return the caret to within ±1 column of the original position.
16. Single Left or Right keystroke clears `desiredVisualX`; subsequent Up does NOT inherit the pre-Left column.
17. Click clears `desiredVisualX`; subsequent Up uses the click position as the column source.
18. Ctrl+Home places caret at qtPos 0 of the first text-bearing block (skipping any leading HR/Image).
19. Ctrl+End places caret at end of last text-bearing block.
20. Ctrl+Left at qtPos 0 crosses to prev block and lands at the previous word boundary going backward from end-of-text.
21. Ctrl+Right at qtPos length crosses to next block and lands at the next word boundary going forward from qtPos 0.
22. Page-Up jumps the caret by approximately the viewport height (using `LiveView.hit()`); ListView scrolls so the caret remains visible.
23. Page-Down symmetric.
24. Shift+Down at visual-bottom-line of block N extends `LiveSelectionView` to (N+1, qtPosAtMatchingColumnTopLine); rendered selection shows the cross-block highlight.
25. Shift+Down twice from middle of block N produces a selection spanning block N tail + block N+1 + block N+2 head.
26. Shift+Ctrl+Left at qtPos 0 extends selection to the previous block's last word.
27. First Up/Down press through an HR or Image lands the caret on it as `BlockSelected`. Second press continues to the next text-bearing block.
28. Math block: Up/Down from outside lands as `BlockSelected`. Internal-edit mode behavior is unchanged (pre-existing).
29. Selection clears on plain (unmodifier) arrow keys when the keystroke causes caret motion.
30. No D5 invariants broken: cursor and selection positions broadcast the same way mouse-driven cursor and selection do today.

### 5.3 Performance

31. Per-keystroke cost (typing benchmark from E1) does not regress: p99 < 33ms CI gate, < 16ms dev aspiration. E2's `setLocalCaretPosition` adds zero work when caret is not entering or leaving a span.
32. Per-caret-move cost in a 100-block doc with 10 spans per block: p99 < 5ms regression guard, < 1ms target.
33. Cross-block arrow keystroke (full focus transition + caret placement + auto-hide reflow on both blocks) p99 < 16ms.

### 5.4 Dogfood (gating tag)

34. Manual dogfood pass on `markoff-live-app` against a representative document containing all 8 inline kinds, headings, lists, blockquotes, code blocks, HR, image: every acceptance criterion 1–33 manually verified in a single editing session before tagging `v0.7.0-e2`.

---

## 6. Test surface

Test executables follow E1 naming: `tst_live_render_e2_*`. Tests live under `libs/markoff-live/tests/` and link the markoff-live target.

| Test file | Coverage |
|---|---|
| `tst_live_render_e2_autohide_per_kind` | Per-inline-kind reveal/hide for 8 kinds × 4 text-bearing delegates (paragraph/heading/list-item/blockquote). Tag = always-shown invariant. |
| `tst_live_render_e2_autohide_caret_adjacent` | Caret at qtPos = span.start-1, span.start, span.start+1, span.end-1, span.end, span.end+1 → reveal/hide expected per §4.1. |
| `tst_live_render_e2_autohide_nested` | Italic-in-bold, code-in-italic, link-with-bold-display-text — both containing spans reveal when caret inside inner. |
| `tst_live_render_e2_autohide_selection` | Selection partially covering span / fully spanning span / spanning multiple blocks → all touched spans' markers visible regardless of caret. |
| `tst_live_render_e2_autohide_block_prefix` | Heading `#` hides on caret-out, reveals on caret-in. Code fence markers collapse on caret-out. List bullet and blockquote `>` always shown invariants. |
| `tst_live_render_e2_autohide_peer_cursor` | Setting a peer cursor inside a span via the D5 hook does NOT trigger reveal. Local caret independently triggers. |
| `tst_live_render_e2_nav_arrows` | Up/Down/Left/Right at each edge of each text-bearing kind. Crossing into HR/Image yields BlockSelected; second press continues. |
| `tst_live_render_e2_nav_column_preservation` | Up across 3 blocks of varying width → caret lands at same `desiredVisualX` on each. Down then Up returns to within ±1 column of origin. Left/Right clears `desiredVisualX`. |
| `tst_live_render_e2_nav_word` | Ctrl+Left at qtPos 0 → previous block, last word boundary. Ctrl+Right at qtPos length → next block, first word boundary. |
| `tst_live_render_e2_nav_home_end` | Home / End intra-block (visual-line). Ctrl+Home → first text block start. Ctrl+End → last text block end. Ctrl+Home skips leading HR. |
| `tst_live_render_e2_nav_page` | Page-Up / Page-Down using `LiveView.hit()` machinery; viewport-relative; respects scroll. |
| `tst_live_render_e2_nav_shift_extend` | Shift+arrow at edge → `LiveSelectionView::extend()` called with correct target. Shift+Down twice from middle of block 1 → selection covers block 1 tail + block 2 + block 3 head. Shift+Ctrl+Left/Right word-extend. |
| `tst_live_render_e2_perf_caret_move` | 100-block doc, 10 spans/block. Caret motion: p99 < 5ms, target < 1ms. |
| `tst_live_render_e2_perf_typing_no_regression` | Re-runs E1's typing benchmark with E2 wired. p99 < 33ms (CI gate). |

Cumulative test pack pattern from E1 stays — E2 tests build on top of E1 tests; both run together.

---

## 7. Performance envelope

- **Caret-move cost** = O(|inlineSpans in block|) for the containment scan + O(|spans in symmetric difference of prev and current containing sets|) for re-highlight. On a typical 10-span block, intra-block caret motion costs 1–2 span re-formats. Cross-block costs the same per-block, doubled (re-highlight of leaving and entering blocks). Target p99 < 1ms; regression guardrail < 5ms.
- **Typing cost** — E2 adds zero work per-keystroke when the caret is not entering or leaving a span. When typing crosses a span boundary, the cost is identical to a caret move (above). Typing benchmark from E1 must not regress: p99 < 33ms CI gate, < 16ms dev aspiration.
- **Memory** — `LiveCursorState` gains one `qreal` (`desiredVisualX`). `Theme` gains one `QTextCharFormat` slot (`HiddenMarker`). Per-delegate overhead unchanged. Containing-span set is recomputed on each caret motion; not persisted.
- **Cross-block focus transition** — already exercised today by mouse click (`LiveView.qml:onPressed` → `requestTextCaretAtRow` → `Connections.onCursorChanged` → `focusEditAt`). Keyboard cross-block adds no new latency on this path.

---

## 8. Subtractability note (E-arc framing §5 invariant)

Per the E-arc framing-doc §5 invariant, every E-phase ships a "subtractability note" — how would a view that doesn't need this capability avoid linking it / instantiating it / paying its runtime cost?

| E2 capability | Subtraction path |
|---|---|
| Auto-hide reveal/hide | **Omit** — don't attach `cursorPositionChanged` to `InlineHighlighter::setLocalCaretPosition`. Markers stay always-visible. **Source mode is the canonical inversion**: `markoff-source` always shows markers because plaintext editors render their source. |
| Cross-block keyboard navigation | **Omit** — don't instantiate `LiveNavigationController`; don't extend per-delegate `Keys.onPressed` to forward arrow/Home/End/PgUp/PgDn keys. Each delegate's TextEdit becomes a sealed-edge editor (intra-block nav only). |
| `Theme::HiddenMarker` slot | **No-op default** — every theme defines it as the same `QTextCharFormat` as the kind it would replace (i.e., format unchanged). Cost: one extra `QTextCharFormat` per theme. Effectively free. |
| `LiveCursorState::desiredVisualX` field | **Vestigial when nav absent** — costs one `qreal` per cursor-state instance. Unused if no cross-block nav source writes to it. Removable from `LiveCursorState` if footprint matters; the spec keeps it on `LiveCursorState` because that's where cursor state belongs and the cost is negligible. |

Two existing/future views demonstrate the subtraction:

- **Source view** (`markoff-source`, present): inverts E2's auto-hide (markers always visible by construction — `QPlainTextEdit` shows source bytes); cross-block nav doesn't apply because `QPlainTextEdit` is single-document. Both deliverables are subtracted by virtue of architectural shape.
- **Future Reading view**: subtracts both — read-only, no caret means neither reveal nor cross-block nav needs to instantiate.

For E6 distillation, this means E2 contributes:

- A *capability-matrix row*: "cursor-aware reveal" — required in live-render, inverted in source, omitted in reading.
- A *capability-matrix row*: "cross-block keyboard nav" — required in live-render, omitted in source (single-document), omitted in reading (no caret).

---

## 9. Open questions

| # | Question | Resolution path |
|---|---|---|
| Q1 | Code-fence "line collapse" semantics — should the fence lines collapse to zero height (block-above butts directly against block-below) or stay as one empty visual line each? Current spec: zero-advance markers, line stays present. If dogfood feels wrong, the fix is a delegate-side `visible: !isCaretOut` toggle on the fence Item. | Resolve at dogfood (Phase G). |
| Q2 | Negative-`letterSpacing` zero-width mechanism robustness across fonts/themes. Primary path may need `setStretch(1)` combination on some platforms. Fallback B (display-buffer) is unauthorized without explicit user decision. | Resolve at start of Phase B (measurement step in plan). |
| Q3 | Visual-edge detection precision when a paragraph is on its first/last visual line but Qt's `cursorRectangle().y` returns slightly off-pixel values. Epsilon tolerance for "is at edge" check — start with `lineHeight * 0.5` epsilon, tighten if false positives observed. | Resolve at Phase B implementation. |
| Q4 | Word-boundary delimiter set for Ctrl+Left/Right cross-block. Use Qt's default (`QTextCursor::PreviousWord` / `NextWord`)? Or a markdown-aware tokenizer that treats `**`, `_`, etc. as separate tokens? Default: Qt's default. | Resolve at Phase B; change only if dogfood surfaces unexpected jumps. |
| Q5 | Page-Up/Down — should the caret land at the same visual-X column as the source (column-preserved across page jumps)? Current spec: yes (uses `currentX`). Verify dogfood feels right. | Resolve at Phase G dogfood. |

---

## 10. Conventions inherited

- **Typing perf budget** from E1 is a hard CI gate; E2 cannot regress.
- **Build cap `-j 8`** per CLAUDE.md.
- **TDD per task** — failing test → run (red) → impl → run (green) → commit.
- **Phase ends with green-tree + commit; full ctest at phase end.**
- **Closeout (final phase) requires user dogfood pass before tagging.**
- **Tag on completion:** `v0.7.0-e2`, on `exploration/new-foundation`. Append-only history. Recent-changes log entry in `docs/e-arc/e-arc-status.md`.

---

## 11. Phase-board entry to land on spec approval

Update `docs/e-arc/e-arc-status.md` phase-board E2 row from `pending` to `spec-approved`; add recent-changes log entry for spec landing.
