# Live-mode find highlighting — design

**Date:** 2026-05-21
**Branch:** `exploration/new-foundation`
**Driver:** Port-pressure follow-up to the Find UI port. Dogfood findings doc: [`docs/handoff/2026-05-21-find-ui-dogfood-findings.md`](../handoff/2026-05-21-find-ui-dogfood-findings.md).
**Out:** Tag `v0.7.0-find-highlights` candidate; closes the Find UI port story.

## Problem

`Markoff::FindController` reports correct match counts and `LiveFindAdapter` parks a non-focusing caret on `navigationRequested`, so Next/Prev navigation works. But `LiveFindAdapter` is a no-op for `matchesChanged` — there is no channel from the controller to the rendered text. Live mode shows the right count but **no visible highlights**. Source mode works "for free" because `QPlainTextEdit::setExtraSelections` is a Qt-built-in highlight surface.

## Approach

Reuse the E1 InlineHighlighter pipeline. The existing path already paints `QTextCharFormat` ranges from `BlockRecord::inlineSpans` through a per-delegate `QSyntaxHighlighter`, with character-by-character merging of overlapping spans. We add a second pass on top of inline-spans for find matches.

## Architecture

Three-layer split mirrors the inline-spans precedent:

```
FindController ──matchesChanged/currentMatchChanged──▶
LiveFindAdapter (producer; per-block cache) ──dataChanged──▶
LiveBlockModel (channel; FindSpansRole) ──model.findSpans──▶
InlineHighlighter (renderer; find-pass after inline-pass)
```

### Producer — `LiveFindAdapter`

Currently a no-op on `matchesChanged`. Will:

1. Subscribe to `FindController::matchesChanged` and `FindController::currentMatchChanged`.
2. Maintain `QHash<BlockAnchor, QList<FindSpan>> m_findSpansByBlock`.
3. On each emission, diff against the previous snapshot and call a new `LiveBlockModel::setFindSpans(BlockAnchor, QList<FindSpan>)` for each block whose span list changed. The model emits one `dataChanged` per touched row.
4. When only `currentMatchIndex` changes (no needle change), only two blocks are touched: the row that *lost* current and the row that *gained* current.
5. On `detach()` (controller swap or session end), clear the cache and call `setFindSpans({}, {})` per previously-touched block to vanish highlights.

The adapter is the sole writer to `BlockRecord::findSpans`. The model itself does not own the cache — it only stores per-row data delivered by the adapter.

### Channel — `LiveBlockModel` + `BlockRecord`

`BlockRecord` gains:

```cpp
QList<FindSpan> findSpans;  // empty if no active find / no matches in this block
```

`FindSpan` defined in a new header `libs/markoff-live/include/markoff/live/FindSpan.h`:

```cpp
namespace Markoff::Live {
struct FindSpan {
    quint32 byteOffset;
    quint32 byteLength;
    bool    isCurrent;  // true for the FindController::currentMatchIndex match
};
}
Q_DECLARE_METATYPE(Markoff::Live::FindSpan)
```

`LiveBlockModel` gains:

- A new role `FindSpansRole = Qt::UserRole + N` exposed via `roleNames()` as `findSpans`.
- `data(index, FindSpansRole)` returns the per-row `QList<FindSpan>` wrapped as `QVariant::fromValue`.
- A new mutator `void setFindSpans(const Markoff::BlockAnchor &, const QList<FindSpan> &)` that finds the row by anchor, swaps the list on the record, and emits `dataChanged` with only `FindSpansRole` in the roles vector.

The role is delegate-readable as `model.findSpans` (lowercase) per Qt convention.

### Renderer — `InlineHighlighter`

Extends `highlightBlock()` with a second pass after the inline-spans pass:

```cpp
void highlightBlock(const QString &text) override {
    // Existing pass 1: walk inline spans, build per-char QTextCharFormat
    // overlay from bold/italic/strike/code/highlight/link/wikilink/tag.
    ...

    // New pass 2: walk findSpans, paint Theme::FindMatch background on
    // non-current matches, Theme::FindMatchCurrent on the current one.
    // Backgrounds compose with existing formats — only sets
    // setBackground(); foreground / weight / style untouched.
    for (const FindSpan &fs : m_findSpans) {
        const QTextCharFormat fmt = backgroundFormatFor(fs.isCurrent);
        const auto [qtPos, qtLen] = byteRangeToQtRange(text, fs.byteOffset, fs.byteLength);
        // Merge with existing per-char overlay then setFormat.
        ...
    }
}
```

`InlineHighlighter` gains a `setFindSpans(const QList<FindSpan> &)` mutator that stores the list, calls `rehighlight()`, and emits the existing rehighlight signal path. The QML attached object `InlineHighlighterAttached` exposes a `findSpans` property that forwards to this mutator.

### MathDelegate

Math blocks render LaTeX, not source text. Find matches still flow through `FindController` (which searches the block's LaTeX bytes). `MathDelegate.qml` will be wired to ignore `model.findSpans` — counts and navigation work but no visible highlight on the rendered math. Deferred to a future spec when the BlockInternalEdit LaTeX-edit surface is plumbed.

### BlockOnly kinds (HR, Image)

No text. `model.findSpans` is irrelevant. Their delegates don't consume it.

### Theme slots

**Reuse existing slots** — `Markoff::Theme::Slot` already defines:

```cpp
SearchMatchBackground,         // #ffe080  (set in defaultLight)
SearchActiveMatchBackground,   // #ffb050  (set in defaultLight)
```

These were added during the older `SearchEngine` work (which publishes `Selection::Kind::SearchMatch` into `Session.secondarySelections`). The slots are populated in `Theme::defaultLight()` but no Live-leaf consumer renders against them yet. We adopt them verbatim. No new Theme work needed — `Theme::defaultDark()` already inherits the defaults from `defaultLight()` per its existing implementation.

The existing yellow/orange palette stays clear of `Theme::Highlight` (`#fff176`, used for `==…==` mark spans) — when searching "foo" inside `==foo==`, the find pass runs after the inline pass and overpaints the find-match background. Two concurrent backgrounds aren't supported by `QTextCharFormat`; the find pass wins by ordering. This is intentional — the find highlight should dominate during an active find session.

## Data flow worked example

User opens a doc with 5 paragraphs, types "the" into FindBar:

1. `FindController::recomputeMatches()` produces 12 matches across 4 paragraphs.
2. `matchesChanged()` emits.
3. `LiveFindAdapter::onMatchesChanged()`:
   - Builds new `QHash<BlockAnchor, QList<FindSpan>>` from `controller->matches()`.
   - For 4 blocks gaining matches: calls `model->setFindSpans(anchor, list)`.
   - For other blocks: empty list (already empty, no-op).
4. `LiveBlockModel::setFindSpans` updates the record and emits `dataChanged(row, row, {FindSpansRole})` for each of the 4 rows.
5. QML delegates with `Connections { onFindSpansChanged: highlighter.setFindSpans(...) }` push the new spans to InlineHighlighter.
6. InlineHighlighter calls `rehighlight()`; on next paint, find-pass runs and the matched ranges show the yellow background.

User clicks "Next":

1. `FindController::findNext()` advances `m_currentIndex`, emits `currentMatchChanged()` then `navigationRequested(match)`.
2. `LiveFindAdapter::onCurrentMatchChanged()`:
   - Identifies which span was previously current and which is now current.
   - For each of 1-2 affected blocks (the block losing current and the block gaining current; often the same block), rebuilds the list with the updated `isCurrent` bools and calls `setFindSpans`.
3. Model emits `dataChanged` for those rows; highlighter re-runs; current match swaps color.
4. `LiveFindAdapter::onNavigationRequested()` (existing path) parks the caret on the new current match for scroll-into-view.

## Invariant 4 falsifiability

The integration test must fail when find-spans plumbing is broken. Falsifiability:

- Stub `InlineHighlighter::setFindSpans` to a no-op → background paint vanishes → test fails on `format runs do not include FindMatch background at byte offset N`.
- Stub `LiveBlockModel::setFindSpans` to swallow the call → `dataChanged` is never emitted → test fails on `dataChanged signal not received for row N within 200 ms`.

Both falsifiability stubs are committed and reverted in history per invariant 4.

## Test plan

Integration test in `tst_live_render_qml_integration`:

**Slot 1: `find_matches_render_highlights_in_live_mode`**
- Load a 3-paragraph doc with the needle "the" appearing in 2 paragraphs.
- Construct a `FindController` against the doc, set needle.
- Verify `model.data(row=0, FindSpansRole)` returns the expected list.
- Drive QML delegate's TextEdit and walk `QTextCursor` across the row, capturing `QTextCharFormat::background()` for each character.
- Assert the matched byte ranges have `Theme::defaultLight().color(FindMatch)` background; non-matched ranges have the default background.

**Slot 2: `current_match_renders_with_distinct_color`**
- Same setup as slot 1.
- Initial state after `setNeedle`: match 0 is current.
- Assert row containing match 0 has `FindMatchCurrent` background on the matched range; other matches have `FindMatch`.
- Call `controller.findNext()` → match 1 becomes current.
- Assert the previously-current range is now `FindMatch` and the new-current range is `FindMatchCurrent`.

**Slot 3: `find_highlights_clear_on_needle_empty`**
- Set needle to "the", verify highlights present.
- Set needle to "", verify all `findSpans` empty and `dataChanged` emitted for previously-touched rows.

**Slot 4: `find_highlights_survive_block_edit`** (regression guard)
- Set needle "the" in a paragraph "the cat sat on the mat".
- User types " gently" at end → block becomes "the cat sat on the mat gently".
- `d2DocumentChanged` fires → `FindController::recomputeMatches()` runs.
- Assert matches re-detected at correct byte offsets (still 2), highlights re-rendered.

C++ unit tests in `tst_live_find_adapter` (new binary):

**`adapter_emits_setfindspans_per_block_on_matchesChanged`**
- Stub `LiveBlockModel`, attach adapter to a FindController.
- Set needle; verify `setFindSpans` called exactly once per block with matches, with the expected `QList<FindSpan>`.

**`adapter_recomputes_isCurrent_on_currentMatchChanged`**
- Same setup; call findNext; verify two `setFindSpans` calls (one for losing-current row, one for gaining-current row, or one call if same row).

**`adapter_clears_cache_on_detach`**
- Set needle, verify spans present; call detach; verify `setFindSpans({}, {})` for previously-touched blocks.

## Files

New:
- `libs/markoff-live/include/markoff/live/FindSpan.h`
- `libs/markoff-live/tests/tst_live_find_adapter.cpp` + CMake target

Modified:
- `libs/markoff-live/src/Detail/LiveFindAdapter.{h,cpp}`
- `libs/markoff-live/include/markoff/live/BlockRecord.h` (+`findSpans` field)
- `libs/markoff-live/include/markoff/live/LiveBlockModel.h` (+`FindSpansRole`, `setFindSpans`)
- `libs/markoff-live/src/LiveBlockModel.cpp`
- `libs/markoff-live/include/markoff/live/InlineHighlighter.h` (+`setFindSpans`, find-pass)
- `libs/markoff-live/src/InlineHighlighter.cpp`
- `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` (pass `model.findSpans` to attached highlighter)
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` (same)
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` (4 new slots above)

Unchanged (the existing slots are reused):
- `libs/markoff-core/include/markoff/core/Theme.h`
- `libs/markoff-core/src/Theme.cpp`

Unchanged (callouts for clarity):
- `libs/markoff-core/include/markoff/core/FindController.h` — no new API.
- `libs/markoff-live/qml/delegates/MathDelegate.qml` — Math intentionally skipped.
- `libs/markoff-live/qml/delegates/BlockOnlyDelegateBase.qml` — HR/Image have no text.

## Engineering-discipline check (per `docs/INVARIANTS.md`)

- **Inv 1 (cite developmental record):** find-session-scope spec (`docs/specs/2026-05-20-find-session-scope-design.md`) introduced `FindController` as the canonical session-bound find owner. This spec consumes that abstract; no shape change to FindController.
- **Inv 2 (L4 authority):** `BlockRecord::findSpans` is *transient* data written by `LiveFindAdapter`, not structural data — does not touch L4 block-content authority. Model wins on receipt (it's the data-channel), adapter wins on production. Single canonical store: the adapter's `QHash`, with the model row being a write-through.
- **Inv 3 (retire old in same plan):** no existing store to retire — `matchesChanged` was a no-op subscription path that is now properly used.
- **Inv 4 (falsifiable invariant tests first):** §"Invariant 4 falsifiability" above prescribes stub-then-revert for two paths.
- **Inv 5 (production callsite):** integration test loads production `Main.qml` via `markoff-live-app-internal` STATIC library per the `tst_live_render_qml_integration` convention.
- **Inv 6/7 (`Qt.callLater` / re-entrance guards):** none introduced. `setFindSpans` is synchronous; `rehighlight()` is QTextDocument-internal; no callLater anywhere in this design.
- **Inv 8 (Discipline Log):** none added. If implementation surfaces a new smell, log it.

## Definition of done

1. All four new integration test slots pass under `scripts/run-tests.sh -R 'find_match\|current_match_renders\|find_highlights'`.
2. `tst_live_find_adapter` (new) passes.
3. Existing 215/218 baseline preserved — no new failures.
4. Build clean from `cmake --build build-dev -j 8`.
5. Dogfooded from Corbomite `port/foundation-exploration` after submodule re-bump: Ctrl+F in Live mode visibly highlights matches; current match has accent; Next/Prev moves the accent; Esc clears all highlights.
6. Tag `v0.7.0-find-highlights` held pending interactive dogfood.

## Out of scope

- Math-block visible highlighting (matches flow + count, no paint).
- "Highlight all occurrences of selected word" (the slots exist for future reuse; no consumer added).
- Replace-mode visual differentiation (Ctrl+H UI is deferred per the Find UI port spec).
- Search flag toggles (case-sensitive, whole-word, regex) — deferred.
- Search history dropdown — deferred.
- Find result animations / fade-ins — pure paint only.
