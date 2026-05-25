# markoff-live — freeze-shape design

**Date:** 2026-05-19
**Branch:** `exploration/new-foundation`
**Status:** design pre-dates 2026-05-20 port-first pivot. Most D-rows landed (D1-D8, D10); D9 superseded by find-session-scope. Remaining unlanded items are accepted-smell decisions, not future work. **No further amendments** without port-evidence pressure.
**Companions:** `docs/2026-05-18-public-api-surface-audit.md` §markoff-live, `docs/specs/2026-05-18-markoff-source-freeze-shape-design.md`, `docs/specs/2026-05-20-find-session-scope-design.md`, `docs/handoff/2026-05-07-pivot-to-d5-first.md` §4.6.

## Amendment log

- **2026-05-20 — D9 superseded.** The QML FindBar / `LiveFindController` Q_INVOKABLE / `LiveView.qml` Item-wrapper introduced in D9 were reverted by the find-session-scope spec; find UI is now consumer-owned, find adapter is internal under `Detail::`. D9 is retained below for provenance but does not reflect the post-2026-05-20 shape.
- **2026-05-20 — D11/D12 proposed and withdrawn same day.** A brief amendment added D11 (`Capabilities::Editable`) and D12 (`Markoff::Live::EditorWidget`) as cross-leaf items pulled out of a `markoff-core` freeze draft. Both were withdrawn within hours when the user pivoted to port-first: read-only Live and the QQuickWidget wrapper will get micro-specs when an actual Corbomite use case pulls on them (HoverPopover, `NoteEditorWidget::activeLeaf()`), not before. The withdrawal is recorded here in case the items return.

## Purpose

`markoff-live` is the largest of the three shipping leaves (24 public headers + a substantial QML module). The audit surfaced eight shape questions for the §4.6 API freeze; brainstorming identified two more during exploration (legacy code cleanup, cross-leaf find UI). This spec captures decisions for all ten.

The substantive new addition is a QML-native find UI on the live leaf, symmetric with the source-leaf find UI that landed in the prior freeze (`docs/specs/2026-05-18-markoff-source-freeze-shape-design.md`). Everything else is API tightening — namespace `Detail::` relocations, dead-code removal, accepted-smell documentation.

## Scope

In scope:
- Public surface of `libs/markoff-live/include/markoff/live/` (24 headers).
- Tier-4c phase-C closeout: `LiveSelectionView` removal.
- Pre-rewrite orphaned code in `libs/markoff-live/include/markoff/` (outside `live/`) and `libs/markoff-live/src/`.
- A new QML find UI: `Markoff::Live::FindBar` (QML component) + `Markoff::Live::LiveFindController` (C++ Q_OBJECT) + Q_INVOKABLE accessors on `LiveListModelBinding`.

Out of scope (tracked separately):
- Plugin-kind dynamic dispatch (the unfinished `delegateUrl` consumer path).
- E5 Math text-bearing (would retire Q3's transitional flag).
- Edit-pipeline echo-suppression redesign (would retire Q7's flag).
- Math BlockInternalEdit focus chokepoint protocol (would retire Q8's `Qt.callLater`).
- ReplaceBar in either leaf.
- Regex search, whole-word / case-sensitivity toggles, cross-block-spanning matches.

## Decisions

### D1. `LiveListModelBinding` keeps its name

**Decision:** No rename. The host's entry point stays `Markoff::Live::LiveListModelBinding`.

**Why:** The name is verbose but accurately describes the role: a Q_OBJECT that binds a list-model-bearing QML view to a `MarkoffDocument` plus sub-controllers. The class is established through E3a and prior arcs; renaming has real sweep cost (~10 source files, ~30 tests, project memories, the test app) for marginal clarity win. Asymmetry with `Markoff::Source::Editor` (single-word) reflects a real architectural difference: source's `Editor` is a `QWidget`; the live binding is NOT a widget — it's the QObject bridge to a QML view that the host instantiates separately via `QQuickWidget`.

**How to apply:** No code change. The freeze spec documents `LiveListModelBinding` as the canonical host entry point in the migration guide.

### D2. Delete `LiveSelectionView`; selection methods move to `LiveCursorState`

**Decision:** Retire `LiveSelectionView` entirely. The 11 selection methods currently on the facade move to `LiveCursorState` as `Q_INVOKABLE`s. QML callsites migrate from `binding.selectionView.X(...)` to `binding.cursorState.X(...)`.

**Why:** Tier-4c made `LiveSelectionView` a stateless facade — all canonical state moved to `LiveCursorState`. Phase C of that arc planned to retire the facade. The freeze is the natural place to close this loop: one canonical store for cursor + selection, one name for that store, no transitional shim shipped into the frozen API.

**How to apply:**

- Delete `libs/markoff-live/include/markoff/live/LiveSelectionView.h` and `libs/markoff-live/src/LiveSelectionView.cpp`.
- Remove `LiveSelectionView *` member from `LiveListModelBinding` (the `selectionView` Q_PROPERTY also retires).
- Add `Q_INVOKABLE` selection methods to `LiveCursorState`:
  - `begin(int blockIndex, int qtPos)`
  - `extend(int blockIndex, int qtPos)`
  - `clear()`
  - `selectAll()`
  - `deleteSelection()`
  - `rangeForBlock(int blockIndex) -> QVariantMap` (returns `{start, end}` or null)
  - `copyToClipboard()`
  - `anchorBlock() -> int`
  - `anchorQtPos() -> int`
  - `activeBlock() -> int`
  - `activeQtPos() -> int`
  - These delegate to the existing `m_selectionAnchor` / `m_cursor` canonical state.
- Migrate the `binding.selectionView.X(...)` callsites in `libs/markoff-live/qml/LiveView.qml` to `binding.cursorState.X(...)`. The current count is 13 lines (per `grep -n 'binding\.selectionView' libs/markoff-live/qml/LiveView.qml` at spec time). Mechanical sweep.
- Migrate `tst_live_render_selection_cursor_unification` (7 slots from tier-4c) and any other selection-related test that constructs `LiveSelectionView` directly to call `LiveCursorState` instead.
- Update `libs/markoff-live/CLAUDE.md` to remove the "Selection state (tier 4c phase A)" mention and reflect the unified `LiveCursorState` story.

### D3. `BlockKindRegistry::isBlockOnly` Math asymmetry kept; documented as transitional

**Decision:** Keep the explicit `isBlockOnly = false` setting for Math despite the absence of `TextCaret` in its `supportedCursorVariants`. Update the docstring to explain.

**Why:** Removing the flag (deriving from `supportedCursorVariants`) is a runtime behaviour change for Math with regression risk in structural-key dispatch. E5 (math/mermaid live-mode parity) makes Math text-bearing; the flag retires then. Freeze locks the current behaviour.

**How to apply:**
- Edit `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h`: update the `isBlockOnly` docstring to: *"Whether this kind cannot host a text caret. Derived from `supportedCursorVariants` in practice — except for transitional kinds (currently Math) where the explicit flag retains current behaviour pending the kind's text-bearing redesign (e.g. E5 for Math)."*
- Existing entries in `BlockKindRegistry.cpp` stay.

### D4. `AstBlockDiff` + `Coordinates` move to `Markoff::Live::Detail::`

**Decision:** Both types move to a `Detail::` sub-namespace, matching the source-leaf precedent (`Markoff::Source::Detail::Gutter`).

**Why:** Both are algorithm-like internal helpers — `AstBlockDiff` is the Myers/LCS diff used by `LiveListModelBinding::applyOps`; `Coordinates` is byte↔QtPos UTF-8 mapping used by `LiveEditBinding`. Neither is part of the consumer-facing story; the `Detail::` namespace signals that. Headers stay in `include/markoff/live/` so tests continue to compile against them (no CMake test-target gymnastics required).

**How to apply:**
- Wrap `AstBlockDiff` in `namespace Markoff::Live::Detail { ... }` in both `include/markoff/live/AstBlockDiff.h` and `src/AstBlockDiff.cpp`.
- Wrap `Coordinates` namespace functions in `namespace Markoff::Live::Detail { ... }` in both `include/markoff/live/Coordinates.h` and `src/Coordinates.cpp`.
- Call sites in `LiveListModelBinding.cpp`, `LiveEditBinding.cpp`, and others get a `using Detail::AstBlockDiff;` / `namespace coords = Detail::Coordinates;` near the top of their `namespace Markoff::Live { ... }` block — same convention as `markoff-source` settled into for `Detail::Gutter` / `Detail::InnerEditor`.

### D5. `BlockKindDescriptor::delegateUrl` kept as populated annotation

**Decision:** Keep the field. Document it as a queryable annotation, not consumed by the current dispatch.

**Why:** The field is populated for all 8 built-in kinds in `BlockKindRegistry.cpp` and contains correct URLs. Removing it loses an information channel (debuggers, DevTools, future plugin-kind machinery) for no current cost. Plugin-kind dynamic dispatch is a future spec; this field is its placeholder.

**How to apply:** Update the field's docstring in `BlockKindDescriptor.h` to: *"qrc: URL of the QML delegate. Populated for built-in kinds as a queryable annotation; NOT consumed by `LiveView.qml`'s `DelegateChooser` (which dispatches on `delegateClass`). Plugin-kind dynamic dispatch is a future spec that would wire this through a Loader-based delegate selector."*

### D6. `BlockRecord::inlineSpans` stale comment refreshed

**Decision:** Update the comment that calls `inlineSpans` dead code; it's load-bearing for E1 inline highlighting.

**How to apply:** Edit `libs/markoff-live/include/markoff/live/BlockRecord.h`. The stale `// excluded ... consumed by InlineFormatHighlighter in R6`-era language becomes: *"Per-block inline-format spans (bold/italic/code/link/wikilink/tag/etc.) populated from the AST and consumed by `InlineHighlighter` (E1). Load-bearing — do not strip."*

### D7. `LiveEditBinding::m_applyingTextUpdate` + Q_INVOKABLE accessor kept; logged as accepted smell

**Decision:** The flag and its `isApplyingTextUpdate()` Q_INVOKABLE stay. Inline comment updated; discipline-log entry added.

**Why:** Two production QML delegates read the accessor to suppress echo-edit reactions (`CodeBlockDelegate.qml:65`, `UnifiedInlineTextDelegate.qml:202`). Removing it requires re-architecting the edit-echo pipeline (signal-blocking, scoped guards on the inner QTextDocument, or per-edit-path routing). That's a focused future spec, out of freeze prep.

**How to apply:**
- Inline comment update at `LiveEditBinding.cpp:73`: *"Re-entrance guard for the `pushTextToDocument` ↔ `onContentsChange` echo loop. Accepted smell at freeze (invariant 7); see `docs/queue.md` Discipline Log. Future redesign tracked in the edit-pipeline echo-suppression spec (TBW)."*
- Append entry to `docs/queue.md` Discipline Log: `2026-05-19 libs/markoff-live/src/LiveEditBinding.cpp:73 — inv #7 — m_applyingTextUpdate + isApplyingTextUpdate() Q_INVOKABLE accessor read by CodeBlockDelegate.qml:65 and UnifiedInlineTextDelegate.qml:202 to suppress echo reactions. Removal requires re-architecting the QTextDocument↔MarkoffDocument echo loop (signal-blocking or per-edit-path routing). Frozen with explicit acceptance; redesign spec TBW.`

### D8. `Qt.callLater` at `MathDelegate.qml:125` kept; logged as accepted smell

**Decision:** The single deferral site stays. Inline comment + discipline-log entry.

**Why:** Single isolated site; the deferral handles a real QML state-propagation timing issue — `cursorState.request({variant: "BlockInternalEdit", ...})` mutates state, and `latexEdit.forceActiveFocus()` needs to run after QML bindings re-evaluate (latexEdit's visibility, focus chain). Removing requires extending the cursor chokepoint protocol to emit a synchronous focus-takeover signal that delegates handle — cross-delegate ripple work; E5 redesigns Math interactions and the callLater retires then.

**How to apply:**
- Inline comment update at `MathDelegate.qml:125`: *"Deferral: forceActiveFocus needs to run after the BlockInternalEdit cursor variant change propagates through QML bindings (latexEdit becomes the focus target). Accepted smell at freeze (invariant 6); see `docs/queue.md` Discipline Log. E5 (math live-mode parity) will redesign this with a chokepoint-routed focus signal."*
- Append entry to `docs/queue.md` Discipline Log: `2026-05-19 libs/markoff-live/qml/delegates/MathDelegate.qml:125 — inv #6 — Qt.callLater defers forceActiveFocus after a BlockInternalEdit cursor variant change so QML bindings re-evaluate (latexEdit's visibility/focus chain) before focus takeover. Single isolated site; redesigning requires extending the cursor chokepoint protocol with a focus-takeover signal. Frozen with explicit acceptance; E5 retires.`

### D9. QML FindBar + `LiveFindController` (new, symmetric with source find UI)

**Decision:** Live grows a QML-native find UI in this freeze. New artifacts:
- `libs/markoff-live/qml/FindBar.qml` — the visual component.
- `libs/markoff-live/include/markoff/live/LiveFindController.h` + `src/LiveFindController.cpp` — the C++ controller.
- `LiveListModelBinding::showFindBar()` / `hideFindBar()` Q_INVOKABLEs (NOT `MarkdownView` overrides — see "Cross-leaf reality" below).

**Why:** The source-leaf freeze (`docs/specs/2026-05-18-markoff-source-freeze-shape-design.md` D2) made `showFindBar`/`hideFindBar` real on `Markoff::Source::Editor`. Symmetry across leaves is part of API finalization for Corbomite consumption. Live's render layer is QML, so the FindBar lives in QML; the C++ controller drives the search loop.

**`LiveFindController` API:**

```cpp
namespace Markoff::Live {

class MARKOFF_LIVE_EXPORT LiveFindController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveFindController is provided by LiveListModelBinding")
    Q_PROPERTY(QString needle READ needle WRITE setNeedle NOTIFY needleChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
    Q_PROPERTY(int currentMatchIndex READ currentMatchIndex NOTIFY currentMatchChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY activeChanged)
public:
    explicit LiveFindController(QObject *parent = nullptr);

    QString needle() const;
    void setNeedle(const QString &);

    int matchCount() const;
    int currentMatchIndex() const;
    bool isActive() const;

    Q_INVOKABLE void activate();    // sets isActive(true); recomputes matches.
    Q_INVOKABLE void deactivate();  // sets isActive(false); clears matches + highlights.
    Q_INVOKABLE void findNext();    // advances currentMatchIndex (wraps).
    Q_INVOKABLE void findPrevious();// retreats currentMatchIndex (wraps).

    // Internal wiring (called by LiveListModelBinding):
    void setBlockModel(LiveBlockModel *);
    void setCursorState(LiveCursorState *);

Q_SIGNALS:
    void needleChanged();
    void matchesChanged();
    void currentMatchChanged();
    void activeChanged();

private:
    struct Match { int row; int startQtPos; int length; };
    void recomputeMatches();
    void seekToCurrent();
    // ...
};

} // namespace Markoff::Live
```

**Search behaviour:**
- On `setNeedle`: iterate `LiveBlockModel` rows; for each block's text, find all matches of the needle (case-insensitive by default); build the `Match` list.
- On `findNext` / `findPrevious`: advance/retreat `currentMatchIndex` (wraps at boundaries); call `cursorState->requestTextCaretAtRow(row, qtPos)` to scroll + place caret on the match's start; the per-delegate TextEdit applies a selection covering the match.
- On `deactivate`: clear matches, clear highlights.

**Match-highlighting strategy: `extraSelections`.** Mirrors source's `FindBar` semantics. The QML `FindBar.qml` reads the current match's `(row, startQtPos, length)` and, via property bindings on the active delegate's inner TextEdit (or via a per-block QML state map), pushes a single `ExtraSelection` onto the TextEdit. `LiveBlockModel` rows that are NOT the current match show no extra selection. (Alternative considered: extend `InlineHighlighter` with a "find-match" inline kind. Rejected: it would couple find-state into the inline-spans pipeline, which is already a load-bearing E1 path.)

**`LiveListModelBinding` integration:**
- New private member `LiveFindController *m_findController = nullptr;` lazy-instantiated on first `showFindBar()`.
- New Q_PROPERTY `LiveFindController *findController` exposed to QML.
- New Q_INVOKABLEs `void showFindBar()` and `void hideFindBar()` that flip `m_findController->isActive`. The QML `FindBar.qml` component bound to `binding.findController` reacts to the property change.

**`Markoff/Live/FindBar.qml` layout (sketch):**

```qml
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    required property var liveBinding
    readonly property var fc: liveBinding ? liveBinding.findController : null

    visible: fc && fc.isActive
    height: visible ? 32 : 0
    color: "#f0f0f0"  // theme via liveBinding.themeColorFor(Theme.FindBarBackground) when wired

    Row {
        anchors.fill: parent
        spacing: 4
        TextField {
            id: needleInput
            text: fc ? fc.needle : ""
            onTextChanged: if (fc) fc.setNeedle(text)
            Keys.onReturnPressed: if (fc) fc.findNext()
            Keys.onEscapePressed: root.requestClose()
        }
        Button { text: "<"; onClicked: if (fc) fc.findPrevious() }
        Button { text: ">"; onClicked: if (fc) fc.findNext() }
        Label {
            text: fc ? (fc.matchCount > 0
                ? (fc.currentMatchIndex + 1) + "/" + fc.matchCount
                : "0/0") : ""
        }
        Button { text: "×"; onClicked: root.requestClose() }
    }

    signal closed()
    function requestClose() {
        if (fc) fc.deactivate()
        root.closed()
    }
}
```

**`LiveView.qml` integration:** the `FindBar` is anchored to the top of the LiveView, overlaying the ListView when active. The ListView's `anchors.top` adjusts to leave room when `FindBar.visible`. When inactive, the bar collapses to height 0; ListView reclaims the space.

**Cross-leaf reality.** The `Markoff::MarkdownView` base class declares `showFindBar` / `hideFindBar` / `showReplaceBar` virtuals with default no-op bodies. `Markoff::Source::Editor` overrides them (per the source freeze D2). The live leaf has no `MarkdownView` subclass — `LiveListModelBinding` is a Q_OBJECT, not a QWidget — so it cannot override `MarkdownView::showFindBar`. Instead, live exposes `showFindBar` / `hideFindBar` as Q_INVOKABLEs directly on `LiveListModelBinding`. Hosts holding a `MarkdownView *` get polymorphic dispatch on source widgets; hosts holding a `LiveListModelBinding *` call the Q_INVOKABLEs directly on live. This asymmetry is documented in the migration guide. `showReplaceBar` is NOT exposed on live (consistent with source's no-op deferral).

### D10. Legacy orphaned code deleted

**Decision:** Delete pre-rewrite headers and sources that aren't in `CMakeLists.txt`.

**Files removed:**

Public headers (in `libs/markoff-live/include/markoff/`, NOT inside `live/`):
- `Editor.h`
- `EditorContext.h`
- `FoldingTypes.h`
- `LinkRenderer.h`
- `ResourceProvider.h`
- `Theme.h`

Source files (in `libs/markoff-live/src/`, NOT in CMakeLists):
- `Editor.cpp`
- `BlockItem.cpp`, `BlockItem.h`
- `CheckboxTextObject.cpp`, `CheckboxTextObject.h`
- `EditorContextClassifier.cpp`, `EditorContextClassifier.h`
- `FoldArrowColumn.cpp`
- `FoldGutter.cpp`, `FoldGutter.h`
- `FoldingModel.cpp`, `FoldingModel.h`
- `FoldingTypes.cpp`
- `GutterColumn.h`
- `ImageBlockItem.cpp`, `ImageBlockItem.h`
- `LinkRenderer.cpp`
- `SceneCoordinator.cpp` (verified not in build via `grep` against `CMakeLists.txt`)

**Verification:**
- `grep` confirmation that none appear in `libs/markoff-live/CMakeLists.txt` before deletion.
- Post-deletion build: `cmake --build build-dev -j 8` succeeds.
- Test suite: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` stays at 216/216.

Git history preserves the retired implementations.

## New public surface (post-freeze)

### `LiveListModelBinding` (entry point)

```cpp
namespace Markoff::Live {

class MARKOFF_LIVE_EXPORT LiveListModelBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Sub-controllers (8 Q_PROPERTYs — selectionView REMOVED)
    Q_PROPERTY(LiveBlockModel *model READ model CONSTANT)
    Q_PROPERTY(LiveCursorState *cursorState READ cursorState CONSTANT)
    Q_PROPERTY(BlockHitTester *hitTester READ hitTester CONSTANT)
    Q_PROPERTY(LiveStructuralKeyHandler *structuralKeyHandler READ structuralKeyHandler CONSTANT)
    Q_PROPERTY(LiveNavigationController *navigationController READ navigationController CONSTANT)
    Q_PROPERTY(LiveClipboardController *clipboardController READ clipboardController CONSTANT)
    Q_PROPERTY(LiveActionController *actionController READ actionController CONSTANT)
    Q_PROPERTY(LiveFormatController *formatController READ formatController CONSTANT)
    Q_PROPERTY(LiveContextMenuHandler *contextMenuHandler READ contextMenuHandler CONSTANT)
    Q_PROPERTY(LiveFindController *findController READ findController CONSTANT)  // NEW
    // ... other Q_PROPERTYs (theme, fontScale, etc.) unchanged ...

public:
    explicit LiveListModelBinding(Capabilities = NoCapabilities, QObject *parent = nullptr);

    // ... existing Q_INVOKABLEs ...
    Q_INVOKABLE void showFindBar();   // NEW: toggles findController->activate()
    Q_INVOKABLE void hideFindBar();   // NEW: toggles findController->deactivate()
};

} // namespace Markoff::Live
```

### `LiveCursorState` (gains selection methods)

The 11 selection Q_INVOKABLEs migrated from `LiveSelectionView`. The existing cursor API and `m_selectionAnchor` / `m_cursor` canonical state are unchanged.

### `LiveFindController` (new)

See D9 above.

### `Markoff::Live::Detail::AstBlockDiff` + `Markoff::Live::Detail::Coordinates` (relocated)

Headers stay in `include/markoff/live/`; namespaces nest into `Detail`.

### Removed

- `LiveSelectionView` (class, header, source, QML registration, all callsites).
- 6 legacy public headers (`Editor.h`, `EditorContext.h`, `FoldingTypes.h`, `LinkRenderer.h`, `ResourceProvider.h`, `Theme.h`).
- ~18 legacy source files in `libs/markoff-live/src/` — the full list is in D10's "Files removed" subsection.

## Testing strategy

### Mechanical changes (D1, D3, D4, D5, D6, D10)

Rely on the existing 216-test fast suite. No new tests needed.

### Behavioural changes

| Decision | New / migrated test |
|---|---|
| D2 — `LiveSelectionView` deleted | Existing `tst_live_render_selection_cursor_unification` (7 slots from tier-4c) verifies against the new call paths. Any test constructing `LiveSelectionView` directly is migrated to `LiveCursorState`. |
| D9 — QML FindBar + LiveFindController | New `tst_live_find_controller` (C++ unit slots: needle search across blocks, match navigation wrap-around, deactivate clears matches, multiple blocks with different needle hits). New slot `find_bar_typing_highlights_and_navigation` in `tst_live_render_qml_integration` for end-to-end show/type-needle/match-highlights/find-next/hide. |
| D7, D8 — accepted smells | No new tests; existing coverage continues to exercise these paths. |

### Discipline-Log entries

Two new entries in `docs/queue.md` Discipline Log (per D7, D8). One mechanical update to the existing entry from the prior session (m_applyingSessionSelection is retired — confirm-it-stays-struck check).

## Commit phasing

Six commits, ordered smallest-blast-radius first:

1. **Legacy cleanup (D10).** Delete orphaned headers + sources. Tree hygiene; zero behavioural change. Gate: `cmake --build build-dev -j 8` succeeds; 216/216 tests still pass.

2. **`Detail::` relocation (D4).** Move `AstBlockDiff` + `Coordinates`. Mechanical namespace + using-decl sweep. Gate: 216/216 tests still pass.

3. **`LiveSelectionView` removal (D2).** Delete the facade; add 11 Q_INVOKABLEs to `LiveCursorState`; migrate 15 QML callsites; migrate any tests that construct the facade directly. Gate: 216/216 tests still pass (with `tst_live_render_selection_cursor_unification` exercising the migrated call paths).

4. **Documentation + annotation updates (D3, D5, D6, D7-inline, D8-inline).** Docstring touches for `isBlockOnly`, `delegateUrl`, `inlineSpans`. Inline comments at the smell sites. Gate: 216/216 tests still pass; build green.

5. **QML FindBar + `LiveFindController` (D9).** New header + source + QML component; `LiveListModelBinding` Q_PROPERTY + Q_INVOKABLEs; `LiveView.qml` integration; new C++ + QML-integration tests. Gate: source suite + 2 new find tests pass; full fast suite green.

6. **Discipline-Log entries (D7, D8).** Append to `docs/queue.md`. Gate: build/tests untouched.

## Risks

1. **D2's 15-callsite QML sweep** has the largest immediate blast radius. Mitigation: tier-4c established the canonical store; the `tst_live_render_selection_cursor_unification` suite was specifically built to catch regressions in this seam.
2. **D9's QML FindBar** is the only NEW substantive code in the spec. Risk surfaces: highlight rendering integration with `InlineHighlighter`'s existing E1 pipeline (mitigated by choosing `extraSelections` rather than inline-kind), search performance on large documents (mitigated by case-insensitive substring match; not regex), and focus handling between needle TextField and the LiveView (mitigated by following the source FindBar's focus pattern).
3. **D10's deletion sweep** could orphan a `#include "Editor.h"` reference somewhere in the worktree that the build doesn't surface (because the file isn't a dependency). Mitigation: `grep -rn '"Editor\.h"\|<markoff/Editor\.h>'` (and similar for each deleted header) before committing the deletion.
4. **Cross-leaf find UI asymmetry**. Source uses `MarkdownView::showFindBar` polymorphic virtual; Live uses Q_INVOKABLE on `LiveListModelBinding`. Hosts must hold the right pointer type for find. Documented in the migration guide; cannot be unified without making either source's Editor a QObject (wrong) or live's binding a QWidget (wrong).

## Open questions deferred

- Plugin-kind dynamic dispatch — when (or whether) to wire `delegateUrl` through a Loader-based `DelegateChooser` replacement. Speculative; no consumer signal.
- Math text-bearing (E5) — retires D3's transitional `isBlockOnly` and D8's `Qt.callLater`.
- Edit-pipeline echo-suppression redesign — retires D7's `m_applyingTextUpdate`.
- ReplaceBar in either leaf — requires a Replace UX design across views.
- Find UX features: regex, case-sensitivity toggle, whole-word toggle, cross-block matches.
