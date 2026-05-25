> **Status: superseded.** All three follow-ups (selection overlay, theming, QML integration test) shipped via `78b8f22` + `bd75ecf` + the `tst_view_qml_live_view_qml` series. Do not execute.

# Live Render walking skeleton — fresh-context follow-up SESSION BRIEF

**Read this first.** This document is a self-contained briefing for a fresh-context Claude session picking up the markoff-view-qml Phase-2 v0 walking skeleton after dogfood landed it.

## Status

The walking skeleton **works** end-to-end. Open `./build-dev/bin/markoff-view-qml-app --live <file>` and the editor renders the document as five block-kind delegates (paragraph / heading / hr / image / code-block) with cross-block selection, Ctrl+C copy, and a right-click QMenu wired through the KDAB Widget bridge.

The user dogfooded it on 2026-04-29 and it passes the basic smell test. There are **known cosmetic and behavioral follow-ups** captured in this doc — none are blockers, but they're the natural next chunk of work.

## TL;DR

1. The walking skeleton landed in 13 commits between `799d6b3` and `f172095`.
2. Five real bugs surfaced during dogfood and were fixed in `b03b0d0` (the most recent commit). None of them were caught by the smoke test, which only exercised the C++ model layer.
3. Three follow-ups remain: (a) selection highlight on non-text delegates, (b) theming, (c) a real QML integration test that would have caught the dogfood bugs.

## Required reading (in this order)

1. **Implementation plan** (already executed): `docs/plans/2026-04-29-live-render-walking-skeleton.md`.
2. **Design spec** (load-bearing for any change in this layer): `docs/specs/2026-04-29-live-render-design.md` — especially §4 invariants (8 of them) and §6 plugin extension points.
3. **Spike findings** (the cross-block-selection layer's hard-won lessons): `docs/specs/2026-04-29-cross-block-selection-spike-findings.md`.
4. **Library guide**: `libs/markoff-view-qml/CLAUDE.md` — has a "Phase-2 v0 walking skeleton (in code)" section listing the C++ exports and QML files.
5. **The five bug fixes from dogfood**: `git show b03b0d0` — the commit message itemizes each bug, root cause, and fix. The lessons are reusable for any future delegate work.

## What just landed (for context)

13 commits implementing the plan, plus 1 follow-up commit fixing dogfood bugs:

```
b03b0d0 fix(view-qml): make LiveView delegates actually receive model data
f172095 docs(view-qml): update CLAUDE.md for Phase-2 v0 walking skeleton
69c7213 feat(view-qml): test app --live flag + smoke test for LiveView walking skeleton
3d10db9 feat(view-qml): MarkoffEditor.qml — mode property + Source/Live swap at PHASE-2 seam
88a7bd6 feat(view-qml): LiveView.qml — ListView + DelegateChooser + hit() drag layer
5bede49 feat(view-qml): five v0 block delegates (paragraph, heading, hr, image, code)
1dad9ab feat(view-qml): LiveContextMenuHandler — KDAB Widget-bridged QMenu
f215fb5 feat(view-qml): LiveListModelBinding — parseUpdatedAt → walker → diff → model
b01d9e6 feat(view-qml): LiveBlockModel — QAbstractListModel applying AstBlockDiff ops
4b3203c feat(view-qml): LiveSelectionModel — cross-block selection (per spike)
df6adf0 feat(view-qml): AstBlockDiff — Myers/LCS over BlockKey sequences
14a3c44 feat(view-qml): BlockWalker — source markdown → BlockRecord list
b60f1d3 feat(view-qml): BlockKind + BlockRecord + BlockKey value types
799d6b3 feat(view-qml): adopt QApplication + Qt6::Widgets for KDAB bridge prep
```

33/33 tests pass at HEAD.

## QML 6 lessons (reusable beyond this branch)

Captured here because they cost real debugging time and will recur in any future delegate work.

### 1. `required property` + `DelegateChoice { ... }` is a footgun

In Qt 6.x, declaring `required property string foo` on a delegate inside `DelegateChoice` with an explicit binding `foo: model.fooRole` results in **the property never being assigned** — the role data isn't queried, the binding doesn't fire, the delegate renders with the property's type-default value (empty string, 0, undefined). No QML warnings.

**Workaround:** declare as a non-required property with a default: `property string foo: ""`. The explicit binding then runs as expected.

This is a counter-example to the typical guidance "use `required property` for safety in delegates." With DelegateChoice it actively breaks. The CLAUDE.md note about CompletionPopup's `display` collision (Qt's `ItemDelegate.display` shadowing) is a related-but-different gotcha — both are about `required` going subtly wrong.

### 2. `index` is a delegate context property, not a model role

`model.index` is **undefined** — `index` is provided by the view as a separate context property. Use bare `index` in delegate property bindings: `blockIndex: index`.

### 3. `item.positionAt` only exists on TextEdit, not on Item-wrapped delegates

If the delegate's root is a TextEdit, `item.positionAt(x, y)` works directly. If you wrap it in an Item (which we did to fix Bug 1 above), `item.positionAt` is undefined.

**Pattern:** every text-bearing delegate exposes a proxy `function positionAt(x, y) { return innerTextEdit.positionAt(x, y) }` plus a `readonly property int textLength: innerTextEdit.length`. Non-text delegates (HR, image) have stub `positionAt` returning 0 and `textLength: 0`. The hit-test layer in `LiveView.qml` calls `item.positionAt(...)` uniformly.

### 4. Every delegate that participates in selection needs a `blockIndex` property

`hit()` returns `{ block: item.blockIndex, ... }`. If a delegate doesn't expose `blockIndex`, the fallback collapses to 0 and the selection snaps to row 0. Even non-text delegates (HR, image) need a `blockIndex` so multi-block selection drag-through works correctly.

### 5. Smoke tests on the C++ model don't validate QML wiring

The plan's smoke test at `tests/tst_view_qml_live_view_smoke.cpp` verifies `LiveBlockModel.rowCount() == 5` and the `kind` per row. It passed before AND after the bugs above. It's not a useful regression guard for "do delegates actually display content" or "does selection work."

A real test would: load the QML scene, render it offscreen, simulate mouse events, verify the rendered text via `grabToImage` or via TextEdit accessors. See follow-up §3 below.

## Follow-ups (the actual next-session work)

### 1. Selection highlight on non-text delegates (HR + image)

The user observed: when dragging a multi-block selection that spans the HR or the image, those blocks **don't visually indicate they're selected**, even though `collectSelectedText()` correctly includes them. Files to touch:

- `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml`: when `selectionModel.rangeForBlock(blockIndex).x !== -1`, draw a tinted overlay (e.g. a `Rectangle { color: "#406080"; opacity: 0.3 }` with `anchors.fill: parent`).
- `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml`: same — overlay a translucent rectangle when in selection.
- Both need to know the `selectionModel`. Wire it via the DelegateChoice as a property binding (same as the text-bearing delegates already do).

This requires a small decision: should selection on the image hide the alt-text fallback (or override its color) so the highlight reads visually? Probably yes, with `opacity: 0.7` on the underlying content.

The current image background is solid black (`color: "#222"`); same for code block. Both should respect the project Theme rather than hardcoding. Tracked under §2 below.

### 2. Theming — image + code block have black backgrounds

`ImageDelegate.qml` has `Rectangle { color: "#222" }` for the alt-text fallback. `CodeBlockDelegate.qml` has `Rectangle { color: "#1e1e1e" }` for the code background. Both are hardcoded; both ignore the project's `Theme` value type.

The foundation already has a `Theme` value type with a `Slot` enum. The proper fix routes these colors through the Theme:

- `Theme::Slot::CodeBlockBackground` (or similar)
- `Theme::Slot::ImageBackdrop`

The user's setup (KDE Plasma) likely picks up a Breeze color scheme that would make the code-block background subtle (light grey on light theme; dark grey on dark theme). The current hardcode is theme-blind.

Files: `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml`, `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml`. Threading: the delegates would need a `theme` property propagated via DelegateChoice, similar to `selectionModel`.

### 3. Real QML integration test — the missing regression guard

The plan's `tst_view_qml_live_view_smoke.cpp` is C++-model-only. It misses the QML wiring entirely. Write a `tst_view_qml_live_view_qml.cpp` (offscreen QML test) that:

1. Loads `MarkoffEditor.qml` with `mode: "live"` and a 5-block fixture.
2. Verifies after parse: ListView has 5 instantiated delegates.
3. Verifies each delegate's rendered text matches the model row's text role (use `findChild` on the QML scene + `TextEdit.text`).
4. Synthesizes a click+drag with `QTest::mouseMove`/`QTest::mousePress` from row 1 to row 3.
5. Verifies `LiveSelectionModel.collectSelectedText(...)` after the drag returns text spanning rows 1-3.

This test is what would have caught Bugs 1, 2, and 4 from the dogfood commit.

### 4. Auto-scroll while dragging selection past viewport edge

Cosmetic (but expected behavior). Currently if the user drags past the viewport top/bottom while the document is taller than the viewport, the selection clamps to the last visible row. Native text editors auto-scroll the view. Implementation: a `Timer` that fires while the mouse is outside the viewport during a drag, adjusting `listView.contentY` by some delta per tick, then re-running `hit()`.

This was deferred in the spike (see findings doc §7) and is on the editing-spec todo. Probably worth lifting forward if dogfood reveals more cases where it matters.

### 5. The "SourceEditor is always instantiated" architectural smell (flagged in Task 11)

Task 11's implementer flagged: `EditorBackend` lives inside `SourceEditor.qml` (`id: backend`), exposed via `readonly property alias editorBackend: backend`. `LiveView` consumes it via `sourceEditor.editorBackend`. Even when `mode === "live"` and SourceEditor is `visible: false; enabled: false`, it's still instantiated — its EditorBackend has to stay alive for LiveView to work.

Fix: lift `EditorBackend` to `MarkoffEditor.qml` itself. SourceEditor and LiveView both reference the parent's instance. Then they can be properly Loader-gated, including disposal of unused mode. Not urgent; flag for whenever the editing spec lands or whenever performance dictates.

### 6. The five-bugs-during-dogfood post-mortem

Worth noting in the next session: **all five bugs only manifested when the QML actually rendered with a real document**. The 33/33 test suite was useless for catching them because the tests stop at the model boundary. Lesson for future Phase-2-style work: every delegate-vs-model contract change deserves a runtime QML test. See §3 above.

## Constraints carried forward

- **Don't touch master.** Branch is `exploration/new-foundation`; master is being deprecated.
- **Build with `-j 8`** — bare `-j` freezes the user's machine (per agent memory: `feedback_complex_first_simplify_later` and the explicit constraint in the plan).
- **Eight invariants** (design spec §4) hold. The most-recent-bitten-by ones:
  - #2: `TextEdit.selectByMouse` is `false` everywhere; the top-level MouseArea is the sole input owner.
  - #8: `rangeForBlock`'s `INT32_MAX` sentinel must be clamped by QML consumers.
- **`MarkoffDocument` black-boxes the CRDT.** No `CollabText::Crdt::*` types in the view-qml layer (invariant #7). The future "lighter non-CRDT codepath" is project memory, not in scope.

## How to verify the current state

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_)' --output-on-failure -j 8
# Expected: 33/33 passed.

# Manual smoke:
echo '# Heading

A paragraph with **bold** text.

---

Another paragraph.

![alt](http://example.com/img.png)

```python
print("hi")
```

## Smaller heading

Last paragraph.' > /tmp/live-test.md

./build-dev/bin/markoff-view-qml-app --live /tmp/live-test.md
```

Try: drag-select across paragraphs (column tracking should work), Ctrl+C, paste somewhere to verify the text. Right-click for the context menu (Copy, Select All).

## Out of scope for the follow-up session

- Editing in Live mode (typing into the rendered view) — separate `editing-spec`.
- The other ~14 block kinds (math, mermaid, tables, lists, blockquotes, callouts, links/wikilinks, embedded notes, frontmatter, tags) — per-delegate specs as we add them.
- Plugin loader / theme loader — designed-in but deferred (design spec §6).
- Cross-mode selection sync (Source mode selection ↔ Live mode selection via Session anchors) — editing-spec.

## Notes

- The user is pragmatic, not pedagogical; heads-down build flow.
- The cross-block-selection spike at `.spike/cross-block-selection/` is a runnable reference; build with its own CMakeLists if you need to confirm "what works" looks like.
- The user dogfooded by hand and surfaced the bugs interactively; the controller (me) instrumented C++ side via `fopen("/tmp/live-debug.log", "a")` writes during diagnosis. That worked when QML's `console.log` did NOT (Qt's logging can suppress it). Pattern is reusable for future GUI-side bug hunts.
