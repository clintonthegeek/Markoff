# Block-only kinds — design

**Date:** 2026-05-13
**Branch:** `exploration/new-foundation`
**Invariants invoked:** 1, 2, 3, 4, 5, 8 per `docs/INVARIANTS.md`
**Driving findings:** 2026-05-11 focus-chokepoint dogfood (D-fc-1, D-fc-2, D-fc-3); 2026-05-13 dogfood (D-fc-4 — orphaned `TextCaret` after Backspace adjacent to HR)

## 0. One-paragraph summary

Markoff has two block kinds that don't host a text caret and won't in the future either — `HorizontalRule` and `Image`. (A third, `Math`, is in a similar state today but is scoped out: it will become text-bearing in a future spec; see §2.) The codebase treats each of them as a one-off: independent delegate files, near-identical `Keys.onPressed` guards, separate structural-key handler registrations, and an implicit "is this block-only?" derived from "`TextCaret` not in `supportedCursorVariants`." Recent bugs trace to the implicit-ness — the chokepoint silently stages an invalid `TextCaret`, the navigation controller silently skips block-only rows, `backspaceMerge` runs across the boundary and orphans the cursor. This spec names the category as an explicit flag on `BlockKindDescriptor`, gives it explicit policy, and folds the per-kind one-offs into one set of behaviours that any future block-only kind inherits.

## 1. Motivation — three findings, one missing abstraction

### 1.1 D-fc-3 (resolved in `9b30d75`)

Clicking on or near an HR left the cursor in an invalid `TextCaret` state. The chokepoint had baked in "every focus request becomes a `TextCaret`." Fixed by making `tryResolvePending` consult `BlockKindRegistry.supportedCursorVariants`. **This proved the registry already knows what each kind supports — but only the chokepoint reads it. Other call sites still hardcode TextCaret.**

### 1.2 D-fc-4 (this spec's bug)

Sequence: at end of an existing paragraph, press Enter, type `---`. An HR is created and a fresh empty paragraph appears below it (D-fc-1 fix). Press Backspace on the empty paragraph.

Observed: cursor ends up as `TextCaret` with `row=-1` (orphan; the anchor points at the deleted paragraph). 5/5 reproductions in the headless test (`backspace_after_typed_hr_lands_somewhere_sensible`); user reports "caret disappears" in interactive use.

Root cause: `LiveStructuralKeyHandler::paragraphBackspace` runs `Cmd::backspaceMerge` unconditionally. `backspaceMerge` (`libs/markoff-core/src/Cmd/D2.cpp:62`) mutates the previous block's buffer and removes the current block — *regardless* of whether the previous block is text-bearing. When the previous block is an HR, the merge inserts the current block's (empty) text into the HR's `---` buffer, then deletes the current block, then `establishFocus`es on the HR. The variant-aware chokepoint *should* then stage `BlockSelected` on the HR; the test shows that in practice the cursor's `m_cursor` is something else by the time the cascade settles. Whether the immediate cause is a race in the cascade or a stale `syncFromTextEdit` echo, the *real* fault is the policy: `backspaceMerge` should not run across a text↔block-only boundary at all.

### 1.3 The missing abstraction

`HorizontalRuleDelegate.qml`, `ImageDelegate.qml`, `MathDelegate.qml` carry three independent copies of:

- The `isSelected` binding (`cursorKind === "BlockSelected" && focusedAnchorRow === modelIndex`).
- The `Keys.onPressed` guard ("only handle when isSelected").
- The forward-to-structural-key-handler logic.
- The `Component.onCompleted` / `onDestruction` chokepoint registration.
- The visual treatment for the selected state (a 2-px border in each; no shared style).

`LiveStructuralKeyHandler::registerBuiltins()` (`libs/markoff-live/src/LiveStructuralKeyHandler.cpp:363` onward) registers near-identical Up / Down / Backspace / Delete handlers for HR, then again for Image, then again for Math. Three lineages drifting apart.

This spec collapses **two of the three** (HR, Image) into a single set of behaviours. Math stays a one-off for now — see §2 — and is retired alongside its own conversion to text-bearing in a separate spec.

**Whatever this spec lands needs to make adding a new block-only kind a one-paragraph descriptor change, not a copy-paste of two delegates and two handler blocks.**

## 2. Scope and explicit non-goals

### In scope — HR and Image only

- Naming the category in code (`BlockKindDescriptor::isBlockOnly` as an explicit boolean field, set true for HR and Image).
- Behaviour policy for arrow navigation, Backspace / Delete, Enter, typing, and clicking when a block-only kind is the focus target or adjacent to it.
- Factoring the structural-key-handler logic into one shared set, registered for every kind whose descriptor reports `isBlockOnly == true`.
- Factoring the delegate-side `isSelected` + `Keys.onPressed` boilerplate into a single QML component, inherited by `HorizontalRuleDelegate` and `ImageDelegate`.
- Visual treatment for the BlockSelected state on HR and Image: keep the existing per-delegate treatment, just shared via the new base. Polish deferred to E2.6 theme work — confirmed with user.

### Out of scope (explicitly)

- **Math.** The user's intent is that Math eventually behaves as a heading does: render-as-formula when the caret is not in it, fall back to text editing as soon as the caret enters or it's clicked. That's a substantial change involving `supportedCursorVariants`, the Math delegate's render-vs-edit state, and the parser pipeline. It gets its own future spec. **For this work, `BlockKindDescriptor::isBlockOnly` is set `false` for Math** even though Math currently has no `TextCaret` in its `supportedCursorVariants` — `MathDelegate.qml`, the Math-specific structural-key registrations, and the existing BlockSelected-on-click behaviour for Math all stay as they are. A discipline-log entry will mark this as a known smell to be revisited.
- Adding new block-only kinds. The categories listed in §0 are exhaustive for now.
- Internal-edit mode for Image (`BlockInternalEdit` variant, alt-edit). Entry into and exit from that mode is a separate concern; this spec handles the BlockSelected ↔ neighbour transitions only. Image with alt-edit active is unchanged by this spec.
- The 8 preexisting failures (`tst_live_render_cursor` etc.) — they predate this work.
- Touch-input affordances. Markoff is currently desktop-only.
- Multi-block selection that includes a block-only kind in the range. The current `LiveSelectionView` design tolerates this; full UX polish for it is deferred.

## 3. The category: `isBlockOnly`

A block kind is **block-only** iff its descriptor's explicit `isBlockOnly` field is `true`. The field is declared on `BlockKindDescriptor` and set during `BlockKindRegistry::registerBuiltins()`:

```cpp
// libs/markoff-live/include/markoff/live/BlockKindDescriptor.h
struct BlockKindDescriptor {
    QString id;
    // ... existing fields ...
    QStringList supportedCursorVariants;
    /// True for kinds whose content can never host a text caret AND
    /// whose UX is "select the whole block, then act on it" — currently
    /// HorizontalRule and Image. The flag is explicit rather than
    /// derived from `supportedCursorVariants` so that Math — which
    /// also lacks TextCaret today but is on a separate trajectory
    /// toward becoming text-bearing — can be excluded cleanly without
    /// a kind-name special-case at every call site.
    bool isBlockOnly = false;
};
```

Convenience predicate on the registry:

```cpp
// libs/markoff-live/include/markoff/live/BlockKindRegistry.h
bool isBlockOnly(const QString &kind) const {
    const auto *d = find(kind);
    return d && d->isBlockOnly;
}
```

Set at registration in `BlockKindRegistry.cpp`:

| Kind | `supportedCursorVariants` | `isBlockOnly` |
|---|---|---|
| Paragraph, Heading, CodeBlock, ListItem, Blockquote | `[TextCaret]` | false |
| HorizontalRule | `[BlockSelected]` | **true** |
| Image | `[BlockSelected, BlockInternalEdit]` | **true** |
| Math | `[BlockSelected, BlockInternalEdit]` | false *(see §2)* |

Every call site that needs to make a block-only-vs-text-bearing decision goes through `BlockKindRegistry::isBlockOnly()`. No `if (kind == "hr" || kind == "image")` style fanning anywhere. The single transitional asymmetry (Math has no `TextCaret` but `isBlockOnly = false`) is the price of deferring the Math conversion to a separate spec; the discipline-log entry §10 covers it.

## 4. Behaviour policy

The eight rules below define the post-spec behaviour. Each rule is paired with a falsifiable invariant test (§7).

### R-arrow-into

**Arrow Up/Down from a text-bearing block toward a block-only neighbour lands `BlockSelected` on the block-only.** (Not skip-past.)

This is the central reversal of `9b30d75`-era behaviour. `previousNavigableRow` / `nextNavigableRow` no longer filter on `isTextBearing` — they return the *immediate* neighbour, whatever its kind. The chokepoint's variant-aware logic then picks `TextCaret` if the target is text-bearing or `BlockSelected` if block-only. Two arrow presses to cross a block-only kind; one to land on it.

### R-arrow-out

**Arrow Up/Down from `BlockSelected` on a block-only block moves the cursor to the immediate neighbour in that direction**, planting `TextCaret` if that neighbour is text-bearing or `BlockSelected` if it too is block-only.

A consecutive-block-only sequence (e.g., HR then Image) requires two arrows to cross, one per block. That's deliberate — each block-only deserves to be visible to the cursor as a distinct stop.

### R-backspace-at-text-start-adjacent

**`Backspace` at qtPos=0 of a text-bearing block whose immediate previous block is block-only does NOT merge.** It moves the cursor to `BlockSelected` on the previous block-only. The current block is unchanged.

This retires the racy `backspaceMerge`-across-boundary path that caused D-fc-4. A second Backspace then deletes the now-selected block-only (R-delete-blockonly).

### R-delete-at-text-end-adjacent

Symmetric to R-backspace-at-text-start-adjacent. **`Delete` at qtPos=length of a text-bearing block whose immediate next block is block-only does NOT merge.** It moves the cursor to `BlockSelected` on the next block-only.

### R-delete-blockonly

**`Backspace` or `Delete` on a `BlockSelected` block-only removes the block.** The cursor lands `TextCaret` on:
- the previous block (at its end) if one exists,
- else the next block (at qtPos=0) if one exists,
- else stays at NoCursor (empty document).

This is the existing `hrDelete` behaviour; reuse it for Image and Math.

### R-enter-blockonly

**`Enter` on a `BlockSelected` block-only inserts a new empty paragraph after the block** and lands `TextCaret` at qtPos=0 of that paragraph.

This is the user-requested behaviour and matches the D-fc-1-style "make typing continue past structural blocks" affordance.

### R-type-blockonly

**Printable keystrokes on a `BlockSelected` block-only are no-op.** Specifically: not consumed, not propagated to a TextEdit (there isn't one), not turned into a content-replacement. The block stays selected; the keystroke is discarded.

Rationale: a "type-replace" affordance (replace the block with a paragraph containing the typed text) was considered and rejected — it's easy to trigger accidentally and there's no Undo affordance specific to that destruction. The user-explicit deletion path (R-delete-blockonly) covers the legitimate use case.

### R-click-blockonly

**Mouse-click on a block-only block lands `BlockSelected`.** (Already implemented in `9b30d75` via the variant-aware chokepoint. Mentioned here so the policy is exhaustive.)

### R-tripleclick-blockonly

**Triple-click on a block-only block lands `BlockSelected`.** Same outcome as single click — there's nothing "more" to select within the block. The triple-click counter in `LiveView.qml`'s MouseArea is reset cleanly. (User-confirmed in §10.)

## 5. Implementation outline

### 5.1 Registry — name the category (small, do first)

- Add `BlockKindDescriptor::isBlockOnly` field (§3).
- Add `BlockKindRegistry::isBlockOnly(QString)` predicate (§3).
- In `BlockKindRegistry::registerBuiltins()`: set `isBlockOnly = true` on the HR and Image descriptors. Leave it `false` (default) elsewhere, including Math.
- Replace any kind-name fanning at the few existing call sites that could use this:
  - The `LiveNavigationController::isTextBearing` helper stays (its "TextCaret in variants?" question is the right one for nav). Add a sibling `isBlockOnly` helper to the controller if needed, but only use it where the new policy genuinely requires it.
- No behaviour change yet — this commit is purely a naming refactor + descriptor flag wiring, and lands first so the rest of the spec can cite it.

### 5.2 Navigation — flip the skip to a land-and-step

- `LiveNavigationController::previousNavigableRow` / `nextNavigableRow` change semantics: they return the immediate previous/next row that is **either** text-bearing **or** `isBlockOnly`. Math (not text-bearing, not `isBlockOnly`) is still skipped — preserving Math's current "invisible to keyboard nav" behaviour until its separate spec lands.
- The chokepoint already picks the right variant for the target.
- The per-kind structural handler for Up/Down on HR (currently `hrNavigateUp`, `hrNavigateDown`) becomes a shared `blockOnlyNavigateUp`/`Down` registered for every `isBlockOnly` kind. Math's existing handlers stay separate.

### 5.3 Structural-key handler — retire merge across the boundary

- `paragraphBackspace`: insert a precondition. If `c.qtPos == 0` AND `c.blockIndex > 0` AND `registry->isBlockOnly(model.recordAt(c.blockIndex - 1).kind)`, call `establishFocus(prevBlock.anchor, /*qtPos=*/0)` (the chokepoint will pick `BlockSelected`) and return `Handled`. Skip the `backspaceMerge` call.
- `paragraphDelete`: symmetric — if `c.qtPos == c.blockText.length()` AND next block is block-only, select-the-next and skip `deleteMerge`.
- The same precondition applies to Heading, ListItem, Blockquote, CodeBlock. Best to lift it out of the kind-specific handlers into a shared helper run *before* kind-specific dispatch in `LiveStructuralKeyHandler::tryHandle`.
- Existing per-kind delete on HR and Image (`hrDelete`, `imgDelete`) become a single `blockOnlyDelete` registered against every `isBlockOnly` kind. `mathDelete` stays separate.
- New: `blockOnlyEnter` registered for `isBlockOnly` kinds — inserts a fresh paragraph after via `Cmd::enterAtEnd` and `establishFocus`-es on it.
- New: `blockOnlyTypePrintable` — registered for `isBlockOnly` kinds. The structural handler returns `Handled` (consuming the keystroke) but performs no model edit and no focus change. The block stays selected; the keystroke is dropped.

### 5.4 Delegates — shared QML base

Create `libs/markoff-live/qml/delegates/BlockOnlyDelegateBase.qml`. It owns:

- The `blockAnchor` cached property (current per-delegate code).
- The `isSelected` binding.
- The `Keys.priority` + `Keys.onPressed` guarded forwarder.
- The `Component.onCompleted` / `onDestruction` chokepoint registration.
- The selected-state visual treatment — for this pass, the existing 2-px border, kept as-is. (Polish deferred to E2.6 theme work.)

`HorizontalRuleDelegate` and `ImageDelegate` inherit from this base and add only their own content layer (the line, the image). `MathDelegate` keeps its current standalone implementation — it will be reworked in its own spec.

### 5.5 Visual treatment

Deferred entirely to E2.6 theme work. This spec preserves the existing per-delegate visual treatment for the BlockSelected state — share it via the new base, but don't re-skin it. (User-confirmed in §10.)

## 6. Migration / retirement

Per invariant 3 ("a new authority retires the old one in the same plan"), the following lineages are retired:

- The `hrNavigateUp`, `hrNavigateDown`, `hrDelete`, `imgDelete` registrations in `LiveStructuralKeyHandler`. Replaced by `blockOnlyNavigateUp/Down/Delete/Enter/TypePrintable` registered against every `isBlockOnly` kind. `mathDelete` is left alone.
- The per-delegate `Keys.onPressed` / `isSelected` / `Component.onCompleted` boilerplate in `HorizontalRuleDelegate.qml` and `ImageDelegate.qml`. Replaced by `BlockOnlyDelegateBase.qml`. `MathDelegate.qml` keeps its standalone copy until its own rework.
- The `LiveNavigationController::previousNavigableRow` / `nextNavigableRow`'s "skip non-text-bearing" filter. Replaced by "skip non-(text-bearing OR `isBlockOnly`)" — Math is still skipped, but HR and Image are landed on.

The retirement is a **work-unit in the implementation plan**, not a follow-up. No new authority lands without the old one going at the same time.

## 7. Falsifiable invariant tests

One test per rule in §4, on the `LiveRealisticInputHarness` (`tst_live_render_focus_chokepoint_invariant`). Each test asserts both:
- `cursorState.cursorKind` matches the expected variant.
- `cursorState.focusedAnchorRow` matches the expected row.

| Rule | Test name | Setup → action → expected |
|---|---|---|
| R-arrow-into | `arrow_down_lands_blockselected_on_hr` | `alpha\n\n---\n\nbeta`; cursor at end of row 0; Down → BlockSelected on row 1 |
| R-arrow-out | `arrow_down_from_blockselected_hr_lands_on_text` | same doc; click HR; Down → TextCaret row 2 |
| R-backspace-at-text-start-adjacent | `backspace_at_para_start_after_hr_selects_hr` | `alpha\n\n---\n\nbeta`; cursor at row 2 pos 0; Backspace → BlockSelected on row 1, model unchanged (still 3 rows) |
| R-delete-at-text-end-adjacent | `delete_at_para_end_before_hr_selects_hr` | same doc; cursor at end of row 0; Delete → BlockSelected on row 1, model unchanged |
| R-delete-blockonly | `backspace_on_selected_hr_removes_it` | click HR; Backspace → row 1 gone, TextCaret on the now-row-1 paragraph |
| R-enter-blockonly | `enter_on_selected_hr_inserts_paragraph_after` | click HR; Enter → new empty paragraph after, TextCaret on it |
| R-type-blockonly | `typing_on_selected_hr_is_noop` | click HR; type "x" → still BlockSelected on row 1; model unchanged |
| R-click-blockonly | already covered by `click_on_hr_sets_block_selected` from `9b30d75` | — |
| R-tripleclick-blockonly | `tripleclick_on_hr_lands_blockselected` | click HR three times in rapid succession → BlockSelected on row 1; model unchanged; click-counter resets cleanly |

Each substantive test (rows above the line) gets a parallel Image-kind variant that uses a parsable image-syntax fixture (`alpha\n\n![alt](url)\n\nbeta` or similar — verify the parser produces an Image block). This proves the behaviour is genuinely registry-driven, not HR-specific.

**Also:** the D-fc-4 reproducer `backspace_after_typed_hr_lands_somewhere_sensible` (already committed) tightens from "cursor is on some row, kind isn't `none`" to "cursor is BlockSelected on the HR (row 1), model is unchanged (still 3 rows)" once R-backspace-at-text-start-adjacent lands.

### Falsifiability stub (Task-15 pattern)

A one-commit stub that makes `BlockKindRegistry::isBlockOnly(QString)` always return `false` should turn the chokepoint, navigation, and structural-key paths back into their pre-spec behaviour and break every test in the table above. If any of them still pass, the test is too lenient and needs tightening before the production change lands. The stub is then reverted in the next commit, preserving the audit trail per invariant 4.

## 8. Edge cases worth pinning

- **Block-only at row 0.** Arrow Up has nowhere to go; same for `R-backspace-at-text-start-adjacent` at row 1 where row 0 is block-only. Existing handlers already return `NotHandled` at these boundaries; verify in tests.
- **Consecutive block-onlies** (`---\n\n---`). Each takes one arrow press to cross. The chokepoint's BlockSelected on the second is reachable; deleting the first lands the cursor on the second (per R-delete-blockonly).
- **First/last block in doc is block-only.** Document can start or end with an HR/Image. R-enter-blockonly creates the new paragraph after; if there's nothing after the last block, `Cmd::enterAtEnd` already handles that. R-delete-blockonly with no neighbour falls back to NoCursor; verify the empty-doc invariant.
- **Block-only with a single text neighbour above.** Backspace on row 1 text-bearing where row 0 is block-only: select row 0. Behaviour matches R-backspace-at-text-start-adjacent.
- **`Math`.** Out of scope (see §2). The rules in §4 apply only to `isBlockOnly == true` kinds (HR, Image). Math keeps current behaviour — invisible to arrow keys, BlockSelected on click via the variant-aware chokepoint, its own structural-key registrations untouched.
- **Image with `alt-edit` mode.** Out of scope — `BlockInternalEdit` variant is a distinct state and entry into it is preserved unchanged. The rules in §4 apply only to Image in its default `BlockSelected` state.
- **Triple-click on a block-only.** Handled explicitly (R-tripleclick-blockonly): lands BlockSelected, same as single click. The triple-click counter in `LiveView.qml`'s MouseArea resets cleanly. No surrounding rows get selected.

## 9. Discipline citations

- **Invariant 1** — cite developmental record: `docs/handoff/2026-05-07-live-binding-developmental-history.md` covers focus delivery and is referenced as authority. This spec also cites the recent commits `9b30d75` (variant-aware chokepoint), `4fb711f` (D-fc-1/2), and the active branch's chokepoint trail (`ad88089`, `2d609ba`/`2ab52aa`, `20dcaee`/`79fcedc`).
- **Invariant 2** — L4 (block-content authority): unchanged. This spec only touches L3 (cursor variant) and L5 (structural keys). No new L4 path is introduced.
- **Invariant 3** — retirement: §6 names every retiring lineage as a work-unit.
- **Invariant 4** — falsifiable tests: §7 lists tests and the stub strategy.
- **Invariant 5** — production callsite, not synonym: the tests all drive via `LiveRealisticInputHarness` against real QML delegates. No C++-only fakes for the keyboard path.
- **Invariant 6** (`Qt.callLater`): none added by this spec. The retirement of three per-delegate `Component.onCompleted` blocks removes incidentally none of them either (they were already migrated off `Qt.callLater` in the chokepoint refactor).
- **Invariant 7** (re-entrance guards): none added.
- **Invariant 8** — discipline log: open entries on `LiveBlockModel.cpp:106` (the kind-swap pattern-match) and `LiveCursorState.cpp:419-440` (variant-aware mutation) stay open after this spec; they will be revisited as part of the broader chokepoint cleanup but are not folded in here.

## 10. Resolved decisions and an open discipline-log entry

User decisions captured 2026-05-13:

- **Visual treatment.** Defer to E2.6 — keep the current per-delegate ink, just share it via the new base.
- **Math.** Excluded from this spec. Math will be reworked to be text-bearing (caret enters → switches to source mode, blur → re-renders) in a separate spec. Until then Math keeps current BlockSelected-on-click behaviour, its own delegate file, and its own structural-key handlers.
- **Triple-click on block-only.** Lands BlockSelected, same as single click. Triple-click counter resets cleanly.
- **`R-delete-blockonly` cursor landing.** Preserve the existing asymmetry — Backspace lands prev, Delete lands next — since it matches users' directional intent.

**Discipline-log entry to file alongside the spec landing:**

> `libs/markoff-live/src/BlockKindRegistry.cpp:Math` — inv #8 — `isBlockOnly` is explicit-false for Math despite Math having no `TextCaret` in `supportedCursorVariants`. Transitional asymmetry; will be removed when Math becomes text-bearing in its own spec.

## 11. Definition of done

- `BlockKindDescriptor::isBlockOnly` field shipped; HR and Image set `true`, Math (and all text-bearing kinds) `false`. `BlockKindRegistry::isBlockOnly(QString)` predicate available. Any incidental hardcoded `kind == "hr"` / `kind == "image"` style checks unearthed during the work are replaced with the predicate.
- `BlockOnlyDelegateBase.qml` lives; `HorizontalRuleDelegate` and `ImageDelegate` inherit from it and contain only their content layer. `MathDelegate.qml` is unchanged.
- Structural-key handler registers one set of block-only handlers (`Navigate`, `Delete`, `Enter`, `TypePrintable`) once, against every `isBlockOnly == true` kind. `hrNavigateUp/Down`, `hrDelete`, `imgDelete` are retired; `mathDelete` and the rest of Math's handlers stay.
- All 9 falsifiability tests in §7 pass on HR, with parallel Image variants where applicable (≈ 14 new test methods total). The stub commit demonstrates they go red without the new path.
- `tst_live_render_focus_chokepoint_invariant::backspace_after_typed_hr_lands_somewhere_sensible` (the D-fc-4 reproducer) tightens to BlockSelected-on-HR and passes 5/5 times.
- No new regressions in the wider live-render suite — the 8 preexisting failures (`tst_live_render_cursor` etc.) stay as they are.
- Discipline log gets the Math-asymmetry entry from §10, plus any other unavoidable smells introduced.

---

**Next step after the user approves this spec:** write the plan
(`docs/plans/2026-05-13-block-only-kinds.md`) with task-by-task TDD,
per the `superpowers:writing-plans` convention.
