# Tier 3 — Kind-transition delegate architecture

**Date:** 2026-05-15
**Branch:** `exploration/new-foundation`
**Predecessor:** `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` (tier 2 complete).
**Plan to follow:** `docs/plans/2026-05-15-tier-3-kind-transition-delegate-architecture.md` (to be written).

## 0. One-paragraph summary

Retires the `kindOnlySwap` + `beginResetModel` workaround in
`LiveBlockModel::applyOps` and the contentY save/restore bandaid in
`LiveView.qml`. Replaces both with **stable-row-identity for
within-class kind changes**: `BlockKey` switches from `{kind,
blockAnchor}` to `{delegateClass, blockAnchor}`, where `delegateClass`
is a coarser bucket over kinds. The five-bucket dispatcher means
paragraph↔heading↔blockquote↔list-item transitions emit
`dataChanged({KindRole, …})` instead of Delete+Insert; the row's
delegate is not destroyed, its `TextEdit` is not destroyed, focus is
preserved by Qt without chokepoint round-trip. The flicker and
"throws me about" symptoms from the 2026-05-15 dogfood retire at the
root. Cross-block paste with a heading renders as a heading by the
same path. Pre-authorized by `docs/queue.md:63` ("single delegate
that internally renders by kind").

## 1. Motivation

The 2026-05-15 dogfood (tier-2 completion handoff §"Dogfood Findings")
named two regressions:

1. **Typing `#` or `-` jumps the view to top.** The bandaid in
   `d92e1e2` softened it (synchronous + deferred `contentY` restore)
   but a one-frame `contentY=0` still renders, perceived as a
   "scroll up then back" flash. Successive transitions still feel
   unstable.
2. **Cross-block paste loses header styling.** Paste of `# header`
   into a paragraph hides the hash marks (inline highlighter works)
   but renders at paragraph font size, not heading. The kind role
   updates on the model side; the delegate does not.

`docs/queue.md:63` (Discipline Log, 2026-05-11) names the load-bearing
cause and the resolution direction:

> `applyOps` now detects a Delete+Insert-at-same-row kind-change
> pattern and synthesises `beginResetModel`/`endResetModel` because
> `DelegateChooser` won't swap delegate type on `dataChanged` and
> pool-reuses the old template under plain rowsRemoved+rowsInserted.
> Heavy hammer; works but is the kind of pattern-match workaround
> that papers over a deeper L1/L2 modelling question. Worth
> revisiting if the chooser ever gains explicit kind-swap support,
> or **if we move to a single delegate that internally renders by
> kind.**

The D3 spec (`docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`
premise 3) introduced view-driven kind-transition detection and
explicitly deferred the descriptor-driven shape to "D4/later." That
"later" is now. The R-arc never planned for kind transitions at all
(R5 covered structural keys but not prefix promotion); the deferred
Loader-dispatch plan in `BlockKindDescriptor.h:21-23` referred to
plugin-registered kinds, not the built-in delegate-swap problem this
spec addresses.

Tier 3 is the architectural correction. It targets one specific L1/L2
modelling lie — kind as part of row identity — and retires the
workarounds that exist solely to mask the consequences.

## 2. Scope and explicit non-goals

### 2.1 In scope

- **`BlockKey` redefinition.** From `{kind, blockAnchor}` to
  `{delegateClass, blockAnchor}` in `LiveListModelBinding`. Within-
  delegateClass kind changes produce Equal ops in the diff instead
  of Delete+Insert.
- **`LiveBlockModel::applyOps` simplification.** Retire the
  `kindOnlySwap` detector and the `beginResetModel` branch (lines
  106–162). Within-class kind changes flow through the Equal op's
  `dataChanged` path with the kind role included in the hint list.
- **New `UnifiedInlineTextDelegate.qml`.** Single delegate component
  serving paragraph, heading, blockquote, and list-item kinds. Holds
  a persistent `TextEdit`; ornaments (list marker, blockquote bar,
  heading-level styling) bind conditionally on `model.kind`. The
  `TextEdit` instance is stable across within-class kind transitions.
- **New `delegateClass` model role.** Derived from `kind` via a
  small switch (see §5.4). Stable for the four text-inline kinds;
  distinct for `code-block`, `math`, `hr`, `image`.
- **`LiveView.qml` dispatch update.** `DelegateChooser.role` changes
  from `"kind"` to `"delegateClass"`; choices reduce from 8 to 5.
  The contentY save/restore `Connections` block (the `d92e1e2`
  bandaid) is deleted in the same commit that lands the dispatcher
  change — there is no interim regression of the user-visible scroll
  bug.
- **Existing kind-specific delegates retire.** `ParagraphDelegate.qml`,
  `HeadingDelegate.qml`, `BlockquoteDelegate.qml`, and
  `ListItemDelegate.qml` are superseded by `UnifiedInlineTextDelegate`
  and deleted. `CodeBlockDelegate.qml`, `MathDelegate.qml`,
  `HorizontalRuleDelegate.qml`, `ImageDelegate.qml` remain.
- **Falsifiable invariant test** (per invariant 4): asserts that the
  `TextEdit` `QQuickItem *` at row N is the **same object pointer**
  before and after a within-class kind transition. Land first, prove
  falsifiable, then implement.

### 2.2 Explicit non-goals

- **Code-block** stays in its own delegate. It has a different
  rendering model (monospace, no inline format pass, syntax-
  highlighting overlay). Folding it into `UnifiedInlineTextDelegate`
  would inflate that delegate's complexity past justification.
  Within-class kind changes between code-block and the text-inline
  kinds remain a Delete+Insert (rare in practice; not the dogfood
  bug).
- **Math** stays separate. `BlockInternalEdit` mode is its own seam.
- **HR and Image** stay separate. They're block-only (no `TextCaret`).
- **Plugin-registered kinds.** Still deferred per
  `BlockKindDescriptor.h:21-23`. The new `delegateClass` dispatcher
  is a forward step toward Loader-based plugin dispatch, but tier 3
  doesn't ship it.
- **The 5 baseline failing tests** (`shift_enter_creates_visible_newline`,
  `enter_at_paragraph_end_migrates_focus`, etc.) are orthogonal per
  the §7 survey; tier 3 does not target them.
- **`tryResolvePending` validation duplication** (queue.md:64). Open
  thread for a future tier.

## 3. The L4 / ownership decision (per invariant 2)

L4 block-content authority is **unchanged**: model wins on structural
events; delegate leads on transient (per memory
`feedback_invariant_promotion_discipline`). Tier 3 does not touch this.

The new decision is **delegateClass authority over row identity**:

- **The model exposes `delegateClass` as a derived role.** Its value
  is a pure function of `kind` (see §5.4).
- **`LiveListModelBinding` uses `delegateClass` (not `kind`) in
  `BlockKey`.** The diff therefore treats within-class kind changes
  as Equal ops, with the kind difference surfaced via `dataChanged`.
- **`LiveView.qml`'s `DelegateChooser` keys on `delegateClass`.**
  Once a row is instantiated as `text-inline`, it remains
  `text-inline` for the lifetime of the row. Any actual class change
  (text-inline → hr) is genuinely a Delete+Insert, handled by the
  pre-existing standard path.
- **`UnifiedInlineTextDelegate` renders kind reactively.** Its
  `TextEdit`'s `font.pixelSize`, `font.bold`, ornament visibility,
  etc., bind on `model.kind`. The same `TextEdit` instance smoothly
  re-renders when the kind role's `dataChanged` propagates.

**Retirement (per invariant 3).** Named explicitly:

| Retired | Reason |
|---|---|
| `LiveBlockModel::applyOps` lines 106–162 (`kindOnlySwap` detector + `beginResetModel` branch) | Stable row identity makes within-class kind change a `dataChanged`, not a Delete+Insert. |
| `LiveView.qml` lines 27–37 (8-choice DelegateChooser keyed on `"kind"`) | Replaced by 5-choice DelegateChooser keyed on `"delegateClass"`. |
| `LiveView.qml` lines 38–82 (contentY save/restore `Connections` block from `d92e1e2`) | The model reset it works around is gone. Removes 1 of 2 `Qt.callLater` sites in the library. |
| `ParagraphDelegate.qml`, `HeadingDelegate.qml`, `BlockquoteDelegate.qml`, `ListItemDelegate.qml` | Superseded by `UnifiedInlineTextDelegate.qml`. |

Per the survey in task #7: the seam-complexity gain is **narrow but
real** — 1 Qt.callLater site retires (the bandaid I just added),
plus the entire `kindOnlySwap` detector. The chokepoint, re-entrance
guards, math focus deferral, and `tryResolvePending` duplication all
remain. The benefit is **correctness** (the L1/L2 lie retires), not
broad seam simplification.

## 4. Architecture

### 4.1 Delegate dispatch

```qml
// LiveView.qml — after refactor
ListView {
    delegate: DelegateChooser {
        role: "delegateClass"
        DelegateChoice { roleValue: "text-inline"; delegate: UnifiedInlineTextDelegate {} }
        DelegateChoice { roleValue: "code-block";  delegate: CodeBlockDelegate         {} }
        DelegateChoice { roleValue: "math";        delegate: MathDelegate              {} }
        DelegateChoice { roleValue: "hr";          delegate: HorizontalRuleDelegate    {} }
        DelegateChoice { roleValue: "image";       delegate: ImageDelegate             {} }
    }
}
```

No `Connections` block on the model. No `Qt.callLater`.

### 4.2 Kind → delegateClass mapping

| kind | delegateClass |
|---|---|
| `paragraph` | `text-inline` |
| `heading` | `text-inline` |
| `blockquote` | `text-inline` |
| `list-item` | `text-inline` |
| `code-block` | `code-block` |
| `math` | `math` |
| `hr` | `hr` |
| `image` | `image` |

Within-class transitions (the common case, including the dogfood-
visible `#`/`-` typing) are `dataChanged`-only. Cross-class
transitions (paragraph→hr, paragraph→math via `$$`, paragraph→
code-block via ` ``` `) still use Delete+Insert; that path is
pre-existing and well-tested.

### 4.3 Data flow — within-class transition

Typing `# ` in an empty paragraph:

```
TextEdit::contentsChange → LiveEditBinding → D2 buffer edit
    → d2DocumentChanged → onD2Changed
        → inferBlockKind("# ") → Cmd::changeKind(Heading) → return
        → d2DocumentChanged → onD2Changed (re-spin)
            → diff: all-Equal (delegateClass unchanged: text-inline)
            → applyOps Equal branch: kind role changed
                → emit dataChanged(idx, idx, {KindRole, HeadingLevelRole, HeadingFormRole})
                    → UnifiedInlineTextDelegate's font.pixelSize re-binds
                        → TextEdit renders larger; cursorPosition preserved
```

Zero delegate destruction. Zero focus migration. Zero scroll
disturbance. No `Qt.callLater`. No `beginResetModel`.

### 4.4 Data flow — cross-class transition

Typing `---` on its own line (paragraph → hr):

```
... same up to applyOps ...
diff: Delete(text-inline/anchor)+Insert(hr/anchor) (delegateClass changed)
applyOps standard Delete+Insert path (unchanged):
    → beginRemoveRows/endRemoveRows → UnifiedInlineTextDelegate destroyed
    → beginInsertRows/endInsertRows → HorizontalRuleDelegate created
        → existing chokepoint resolves focus per §564–584 of LiveListModelBinding.cpp
```

This path is unchanged. The append-fresh-paragraph behaviour for
non-text-bearing kinds (line 579–585) remains.

## 5. Components

### 5.1 `UnifiedInlineTextDelegate.qml` (new)

Replaces `ParagraphDelegate`, `HeadingDelegate`, `BlockquoteDelegate`,
`ListItemDelegate`. Single QML file, single `TextEdit`. Conditional
ornaments via `Loader { active: root.kind === "..." }` for marker
(list-item) and bar (blockquote). Heading sizing via theme slot
lookup keyed on `kind` + `headingLevel`.

Skeleton (full file in plan):

```qml
Item {
    id: root
    property int modelIndex: index
    readonly property string kind: model.kind
    readonly property int headingLevel: model.headingLevel || 0
    readonly property var blockAnchor // captured at Component.onCompleted
    readonly property string blockText: model.text

    readonly property int themeSlot: {
        if (kind === "heading")    return liveBinding.headingSlot(headingLevel)
        if (kind === "blockquote") return Theme.Slot.Blockquote
        if (kind === "list-item")  return Theme.Slot.ListItem
        return Theme.Slot.TextDefault
    }

    Loader { active: root.kind === "list-item"; sourceComponent: listItemMarker }
    Loader { active: root.kind === "blockquote"; sourceComponent: blockquoteBar }

    TextEdit {
        id: edit
        // … bindings as before; font slot bound to root.themeSlot
        leftPadding: root.kind === "list-item" ? markerWidth + 4 : 8
        // … LiveEditBinding, InlineHighlighterAttached, etc.
    }
}
```

The crucial property: `id: edit` is one `QQuickItem`. Across a
paragraph→heading transition, the binding `font.pixelSize` re-
evaluates and the `TextEdit` resizes; the `QQuickItem *` is the same
object. Cursor, selection, focus all preserved by Qt's normal
property-change machinery.

### 5.2 `LiveBlockModel` changes

- Add `DelegateClassRole`.
- `delegateClassFor(kind)` free function in `LiveBlockModel.cpp`.
- `applyOps`:
  - Delete lines 106–162 entirely.
  - In the Equal branch (currently lines 162–191), if
    `m_rows[row].kind != merged.kind`, include `KindRole`,
    `HeadingLevelRole`, `HeadingFormRole`, `MarkerStyleRole`,
    `IndentLevelRole`, `MarkerNumberRole`, `CheckedRole` (whichever
    apply) in the role-hint list of `dataChanged`.

### 5.3 `LiveListModelBinding` changes

- `BlockKey` struct: rename `kind` field to `delegateClass`; populate
  with `delegateClassFor(kind)`.
- `onD2Changed`: the for-loop that walks ops looking for kind
  transitions is unchanged (it operates on `inferBlockKind` of text,
  not on `BlockKey`).
- The `establishFocus` re-anchor calls at lines 447–453 and 568–575
  are unchanged. For within-class transitions they're a no-op (focus
  is naturally preserved); for cross-class they're load-bearing.

### 5.4 `LiveView.qml` changes

- `DelegateChooser` keys on `"delegateClass"` per §4.1.
- `property real _lastSavedContentY`, `property int _modelResetCount`,
  and the `Connections { target: root.model … }` block are deleted.
- The remaining `LiveView.qml` content (hit-test, navigation
  controller wiring, keyboard shortcuts, mouse handling, remote
  cursor overlay, wheel zoom) is untouched.

### 5.5 `LiveCursorState` — no changes

Focus delivery for within-class transitions becomes a no-op
naturally: the delegate isn't destroyed, so `delegateGoingAway`
doesn't fire; the new delegate doesn't appear, so `delegateAvailable`
doesn't fire either. Cross-class transitions still go through the
existing chokepoint machinery.

The `tryResolvePending` validation bypass for "valid transient states"
(queue.md:64) remains an open thread.

> **2026-05-16 amendment (queue #6 closeout).** The "no changes"
> claim was wrong: `tryResolvePending`'s stale-registration check
> still compared **literal kind strings** rather than the new
> `delegateClass` identity, so any **subsequent** cross-block focus
> request targeting a row that had undergone a within-class kind
> transition would falsely bail. `m_delegates[anchor].kind` is
> frozen at the kind that was current when `Component.onCompleted`
> fired (e.g. `"paragraph"`); after a paragraph→heading transition
> the doc reports `"heading"`, the literal-kind comparison
> mismatches, and the request never resolves. The fix is a one-line
> swap to `delegateClassFor(it->kind) != delegateClassFor(currentKind)`
> plus an explicit empty-`currentKind` guard and a self-heal write of
> the registered kind once a class match is confirmed. See queue.md
> §#6 for the trace; bug surfaced via
> `tst_live_render_focus_chokepoint_invariant::nav_into_runtime_promoted_heading`,
> which was already in-tree as the falsifiable test.

## 6. Testing — supporting work

Per **invariant 4**: write the falsifiable invariant test first.

### 6.1 New test: `tst_live_render_within_class_kind_transition_preserves_textedit`

CMake target. Uses `LiveRealisticInputHarness` (per invariant 5).
Three slots:

| Slot | Setup | Assertion |
|---|---|---|
| `paragraph_to_heading_preserves_textedit_pointer` | Load doc with single paragraph "hello"; record `QQuickItem * textEdit0 = fix.delegateTextEdit(0)`. Place cursor at qtPos=0. Type `# `. | After kind=="heading", `fix.delegateTextEdit(0) == textEdit0` (same pointer). `fix.delegateCursorPos(0) == 2`. ListView `contentY` unchanged. |
| `heading_to_paragraph_preserves_textedit_pointer` | Load doc with `# foo`; record pointer; place cursor at qtPos=1; backspace the `#`. | After kind=="paragraph", same pointer. Cursor at qtPos=0. contentY unchanged. |
| `paragraph_to_listitem_preserves_textedit_pointer` | Load empty paragraph; record pointer; type `- ` at qtPos=0. | After kind=="list-item", same pointer. contentY unchanged. |

### 6.2 New test: `tst_live_render_cross_class_kind_transition_swaps_delegate`

Sanity check the cross-class path still works:

- Load empty paragraph; record `QQuickItem * paraPointer = fix.delegateAt(0)`.
- Type `---`.
- Assert: kind=="hr", `fix.delegateAt(0) != paraPointer`, `fix.delegateAt(0)->metaObject()->className()` contains `"HorizontalRule"`.

### 6.3 New test: `tst_live_render_paste_heading_renders_as_heading`

Closes the second dogfood bug at the root:

- Load doc with single empty paragraph.
- `fix.pasteText("# heading\n");` (or equivalent — implementation
  detail; the plan picks the exact text after checking the paste
  pipeline).
- Wait for kind change to "heading".
- Assert: `fix.delegateTextEdit(0)->property("font").value<QFont>().pixelSize()`
  matches the heading-level-1 expected pixelSize (not paragraph's).

### 6.4 Falsifiability proof (per invariant 4)

Pattern follows tier-2 §5.4 (`0aef9f3` / `6b32482` lineage).

1. Commit a stub: change `delegateClassFor` to always return the
   incoming kind unchanged (i.e. revert the bucketing). This makes
   `BlockKey` effectively `{kind, blockAnchor}` again; within-class
   kind changes again produce Delete+Insert.
2. Run §6.1's three slots. **All three must fail** (the `TextEdit`
   pointer changes because the delegate gets recreated).
3. Revert the stub.

The proof confirms the invariant test catches a regression to the
old architecture. If any §6.1 slot still passes under the stub, the
test is too lenient — fix the test before continuing.

## 7. Definition of done

- [ ] §6.1 test landed first and proven falsifiable. All three slots
      fail under the stub; all three pass under the implemented
      `delegateClassFor` bucketing.
- [ ] §6.2 test passes (cross-class still correctly swaps delegate).
- [ ] §6.3 test passes (paste-styling fixed at root).
- [ ] `kindOnlySwap` detector + `beginResetModel` branch deleted from
      `LiveBlockModel::applyOps`. `git grep kindOnlySwap` returns zero.
- [ ] `LiveView.qml`'s contentY save/restore `Connections` deleted.
      `git grep "_lastSavedContentY\|_modelResetCount"` returns zero.
- [ ] `BlockKey` field renamed `kind` → `delegateClass` in
      `LiveListModelBinding`; all consumers updated.
- [ ] `UnifiedInlineTextDelegate.qml` created; renders correctly for
      all four text-inline kinds.
- [ ] `ParagraphDelegate.qml`, `HeadingDelegate.qml`,
      `BlockquoteDelegate.qml`, `ListItemDelegate.qml` deleted.
      `git grep "ParagraphDelegate {\|HeadingDelegate {\|..."` returns
      zero outside test fixtures that may rename their expectations.
- [ ] No new `Qt.callLater` sites. Net change: −1 (the d92e1e2 site
      retires).
- [ ] No new re-entrance guards.
- [ ] Full live-render suite ≤ pre-tier-3 baseline failure count (11
      under offscreen).
- [ ] Dogfood pass: user types `#`, `-`, `>` at various positions;
      no flicker, no `contentY` jump, no "throws me about" behaviour.
      Paste with heading renders as heading.
- [ ] Discipline-log entry filed in `docs/queue.md` confirming
      queue.md:63's open thread is closed.

## 8. Future work

- **Plugin-registered kinds.** The `delegateClass` dispatcher is the
  hook point. A plugin could register a new `delegateClass` value
  with its own delegate component. Out of tier-3 scope; the design
  admits it without precluding changes.
- **Folding code-block into the unified delegate.** Open question.
  Code-block has different inline-format handling and monospace
  fonts; the cost of merging may exceed the gain. Defer.
- **Folding math into the unified delegate.** Even more deferred —
  `BlockInternalEdit` mode is its own seam.
- **`tryResolvePending` validation duplication** (queue.md:64).
  Independent.

## 9. Citations

- `docs/queue.md:63` — the pre-authorization for this direction
  (Discipline Log, 2026-05-11, invariant 7).
- `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` §
  premise 3 — D3 deferred descriptor-driven kind transitions to
  D4/later.
- `docs/specs/2026-05-11-focus-chokepoint-design.md` — tier-1 spec;
  defines the chokepoint machinery this spec leaves intact.
- `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` —
  tier-2; the dogfood that surfaced the bugs this spec fixes.
- `docs/handoff/2026-05-15-tier-2-completion.md` §"Dogfood Findings"
  — the named bugs.
- `docs/INVARIANTS.md` invariants 1, 2, 3, 4, 5, 6, 7, 8 — all
  enforced by this spec.
- `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h:21-23`
  — the R-arc's deferred-Loader plan; this spec is the first step
  toward consuming `delegateUrl` (still not consumed in tier 3, but
  the dispatcher is now keyed in a way that admits it).
- `libs/markoff-live/CLAUDE.md` — seam guidance; updated by this
  spec (Qt.callLater count goes from 2 to 1; re-entrance guards
  unchanged).
- Commit `d92e1e2` — the bandaid this spec retires.
- Memory `feedback_invariant_promotion_discipline` — model-wins-on-
  structural; delegate-leads-on-transient. Unchanged.

## 10. Open questions deferred to the plan

- **Theme-slot mapping for the unified delegate.** The current
  delegates each set their own theme slot. The plan picks the exact
  enum values and confirms `liveBinding.headingSlot(level)` /
  `Theme.Slot.Blockquote` / `Theme.Slot.ListItem` are available
  (they should be — the existing delegates already use them).
- **Inline-highlighter wiring.** All four text-inline kinds use
  `InlineHighlighterAttached`; the plan confirms the same
  attachment binding works under the unified delegate.
- **CMake retirement of the four superseded delegates.** Plan picks
  the exact lines in the qmldir and `qt_add_qml_module` call.
- **Test for the paste path.** §6.3's exact paste content depends
  on the paste pipeline's structural processing; the plan
  reproduces the dogfood scenario faithfully.
- **Whether `BlockKey`'s field-rename can be mechanical** (`kind`
  → `delegateClass` everywhere) or whether some call sites still
  need access to the raw `kind`. Default: rename mechanical; raw
  `kind` access goes through `BlockRecord.kind` (which exists).
