# TextControl test coverage — design

**Date:** 2026-04-16
**Status:** approved — moving to implementation plan
**Related docs:** `docs/TODO.md` §Testing, `docs/2026-04-16-codebase-evaluation.md` §2 item 1

## Problem

`src/TextControl.cpp` is a 2,572-LOC fork of Qt's private `QWidgetTextControl`.
It is the state machine behind every `MarkdownTextItem`'s cursor, selection,
input-method handling, and link activation. Today it has **zero direct test
coverage** — the only safety net is transitive coverage through
`MarkdownTextItem` and Editor-level tests. The codebase evaluation names this
as the single largest risk in the library: a regression in cursor movement,
input method handling, preedit composition, or drag-and-drop would be
invisible until a user hit it.

This spec scopes a dedicated regression suite for TextControl.

## Scope

### In scope

Two matched test vehicles:

1. **Direct suite.** Tests instantiate a bare `Markoff::TextControl` + `QTextDocument`
   + dummy context `QWidget`, drive it via `processEvent()`, and assert on
   document / cursor / signal state. No `MarkdownTextItem`, no substitution
   handlers, no reparse pipeline in the loop. A failure points at TextControl,
   period.
2. **Integration suite.** Tests build a real `Editor`, reach through
   `coordinatorForTesting()->items()` to the first `MarkdownTextItem`'s
   `TextControl`, and exercise behaviors that only reproduce with real
   `MathTextObject` / `CheckboxTextObject` handlers attached to U+FFFC glyphs.

Behavior coverage (Tier 1 = fork-specific / known-risky; Tier 2 = general
correctness):

- **Cursor movement** — arrow keys, Home/End, Ctrl+word-left/right, shift-
  modifier extension, `moveCursor()` API parity, traversal past U+FFFC.
- **Selection** — shift+arrow, shift+click anchoring, double-click-word,
  triple-click-line, drag-select press/move/release, `selectAll()`, selection
  spanning U+FFFC, `visibilityRequest` emission when drag reaches a viewport
  edge.
- **Text manipulation** — char insertion via KeyPress, `insertPlainText()`,
  Backspace, Delete, Backspace/Delete adjacent to U+FFFC, read-only mode
  suppression of edits.
- **Input method** — preedit insertion, preedit update replacing prior,
  commit clearing preedit and inserting committed text, `inputMethodQuery()`
  returning sensible cursor/surrounding-text, IME attributes applied. Absorbs
  the existing `tst_cjk_autocorrect.cpp` cases.
- **Link activation** — `anchorAt()` positive/negative, `linkActivated`
  emission on click over anchor, `linkHovered` enter/leave emission, no
  emission on click outside anchor or on empty-href anchor.
- **Substitution-glyph integration** (integration suite only) — cursor
  traversal across real math / checkbox glyphs, Backspace/Delete removing
  glyph + underlying source span, selection spanning real glyphs yielding
  source characters in `toPlainText()`, math reveal on click, mixed math +
  checkbox arrow-key traversal.

### Out of scope (explicitly deferred)

- Drag-and-drop event synthesis (Tier 3).
- Clipboard cut/copy/paste round-trip (Tier 3 — Qt's own layer + existing
  `tst_undo_grouping` give partial indirect coverage).
- Full viewport-scroll autoscroll (direct tests assert `visibilityRequest`
  emission; the viewport-response half is an Editor/QGraphicsView concern).
- Undo/redo plumbing through TextControl (Tier 3).
- Cursor blink timing and focus decoration (Tier 4 — low signal).
- MarkdownHighlighter coverage, broader SceneCoordinator coverage,
  CheckboxTextObject paint/size coverage, cross-item undo, perf benchmark
  harness — all flagged in TODO.md §Testing but out of this round. Separate
  specs if/when prioritized.

### Non-goals

- **No differential-against-upstream-Qt suite.** `QWidgetTextControl` is a
  Qt private class; there is no public base class to diff against. Tests
  describe observed desired behavior; upstream evolution is re-evaluated
  per incident.
- **No fork-vs-fork binary equivalence.** Behavior equivalence at the
  public API level is what the suite enforces.

## File layout

All under `libs/markoff/tests/`:

```
tests/
  support/
    textcontrol_testutil.h      -- header-only; factories + event synthesis
  tst_textcontrol_cursor.cpp    -- direct, cursor movement
  tst_textcontrol_selection.cpp -- direct, selection mechanics
  tst_textcontrol_editing.cpp   -- direct, text insert/delete, read-only
  tst_textcontrol_input.cpp     -- direct, IME/preedit (absorbs tst_cjk_autocorrect.cpp)
  tst_textcontrol_links.cpp     -- direct, anchorAt / linkActivated / linkHovered
  tst_textcontrol_integration.cpp -- indirect through MarkdownTextItem
```

**Removed:** `tests/tst_cjk_autocorrect.cpp` and its `tests/CMakeLists.txt`
registration. Cases absorbed into `tst_textcontrol_input.cpp`.

The existing narrow-file convention is preserved (largest direct file will
be comparable in size to `tst_selection.cpp` at ~16KB). One QTest executable
per file keeps per-slot diagnostic context small and matches the existing
ctest layout.

## `tests/support/textcontrol_testutil.h`

Header-only. No separate TU, no library target. Each test TU that needs it
includes the header; inline / static functions avoid ODR trouble.

Provides:

- **`TestPlaceholderObject`** — a `QObject` implementing
  `QTextObjectInterface`. Reports a fixed 10×16 intrinsic size; paints
  nothing (or a 1×1 debug rect under a compile flag). Uses the sentinel
  `QTextFormat::UserObject + 100` as its `TypeId` — safely clear of
  `MathTextObject::TypeId` (`UserObject + 1`) and
  `CheckboxTextObject::TypeId` (`UserObject + 2`).
- **`TextControlFixture`** — RAII struct owning a `QTextDocument`,
  `TextControl`, dummy `QWidget` (context), and `TestPlaceholderObject`
  handler. Constructor registers the placeholder object with the document's
  layout and wires `TextControl::setDocument()`.
- **`makeFixture(text, cursorPos = 0)`** — returns a `TextControlFixture`
  with document pre-loaded via `setPlainText()` and cursor positioned.
- **`insertPlaceholder(QTextCursor &c)`** — inserts a U+FFFC char whose
  `QTextCharFormat::objectType()` is the sentinel.
- **`sendKey(tc, key, modifiers = NoModifier, text = "")`** — builds a
  `QKeyEvent(KeyPress)` and routes it via `tc.processEvent()`.
- **`sendMousePress/Move/Release(tc, pos, buttons, modifiers)`** — builds
  `QMouseEvent` and routes via `processEvent(..., coordinateOffset)`.
- **`sendInputMethod(tc, commit, preedit, attributes = {})`** — builds
  `QInputMethodEvent` and routes.
- Integration-suite editor construction is handled inline in
  `tst_textcontrol_integration.cpp` via a local `makeEditor()` helper
  that builds an `Editor`, waits for reparse debounce, and returns the
  first `MarkdownTextItem`. It is not placed in `textcontrol_testutil.h`
  because including `Editor.h` there would pull widget-level deps into
  every direct-test TU that needs only `TextControl.h`.

The helpers intentionally stay thin: they synthesize events the same way
Qt would in production, so the tests exercise the real event path, not a
synthetic one that skips dispatch.

## Test enumeration (target counts)

Approximate targets. Real counts set during implementation; deviations are
fine as long as tier coverage is complete.

- `tst_textcontrol_cursor.cpp` — ~10-12 tests. Arrow keys × 4 directions,
  Home/End, Ctrl+word × 2 directions, shift-modifier extension, `moveCursor()`
  API parity with keyboard, cursor-right-over-placeholder, cursor-left-
  over-placeholder, Ctrl+word-across-placeholder.
- `tst_textcontrol_selection.cpp` — ~10-12 tests. shift+arrow extend,
  shift+click anchor, double-click word, triple-click line, drag-select
  (press/move/release), `selectAll()`, selection across placeholder
  yields placeholder glyph in selected text, `visibilityRequest` emits
  when drag reaches configured edge.
- `tst_textcontrol_editing.cpp` — ~8-10 tests. KeyPress insert, Backspace,
  Delete, `insertPlainText()`, Backspace across placeholder,
  Delete across placeholder, read-only rejects KeyPress, read-only rejects
  `insertPlainText()` if API honors flag (document if it does not).
- `tst_textcontrol_input.cpp` — ~8-10 tests. Preedit insert, preedit
  update, preedit commit, `inputMethodQuery()` cursor/surrounding text,
  preedit attributes applied, plus CJK autocorrect cases migrated from
  existing `tst_cjk_autocorrect.cpp`.
- `tst_textcontrol_links.cpp` — ~5-7 tests. `anchorAt()` on anchor
  positive, off anchor negative, click-on-anchor emits `linkActivated`,
  hover enter/leave emits `linkHovered`, click off anchor silent, empty
  href silent.
- `tst_textcontrol_integration.cpp` — ~8-12 tests. Real math-glyph cursor
  traversal right/left, real checkbox-glyph traversal, Backspace adjacent
  to real math glyph removes glyph + underlying source span, selection
  spanning real math glyph includes source chars in `toPlainText()`,
  math reveal on click, checkbox click toggle (MarkdownTextItem path, not
  TextControl's responsibility — asserts it reaches the right handler),
  mixed math+checkbox line traversal.

Total estimate: ~50-60 tests.

## CMake changes

`tests/CMakeLists.txt` has no helper macro — each test target gets the
standard 5-line block already used for the existing tests:

```cmake
add_executable(tst_markoff_<name> tst_<name>.cpp)
add_test(NAME tst_markoff_<name> COMMAND tst_markoff_<name>)
target_link_libraries(tst_markoff_<name>
    PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_<name>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR}/support)
set_tests_properties(tst_markoff_<name>
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Changes:

1. Remove the `tst_markoff_cjk_autocorrect` registration block.
2. Add six blocks — one per new test file
   (`textcontrol_cursor`, `textcontrol_selection`, `textcontrol_editing`,
   `textcontrol_input`, `textcontrol_links`, `textcontrol_integration`).
3. Each new block adds `support/` to its include path as shown above so
   the header-only helpers resolve.

No new library target. No changes to the main `libs/markoff/CMakeLists.txt`.

## Risks and mitigations

- **`processEvent()` with a bare `TextControl` may behave differently than
  inside a live `QGraphicsView` event loop.** Specifically, viewport-
  dependent behavior (page up/down, autoscroll) may return fallback results.
  Mitigation: these tests assert emitted signals and observable document
  state, not viewport math. Where direct tests cannot meaningfully exercise
  a behavior, the case goes into the integration suite instead.
- **Qt private-header drift.** TextControl depends on a handful of Qt
  private headers. A Qt point-release that changes one could break the fork;
  this suite is the intended detector. On breakage, the triage path is
  upstream reconciliation, not test weakening.
- **Test-only `QTextObjectInterface` colliding with
  `MathTextObject::ObjectType` / `CheckboxTextObject::ObjectType`.**
  Mitigation: pick a sentinel value unused in the production enum and
  document it in `textcontrol_testutil.h`. Collision would manifest as
  a test-harness assertion, not silent corruption.
- **IME/preedit tests are platform-sensitive.** CJK input attribute
  handling differs across QPA backends. Tests run under
  `QT_QPA_PLATFORM=offscreen` to pin behavior. If a test is inherently
  platform-dependent, it carries a comment explaining why.

## Success criteria

- All six new test executables build, and every slot passes under
  `QT_QPA_PLATFORM=offscreen` on the existing CI target.
- `tst_cjk_autocorrect` is removed from the build without losing any of
  its existing assertions (all migrated into `tst_textcontrol_input.cpp`).
- Running the full markoff ctest suite passes. No existing test is
  broken by the additions.
- A subsequent intentional regression (e.g. swap `QTextCursor::Right` with
  `QTextCursor::Left` in TextControl's key dispatch) is caught by the
  direct cursor suite — i.e. the suite actually detects regressions, not
  just compiles.

## Out-of-scope follow-ups (next rounds)

Separate specs, separate rounds:

- MarkdownHighlighter regression suite.
- Expanded SceneCoordinator coverage beyond the single
  `reparsedHandlerEditIsNotSwallowed` regression test.
- CheckboxTextObject paint/size/property round-trip coverage.
- Cross-item undo coordination (requires an architecture decision per
  TODO.md — blocked on spec).
- Performance benchmark harness.

These stay tracked under TODO.md §Testing until scheduled.
