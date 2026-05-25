# markoff-live freeze-shape — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the ten freeze-shape decisions from `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md` for `libs/markoff-live`: delete pre-rewrite orphans, relocate internal types to `Detail::`, retire `LiveSelectionView` and consolidate selection methods on `LiveCursorState`, refresh annotations, add a new QML-native find UI symmetric with the source-leaf find UI, and log accepted invariant smells.

**Architecture:** Six commits, ordered smallest-blast-radius first. Tasks 1–4 are mechanical or documentation; Task 5 ships the only substantive new code (LiveFindController + FindBar.qml + LiveListModelBinding wiring + integration test); Task 6 appends to the Discipline Log.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, QML, KSyntaxHighlighting. Tests via `scripts/run-tests.sh` (defaults to `QT_QPA_PLATFORM=offscreen`).

---

## File structure

Files created/modified/deleted, grouped by responsibility:

```
libs/markoff-live/
├── include/markoff/
│   ├── Editor.h, EditorContext.h, FoldingTypes.h, LinkRenderer.h,
│   ├── ResourceProvider.h, Theme.h               # Task 1 (DELETE: orphaned)
│   └── live/
│       ├── AstBlockDiff.h                        # Task 2 (Detail:: namespace)
│       ├── BlockKindDescriptor.h                 # Task 4 (docstring D3, D5)
│       ├── BlockRecord.h                         # Task 4 (comment D6)
│       ├── Coordinates.h                         # Task 2 (Detail:: namespace)
│       ├── LiveCursorState.h                     # Task 3 (11 Q_INVOKABLE selection methods)
│       ├── LiveFindController.h                  # Task 5 (CREATE)
│       ├── LiveListModelBinding.h                # Task 3 (remove selectionView)
│       │                                         # Task 5 (add findController + show/hideFindBar)
│       └── LiveSelectionView.h                   # Task 3 (DELETE)
├── src/
│   ├── AstBlockDiff.cpp                          # Task 2 (Detail:: namespace)
│   ├── Coordinates.cpp                           # Task 2 (Detail:: namespace)
│   ├── LiveCursorState.cpp                       # Task 3 (impl of 11 methods)
│   ├── LiveEditBinding.cpp                       # Task 4 (inline comment D7)
│   ├── LiveFindController.cpp                    # Task 5 (CREATE)
│   ├── LiveListModelBinding.cpp                  # Task 3 (selectionView removal)
│   │                                             # Task 5 (findController wiring)
│   ├── LiveSelectionView.cpp                     # Task 3 (DELETE)
│   ├── Editor.cpp, BlockItem.{cpp,h},            # Task 1 (DELETE: orphaned)
│   ├── CheckboxTextObject.{cpp,h},
│   ├── EditorContextClassifier.{cpp,h},
│   ├── FoldArrowColumn.cpp, FoldGutter.{cpp,h},
│   ├── FoldingModel.{cpp,h}, FoldingTypes.cpp,
│   ├── GutterColumn.h, ImageBlockItem.{cpp,h},
│   ├── LinkRenderer.cpp, SceneCoordinator.cpp
│   └── (sweep call sites for Detail::AstBlockDiff / Detail::Coordinates)
├── qml/
│   ├── FindBar.qml                               # Task 5 (CREATE)
│   ├── LiveView.qml                              # Task 3 (13 selectionView→cursorState callsites)
│   │                                             # Task 5 (embed FindBar)
│   └── delegates/
│       └── MathDelegate.qml                      # Task 4 (inline comment D8)
├── tests/
│   ├── tst_live_find_controller.cpp              # Task 5 (CREATE)
│   ├── tst_live_render_qml_integration.cpp      # Task 5 (new slot)
│   ├── tst_live_render_selection_*.cpp           # Task 3 (migrate from LiveSelectionView)
│   └── (other tests that reference LiveSelectionView — migrate)
├── CMakeLists.txt                                # Tasks 3, 5 (sources, qmlmodule)
└── CLAUDE.md                                     # Task 3 (selection-state section)

docs/queue.md                                    # Task 6 (Discipline Log entries D7+D8)
```

---

## Task 1: Delete legacy pre-rewrite orphans (D10)

**Files:**
- Delete: `libs/markoff-live/include/markoff/Editor.h`, `EditorContext.h`, `FoldingTypes.h`, `LinkRenderer.h`, `ResourceProvider.h`, `Theme.h`
- Delete: `libs/markoff-live/src/Editor.cpp`, `BlockItem.{cpp,h}`, `CheckboxTextObject.{cpp,h}`, `EditorContextClassifier.{cpp,h}`, `FoldArrowColumn.cpp`, `FoldGutter.{cpp,h}`, `FoldingModel.{cpp,h}`, `FoldingTypes.cpp`, `GutterColumn.h`, `ImageBlockItem.{cpp,h}`, `LinkRenderer.cpp`, `SceneCoordinator.cpp`

None of these are in CMakeLists. All are leftovers from the retired pre-rewrite markoff-live.

- [ ] **Step 1: Confirm files aren't in the build**

Run:

```bash
for f in include/markoff/Editor.h include/markoff/EditorContext.h include/markoff/FoldingTypes.h include/markoff/LinkRenderer.h include/markoff/ResourceProvider.h include/markoff/Theme.h src/Editor.cpp src/BlockItem.cpp src/BlockItem.h src/CheckboxTextObject.cpp src/CheckboxTextObject.h src/EditorContextClassifier.cpp src/EditorContextClassifier.h src/FoldArrowColumn.cpp src/FoldGutter.cpp src/FoldGutter.h src/FoldingModel.cpp src/FoldingModel.h src/FoldingTypes.cpp src/GutterColumn.h src/ImageBlockItem.cpp src/ImageBlockItem.h src/LinkRenderer.cpp src/SceneCoordinator.cpp; do
  if grep -q "$f" libs/markoff-live/CMakeLists.txt; then
    echo "STILL IN BUILD: $f"
  fi
done
echo "Done. Empty output above = all confirmed orphaned."
```

Expected: empty output (every file confirmed orphaned).

- [ ] **Step 2: Pre-deletion include-reference grep**

Verify nothing in the active sources includes any of the soon-to-delete headers (in case an active source incorrectly references one):

```bash
for h in Editor EditorContext FoldingTypes LinkRenderer ResourceProvider Theme BlockItem CheckboxTextObject EditorContextClassifier FoldGutter FoldingModel GutterColumn ImageBlockItem SceneCoordinator; do
  matches=$(grep -rn "\"$h\.h\"\|<markoff/$h\.h>" libs/markoff-live/include/markoff/live/ libs/markoff-live/src/ libs/markoff-live/tests/ libs/markoff-live/app/ libs/markoff-live/qml/ 2>/dev/null \
            | grep -v "libs/markoff-live/src/$h\.cpp" \
            | grep -v "libs/markoff-live/src/$h\.h" \
            | grep -v "libs/markoff-live/include/markoff/$h\.h")
  if [ -n "$matches" ]; then
    echo "REFERENCE FOUND: $h"
    echo "$matches"
  fi
done
echo "Done. Empty output = no active include references."
```

Expected: empty output. If anything appears, STOP — investigate before deletion.

- [ ] **Step 3: Delete the orphaned files**

Run:

```bash
git rm libs/markoff-live/include/markoff/Editor.h \
       libs/markoff-live/include/markoff/EditorContext.h \
       libs/markoff-live/include/markoff/FoldingTypes.h \
       libs/markoff-live/include/markoff/LinkRenderer.h \
       libs/markoff-live/include/markoff/ResourceProvider.h \
       libs/markoff-live/include/markoff/Theme.h \
       libs/markoff-live/src/Editor.cpp \
       libs/markoff-live/src/BlockItem.cpp \
       libs/markoff-live/src/BlockItem.h \
       libs/markoff-live/src/CheckboxTextObject.cpp \
       libs/markoff-live/src/CheckboxTextObject.h \
       libs/markoff-live/src/EditorContextClassifier.cpp \
       libs/markoff-live/src/EditorContextClassifier.h \
       libs/markoff-live/src/FoldArrowColumn.cpp \
       libs/markoff-live/src/FoldGutter.cpp \
       libs/markoff-live/src/FoldGutter.h \
       libs/markoff-live/src/FoldingModel.cpp \
       libs/markoff-live/src/FoldingModel.h \
       libs/markoff-live/src/FoldingTypes.cpp \
       libs/markoff-live/src/GutterColumn.h \
       libs/markoff-live/src/ImageBlockItem.cpp \
       libs/markoff-live/src/ImageBlockItem.h \
       libs/markoff-live/src/LinkRenderer.cpp \
       libs/markoff-live/src/SceneCoordinator.cpp
```

- [ ] **Step 4: Verify clean build + tests**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: build succeeds; 216/216 fast tests pass.

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
chore(live): delete pre-rewrite orphaned headers + sources

Deletes 6 legacy headers in libs/markoff-live/include/markoff/
(Editor.h, EditorContext.h, FoldingTypes.h, LinkRenderer.h,
ResourceProvider.h, Theme.h) and ~18 legacy src/ files (Editor.cpp,
BlockItem, CheckboxTextObject, EditorContextClassifier,
FoldArrowColumn, FoldGutter, FoldingModel, FoldingTypes,
GutterColumn, ImageBlockItem, LinkRenderer, SceneCoordinator).

None were in CMakeLists.txt — all leftover from the retired
original markoff-live leaf before the foundation-exploration
rewrite. Git history preserves the implementations.

Decision D10 of docs/specs/2026-05-19-markoff-live-freeze-shape-design.md.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Move `AstBlockDiff` + `Coordinates` to `Markoff::Live::Detail::` (D4)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/AstBlockDiff.h`
- Modify: `libs/markoff-live/src/AstBlockDiff.cpp`
- Modify: `libs/markoff-live/include/markoff/live/Coordinates.h`
- Modify: `libs/markoff-live/src/Coordinates.cpp`
- Modify: call sites in `libs/markoff-live/src/*.cpp` (using-decl additions; mechanical)

- [ ] **Step 1: Wrap `AstBlockDiff` in `Detail::` namespace**

Edit `libs/markoff-live/include/markoff/live/AstBlockDiff.h`. The current outer namespace is `namespace Markoff::Live { ... }`. Wrap the class in a nested `Detail`:

```cpp
namespace Markoff::Live::Detail {

class AstBlockDiff {
    // ... existing class body unchanged ...
};

} // namespace Markoff::Live::Detail
```

- [ ] **Step 2: Wrap `AstBlockDiff.cpp` matching the header**

Edit `libs/markoff-live/src/AstBlockDiff.cpp`: open namespace becomes `namespace Markoff::Live::Detail {`, close becomes `} // namespace Markoff::Live::Detail`.

- [ ] **Step 3: Wrap `Coordinates` namespace functions in `Detail::`**

Edit `libs/markoff-live/include/markoff/live/Coordinates.h`. The current shape is `namespace Markoff::Live::Coordinates { ... }`. Nest it under `Detail`:

```cpp
namespace Markoff::Live::Detail::Coordinates {
    // ... existing function declarations unchanged ...
}
```

- [ ] **Step 4: Wrap `Coordinates.cpp` matching the header**

Edit `libs/markoff-live/src/Coordinates.cpp`: open becomes `namespace Markoff::Live::Detail::Coordinates {`, close matches.

- [ ] **Step 5: Find call sites and add using-decls**

```bash
grep -rn "AstBlockDiff\|Coordinates::" libs/markoff-live/src/ libs/markoff-live/tests/ 2>/dev/null \
  | grep -v "libs/markoff-live/src/AstBlockDiff" \
  | grep -v "libs/markoff-live/src/Coordinates"
```

For each `.cpp` file in the output, add the following near the top of its `namespace Markoff::Live { ... }` block (right after any anonymous namespace and existing using-decls):

```cpp
using Detail::AstBlockDiff;       // only if file uses AstBlockDiff
namespace coords = Detail::Coordinates;  // only if file uses Coordinates
```

Then change any `Coordinates::funcName(...)` callsite to `coords::funcName(...)`. `AstBlockDiff` callsites remain unqualified due to the using-decl.

Likely callers per the grep above: `LiveListModelBinding.cpp`, `LiveEditBinding.cpp`, possibly `LiveBlockModel.cpp`, possibly `KindTransition.cpp`. Test files may also reference these — add the same using-decls inside the test's namespace block.

- [ ] **Step 6: Build + run tests**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 216/216 pass.

- [ ] **Step 7: Commit**

```bash
git commit -am "$(cat <<'EOF'
refactor(live): move AstBlockDiff + Coordinates to Markoff::Live::Detail

Matches the source-leaf precedent (Markoff::Source::Detail::Gutter)
and the project-wide convention from markoff-core (CLAUDE.md:
"Foundation-internal helpers go in Markoff::Detail namespace").

Both are algorithm-like internal helpers — AstBlockDiff is the
Myers/LCS diff used by LiveListModelBinding::applyOps; Coordinates
is the byte↔QtPos UTF-8 mapping used by LiveEditBinding. Neither
is part of the consumer-facing story; Detail:: signals that.
Headers stay in include/markoff/live/ so tests compile against
them; call sites pick up the types via using-decls inside their
namespace blocks.

Decision D4 of docs/specs/2026-05-19-markoff-live-freeze-shape-design.md.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Retire `LiveSelectionView`; selection methods to `LiveCursorState` (D2)

**Files:**
- Delete: `libs/markoff-live/include/markoff/live/LiveSelectionView.h`
- Delete: `libs/markoff-live/src/LiveSelectionView.cpp`
- Modify: `libs/markoff-live/include/markoff/live/LiveCursorState.h` (add 11 Q_INVOKABLE methods)
- Modify: `libs/markoff-live/src/LiveCursorState.cpp` (impl)
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` (remove selectionView Q_PROPERTY + getter)
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp` (remove selectionView PIMPL field, instantiation, wiring, getter impl)
- Modify: `libs/markoff-live/qml/LiveView.qml` (13 callsites `binding.selectionView.X` → `binding.cursorState.X`)
- Modify: `libs/markoff-live/CMakeLists.txt` (remove `LiveSelectionView.h` + `.cpp` from SOURCES)
- Modify: ~13 test files that reference `LiveSelectionView` (migrate constructor calls and `binding.selectionView()` accesses)
- Modify: `libs/markoff-live/CLAUDE.md` (selection-state section)

`LiveSelectionView` is already a stateless facade post-tier-4c; all canonical state lives in `LiveCursorState`. The migration moves the public-facing methods to where the state already lives.

- [ ] **Step 1: Add 11 Q_INVOKABLE method signatures to `LiveCursorState.h`**

Edit `libs/markoff-live/include/markoff/live/LiveCursorState.h`. Add inside the `public:` section, grouped under a `// Selection operations (migrated from LiveSelectionView, 2026-05-19)` comment:

```cpp
    // Selection operations (migrated from LiveSelectionView, 2026-05-19)
    Q_INVOKABLE void begin(int blockIndex, int qtPos);
    Q_INVOKABLE void extend(int blockIndex, int qtPos);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deleteSelection();

    /// Returns QPoint(start, end) for the block, or QPoint(-1,-1) if
    /// untouched. `end` may be INT32_MAX ("to end of block") — consumers
    /// must clamp via Math.min(r.y, textEdit.length) before calling
    /// TextEdit.select.
    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;

    /// Copy the current selection to the system clipboard. Reads block
    /// texts directly from the bound LiveBlockModel.
    Q_INVOKABLE void copyToClipboard() const;

    // Accessors used by clipboard / navigation / delegate sync paths.
    Q_INVOKABLE int anchorBlock() const;
    Q_INVOKABLE int anchorQtPos() const;
    Q_INVOKABLE int activeBlock() const;
    Q_INVOKABLE int activeQtPos() const;
```

`hasSelection()`, `selectionChanged` signal, and `selectionChangedFromSession` already exist on `LiveCursorState` — no changes needed there.

Add `#include <QPoint>` to the header if not already present.

- [ ] **Step 2: Implement the 11 methods in `LiveCursorState.cpp`**

Edit `libs/markoff-live/src/LiveCursorState.cpp`. Copy the existing bodies from `LiveSelectionView.cpp`, replacing `m_cursorState->X()` with `this->X()` (or direct `m_*` member access where appropriate). The implementations are identical in semantics — they already operate on `LiveCursorState`'s canonical state via the facade pattern.

For each method, the body shape:

```cpp
void LiveCursorState::begin(int blockIndex, int qtPos)
{
    // Copy from LiveSelectionView::begin's existing implementation, but call
    // local methods directly instead of forwarding through m_cursorState.
    // The semantics are unchanged: begin() resolves to a model-row-based
    // SelectionAnchor write and emits selectionChanged.
    // ...
}
```

Repeat for `extend`, `clear`, `selectAll`, `deleteSelection`, `rangeForBlock`, `copyToClipboard`, `anchorBlock`, `anchorQtPos`, `activeBlock`, `activeQtPos`.

- [ ] **Step 3: Delete `LiveSelectionView.h` and `.cpp`**

```bash
git rm libs/markoff-live/include/markoff/live/LiveSelectionView.h \
       libs/markoff-live/src/LiveSelectionView.cpp
```

- [ ] **Step 4: Remove `selectionView` from `LiveListModelBinding`**

Edit `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`:
- Delete line ~9: `#include <markoff/live/LiveSelectionView.h>`.
- Delete the `selectionView` Q_PROPERTY (around line 48): `Q_PROPERTY(Markoff::Live::LiveSelectionView *selectionView ...)`.
- Delete the `selectionView()` getter declaration (around line 105).

Edit `libs/markoff-live/src/LiveListModelBinding.cpp`:
- Delete the `LiveSelectionView *selectionView = nullptr;` PIMPL member (line ~140).
- Delete the `d->selectionView = new LiveSelectionView(this);` instantiation (line ~178).
- Delete the signal-forwarding `QObject::connect(this, &LiveListModelBinding::selectionChangedFromSession, d->selectionView, &LiveSelectionView::selectionChanged);` block (lines ~181-186). `LiveCursorState::selectionChangedFromSession` continues to emit; the facade's re-emission is no longer needed because consumers connect to `cursorState.selectionChanged` directly.
- Delete the `selectionView()` getter impl (line ~292).

- [ ] **Step 5: Sweep QML callsites in `LiveView.qml`**

Edit `libs/markoff-live/qml/LiveView.qml`. Replace every occurrence of `binding.selectionView` with `binding.cursorState`. Per the spec there are 13 such lines (verify post-sweep with `grep -n 'binding\.selectionView' libs/markoff-live/qml/LiveView.qml` returns zero).

- [ ] **Step 6: Migrate tests that reference `LiveSelectionView`**

```bash
grep -rln "LiveSelectionView" libs/markoff-live/tests/
```

Expected affected files (per pre-task survey):
- `tst_live_render_paragraph_edit.cpp`
- `tst_live_render_session_apply.cpp`
- `tst_live_render_session_two_bindings.cpp`
- `tst_live_render_session_clamp.cpp`
- `tst_live_render_e2_nav_shift_extend_qml.cpp`
- `tst_live_render_clipboard_copy.cpp`
- `tst_live_render_selection_cursor_unification.cpp`
- `tst_live_render_selection_delete.cpp`
- `tst_live_render_session_orphaned_block.cpp`
- `tst_live_render_actions_enabled_state.cpp`
- `QmlIntegrationFixture.cpp`
- `tst_live_render_selection_select_all.cpp`
- `tst_live_render_e2_nav_shift_extend.cpp`
- `tst_live_render_paste_kind_roundtrip.cpp`

For each file:
1. Remove `#include <markoff/live/LiveSelectionView.h>`.
2. Replace `binding.selectionView()->X(...)` with `binding.cursorState()->X(...)`.
3. Replace any direct `LiveSelectionView sv(...)` constructor calls with use of `binding.cursorState()` (tests should never instantiate the facade directly post-migration).
4. For QML-integration-style tests that read `binding.selectionView`, replace with `binding.cursorState` (same pattern as Step 5).

- [ ] **Step 7: Remove `LiveSelectionView.{h,cpp}` from `CMakeLists.txt`**

Edit `libs/markoff-live/CMakeLists.txt`. Find and delete:
- `include/markoff/live/LiveSelectionView.h` line (in the SOURCES list).
- `src/LiveSelectionView.cpp` line (in the SOURCES list).

- [ ] **Step 8: Update `libs/markoff-live/CLAUDE.md`**

Edit `libs/markoff-live/CLAUDE.md`. Update the "Architecture" / "Cursor model" / "Cursor delivery" sections to remove references to `LiveSelectionView`. Specifically:
- Any sentence like "selection state is held by `LiveSelectionView`" → "selection state is held by `LiveCursorState`."
- Any "tier-4c phase A" mention of split selection state → noted as resolved by D2 of `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`.

- [ ] **Step 9: Build + run tests**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 216/216 pass. Particular attention to `tst_live_render_selection_cursor_unification` (7 slots from tier-4c — these verify the migrated call paths).

If any test fails: investigate. The most likely failure mode is a missed `binding.selectionView` callsite (compile-time error) or a logic difference between `LiveSelectionView`'s body and the migrated `LiveCursorState` body (test failure with specific selection mismatch).

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-live/ docs/queue.md
git commit -m "$(cat <<'EOF'
refactor(live): retire LiveSelectionView; selection methods to LiveCursorState

LiveSelectionView was a stateless facade post-tier-4c — canonical
state already lived in LiveCursorState. The freeze is the natural
place to close the loop: one canonical store for cursor + selection,
one name for that store, no transitional shim in the frozen API.

Closes tier-4c Phase C (per docs/specs/2026-05-16-tier-4c-selection-cursor-unification-design.md §"Future work").

Changes:
- 11 selection Q_INVOKABLEs added to LiveCursorState (begin, extend,
  clear, selectAll, deleteSelection, rangeForBlock, copyToClipboard,
  anchorBlock, anchorQtPos, activeBlock, activeQtPos). hasSelection()
  and selectionChanged signal already existed.
- LiveSelectionView.{h,cpp} deleted; LiveListModelBinding's selectionView
  Q_PROPERTY + getter removed; PIMPL member + instantiation + signal
  forwarding gone.
- 13 callsites in LiveView.qml migrated binding.selectionView.X →
  binding.cursorState.X.
- 14 test files migrated (LiveSelectionView includes removed; accessor
  callsites switched).
- CMakeLists.txt updated to drop LiveSelectionView sources.
- CLAUDE.md updated to reflect the unified store.

Decision D2 of docs/specs/2026-05-19-markoff-live-freeze-shape-design.md.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Documentation + annotation updates (D3, D5, D6, D7-inline, D8-inline)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h` (D3 + D5 docstrings)
- Modify: `libs/markoff-live/include/markoff/live/BlockRecord.h` (D6 comment)
- Modify: `libs/markoff-live/src/LiveEditBinding.cpp` (D7 inline comment)
- Modify: `libs/markoff-live/qml/delegates/MathDelegate.qml` (D8 inline comment)

Documentation-only commit. Zero behavioural change.

- [ ] **Step 1: D3 — `isBlockOnly` docstring**

Edit `libs/markoff-live/include/markoff/live/BlockKindDescriptor.h`. Find the field declaration for `isBlockOnly` and replace its `///`-style doc comment with:

```cpp
    /// Whether this kind cannot host a text caret. Derived from
    /// `supportedCursorVariants` in practice — except for transitional
    /// kinds (currently Math) where the explicit flag retains current
    /// behaviour pending the kind's text-bearing redesign (E5 for Math).
    bool isBlockOnly = false;
```

- [ ] **Step 2: D5 — `delegateUrl` docstring**

In the same `BlockKindDescriptor.h`, find the `delegateUrl` field and replace its doc comment with:

```cpp
    /// qrc: URL of the QML delegate. Populated for built-in kinds as
    /// a queryable annotation (debuggers, DevTools, future plugin-kind
    /// machinery); NOT consumed by LiveView.qml's DelegateChooser, which
    /// dispatches on `delegateClass`. Plugin-kind dynamic dispatch is a
    /// future spec that would wire this through a Loader-based selector.
    QString delegateUrl;
```

- [ ] **Step 3: D6 — `BlockRecord::inlineSpans` comment refresh**

Edit `libs/markoff-live/include/markoff/live/BlockRecord.h`. Find the `inlineSpans` field and replace its stale comment with:

```cpp
    /// Per-block inline-format spans (bold/italic/code/link/wikilink/tag/
    /// strikethrough/highlight) populated from the parsed AST and consumed
    /// by `InlineHighlighter` (E1). Load-bearing — do not strip.
    QList<Markoff::SourceSpan> inlineSpans;
```

- [ ] **Step 4: D7 — `LiveEditBinding::m_applyingTextUpdate` inline comment**

Edit `libs/markoff-live/src/LiveEditBinding.cpp`. At line ~73 (where `m_applyingTextUpdate = true;` is set), expand the existing comment block above the scope guard to include the freeze-spec context:

```cpp
    // Re-entrance guard for the pushTextToDocument ↔ onContentsChange
    // echo loop. Accepted invariant-7 smell at freeze (2026-05-19); see
    // docs/queue.md Discipline Log. Future redesign tracked as the
    // edit-pipeline echo-suppression spec (TBW). Two production QML
    // delegates (CodeBlockDelegate.qml:65, UnifiedInlineTextDelegate.qml:202)
    // read the public isApplyingTextUpdate() accessor to suppress reactions
    // during the apply window — that is the load-bearing reason this
    // accessor stays in the frozen public API.
    m_applyingTextUpdate = true;
    auto _ = qScopeGuard([this]{ m_applyingTextUpdate = false; });
```

- [ ] **Step 5: D8 — `MathDelegate.qml` Qt.callLater inline comment**

Edit `libs/markoff-live/qml/delegates/MathDelegate.qml`. At line ~125 (the `Qt.callLater(function() { latexEdit.forceActiveFocus() })` line), prepend a comment block:

```qml
    // Deferral: forceActiveFocus needs to run after the
    // BlockInternalEdit cursor variant change propagates through QML
    // bindings (latexEdit's visibility / focus chain). Accepted
    // invariant-6 smell at freeze (2026-05-19); see docs/queue.md
    // Discipline Log. E5 (math live-mode parity) will redesign this
    // with a chokepoint-routed focus signal.
    Qt.callLater(function() { latexEdit.forceActiveFocus() })
```

- [ ] **Step 6: Build + sanity tests**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 216/216 pass (no behavioural change; just docs).

- [ ] **Step 7: Commit**

```bash
git commit -am "$(cat <<'EOF'
docs(live): freeze annotations — isBlockOnly, delegateUrl, inlineSpans, smell comments

Pure documentation update. No behaviour change.

D3: BlockKindDescriptor::isBlockOnly docstring documents the Math
asymmetry as transitional pending E5 (math text-bearing).

D5: BlockKindDescriptor::delegateUrl docstring documents the field
as a populated annotation, NOT consumed by current dispatch.
Plugin-kind dynamic dispatch is a future spec.

D6: BlockRecord::inlineSpans comment refreshed. The field is
load-bearing for E1 (inline highlighter); the stale "dead code"
language from the §A.7-era developmental history is retired.

D7-inline: LiveEditBinding::m_applyingTextUpdate gains a comment
referencing the freeze spec and the public-QML-readers that make
the accessor load-bearing. (Full Discipline Log entry is Task 6.)

D8-inline: MathDelegate.qml's Qt.callLater gains a comment
referencing the freeze spec and naming E5 as the natural redesign
point. (Full Discipline Log entry is Task 6.)

Decisions D3, D5, D6, D7, D8 of docs/specs/2026-05-19-markoff-live-freeze-shape-design.md.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: QML FindBar + `LiveFindController` (D9 — the substantive new code)

**Files:**
- Create: `libs/markoff-live/include/markoff/live/LiveFindController.h`
- Create: `libs/markoff-live/src/LiveFindController.cpp`
- Create: `libs/markoff-live/qml/FindBar.qml`
- Create: `libs/markoff-live/tests/tst_live_find_controller.cpp`
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` (add findController Q_PROPERTY + show/hideFindBar Q_INVOKABLEs)
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp` (PIMPL member, instantiation, wiring)
- Modify: `libs/markoff-live/qml/LiveView.qml` (embed FindBar component)
- Modify: `libs/markoff-live/CMakeLists.txt` (add LiveFindController to SOURCES; add FindBar.qml to qt_add_qml_module)
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (new tst_live_find_controller target)
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` (add `find_bar_typing_highlights_and_navigation` slot)

- [ ] **Step 1: Create `LiveFindController.h` with class skeleton**

Create `libs/markoff-live/include/markoff/live/LiveFindController.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QList>
#include <QObject>
#include <QString>
#include <qqmlintegration.h>

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;

/// Drives the search loop for the live find UI (`Markoff/Live/FindBar.qml`).
///
/// Owns the current needle string, the list of matches across all blocks,
/// and the index of the currently-highlighted match. Search runs against
/// `LiveBlockModel`'s row texts; navigation calls into `LiveCursorState`
/// to scroll + place caret at each match.
///
/// Instantiated and wired by `LiveListModelBinding`; QML accesses via
/// `binding.findController`.
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
    ~LiveFindController() override;

    QString needle() const { return m_needle; }
    void    setNeedle(const QString &);

    int  matchCount()        const { return static_cast<int>(m_matches.size()); }
    int  currentMatchIndex() const { return m_currentIndex; }
    bool isActive()          const { return m_isActive; }

    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();

    /// Internal wiring (called by LiveListModelBinding during setup).
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

    LiveBlockModel  *m_blockModel  = nullptr;
    LiveCursorState *m_cursorState = nullptr;

    QString      m_needle;
    QList<Match> m_matches;
    int          m_currentIndex = -1;
    bool         m_isActive     = false;
};

} // namespace Markoff::Live
```

- [ ] **Step 2: Create `LiveFindController.cpp` skeleton (empty bodies)**

Create `libs/markoff-live/src/LiveFindController.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveFindController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockRecord.h>

namespace Markoff::Live {

LiveFindController::LiveFindController(QObject *parent) : QObject(parent) {}
LiveFindController::~LiveFindController() = default;

void LiveFindController::setBlockModel(LiveBlockModel *m)   { m_blockModel = m; }
void LiveFindController::setCursorState(LiveCursorState *s) { m_cursorState = s; }

void LiveFindController::setNeedle(const QString &n)
{
    if (n == m_needle) return;
    m_needle = n;
    Q_EMIT needleChanged();
    if (m_isActive) recomputeMatches();
}

void LiveFindController::activate()
{
    if (m_isActive) return;
    m_isActive = true;
    Q_EMIT activeChanged();
    recomputeMatches();
}

void LiveFindController::deactivate()
{
    if (!m_isActive) return;
    m_isActive = false;
    m_matches.clear();
    m_currentIndex = -1;
    Q_EMIT activeChanged();
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged();
}

void LiveFindController::findNext()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_matches.size();
    Q_EMIT currentMatchChanged();
    seekToCurrent();
}

void LiveFindController::findPrevious()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex - 1 + m_matches.size()) % m_matches.size();
    Q_EMIT currentMatchChanged();
    seekToCurrent();
}

void LiveFindController::recomputeMatches()
{
    m_matches.clear();
    m_currentIndex = -1;
    if (!m_blockModel || m_needle.isEmpty()) {
        Q_EMIT matchesChanged();
        Q_EMIT currentMatchChanged();
        return;
    }
    const int rows = m_blockModel->rowCount();
    for (int r = 0; r < rows; ++r) {
        const auto &rec = m_blockModel->recordAt(r);
        const QString &text = rec.text;
        int from = 0;
        while (true) {
            const int idx = text.indexOf(m_needle, from, Qt::CaseInsensitive);
            if (idx < 0) break;
            m_matches.append(Match{r, idx, static_cast<int>(m_needle.size())});
            from = idx + 1;  // overlapping matches allowed via +1 step
        }
    }
    if (!m_matches.isEmpty()) m_currentIndex = 0;
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged();
    if (m_currentIndex >= 0) seekToCurrent();
}

void LiveFindController::seekToCurrent()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_matches.size()) return;
    if (!m_cursorState) return;
    const Match &m = m_matches[m_currentIndex];
    m_cursorState->requestTextCaretAtRow(m.row, m.startQtPos);
}

} // namespace Markoff::Live
```

- [ ] **Step 3: Add `LiveFindController` to `CMakeLists.txt`**

Edit `libs/markoff-live/CMakeLists.txt`. Find the SOURCES list inside `qt_add_qml_module(markoff_live ...)`. Add:

```cmake
        include/markoff/live/LiveFindController.h
        src/LiveFindController.cpp
```

next to the other `Live*Controller.h` / `Live*Controller.cpp` entries (alphabetical order).

- [ ] **Step 4: Create the failing tests in `tst_live_find_controller.cpp`**

Create `libs/markoff-live/tests/tst_live_find_controller.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFindController.h>
#include <markoff/live/LiveBlockModel.h>

class TstLiveFindController : public QObject {
    Q_OBJECT
private slots:
    void needle_search_finds_matches_across_blocks() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "First paragraph has the word find.\n\n"
            "Second paragraph also has find inside.\n\n"
            "Third paragraph: nothing here.\n"
        ));
        // Wait for model to populate (debounced d2DocumentChanged).
        QTRY_COMPARE(binding.model()->rowCount(), 3);

        auto *fc = binding.findController();
        QVERIFY(fc != nullptr);
        fc->activate();
        fc->setNeedle(QStringLiteral("find"));

        QCOMPARE(fc->matchCount(), 2);
        QCOMPARE(fc->currentMatchIndex(), 0);
    }

    void find_next_wraps_at_end() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "alpha alpha alpha\n"
        ));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *fc = binding.findController();
        fc->activate();
        fc->setNeedle(QStringLiteral("alpha"));
        QCOMPARE(fc->matchCount(), 3);
        QCOMPARE(fc->currentMatchIndex(), 0);

        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 1);
        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 2);
        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 0);  // wraps
    }

    void find_previous_wraps_at_start() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "beta beta beta\n"
        ));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *fc = binding.findController();
        fc->activate();
        fc->setNeedle(QStringLiteral("beta"));
        QCOMPARE(fc->matchCount(), 3);
        QCOMPARE(fc->currentMatchIndex(), 0);

        fc->findPrevious();
        QCOMPARE(fc->currentMatchIndex(), 2);  // wraps to last
        fc->findPrevious();
        QCOMPARE(fc->currentMatchIndex(), 1);
    }

    void deactivate_clears_matches() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral("gamma gamma\n"));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *fc = binding.findController();
        fc->activate();
        fc->setNeedle(QStringLiteral("gamma"));
        QVERIFY(fc->matchCount() > 0);
        QVERIFY(fc->isActive());

        fc->deactivate();

        QCOMPARE(fc->matchCount(), 0);
        QCOMPARE(fc->currentMatchIndex(), -1);
        QVERIFY(!fc->isActive());
    }
};

QTEST_MAIN(TstLiveFindController)
#include "tst_live_find_controller.moc"
```

- [ ] **Step 5: Add the test target to `tests/CMakeLists.txt`**

Edit `libs/markoff-live/tests/CMakeLists.txt`. Add a new `qt_add_executable` block following the existing pattern (e.g., model after `tst_live_render_cursor`):

```cmake
qt_add_executable(tst_live_find_controller tst_live_find_controller.cpp)
target_link_libraries(tst_live_find_controller PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
    markoff_core markoff_live
)
add_test(NAME tst_live_find_controller COMMAND tst_live_find_controller)
set_tests_properties(tst_live_find_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

(Copy the exact pattern from an adjacent existing executable in this file to ensure consistent flags / dependencies.)

- [ ] **Step 6: Wire `LiveFindController` into `LiveListModelBinding`**

Edit `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`. Add forward declaration near the top:

```cpp
class LiveFindController;
```

Add the Q_PROPERTY in the existing block alongside the other sub-controller properties:

```cpp
    Q_PROPERTY(LiveFindController *findController READ findController CONSTANT)
```

Add the getter declaration and the two new Q_INVOKABLEs in the public interface (with the other getters/Q_INVOKABLEs):

```cpp
    LiveFindController *findController() const;

    Q_INVOKABLE void showFindBar();
    Q_INVOKABLE void hideFindBar();
```

Edit `libs/markoff-live/src/LiveListModelBinding.cpp`. Inside the `struct Private` (PIMPL), add:

```cpp
    LiveFindController *findController = nullptr;
```

In the constructor (after the other sub-controller instantiations), add:

```cpp
    d->findController = new LiveFindController(this);
    d->findController->setBlockModel(d->blockModel);
    d->findController->setCursorState(d->cursorState);
```

Add the getter impl at the bottom alongside the others:

```cpp
LiveFindController *LiveListModelBinding::findController() const { return d->findController; }
```

Add the Q_INVOKABLE impls (placement: alongside `setTheme`, `applyDefaultTheme`, etc.):

```cpp
void LiveListModelBinding::showFindBar() { if (d->findController) d->findController->activate(); }
void LiveListModelBinding::hideFindBar() { if (d->findController) d->findController->deactivate(); }
```

Add `#include <markoff/live/LiveFindController.h>` at the top of `LiveListModelBinding.cpp`.

- [ ] **Step 7: Build + run the C++ unit tests; verify PASS**

```bash
cmake --build build-dev --target tst_live_find_controller -j 8
QT_QPA_PLATFORM=offscreen build-dev/bin/tst_live_find_controller
```

Expected: all 4 slots PASS.

If they fail because `LiveFindController` isn't compiling: verify `LiveFindController.h` and `.cpp` are included in `libs/markoff-live/CMakeLists.txt` SOURCES (Step 3). Re-run `cmake -S . -B build-dev` if needed.

- [ ] **Step 8: Create `FindBar.qml`**

Create `libs/markoff-live/qml/FindBar.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    required property var liveBinding
    readonly property var fc: liveBinding ? liveBinding.findController : null

    visible: fc && fc.isActive
    height: visible ? 32 : 0
    color: "#f0f0f0"  // theme integration TBW; matches host-default light theme today

    signal closed()

    function requestClose() {
        if (fc) fc.deactivate()
        root.closed()
    }

    Row {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        TextField {
            id: needleInput
            width: 200
            text: fc ? fc.needle : ""
            placeholderText: "Find..."
            onTextChanged: if (fc) fc.setNeedle(text)
            Keys.onReturnPressed: if (fc) fc.findNext()
            Keys.onEscapePressed: root.requestClose()
        }

        Button {
            text: "<"
            enabled: fc && fc.matchCount > 0
            onClicked: if (fc) fc.findPrevious()
        }
        Button {
            text: ">"
            enabled: fc && fc.matchCount > 0
            onClicked: if (fc) fc.findNext()
        }

        Label {
            text: fc ? (fc.matchCount > 0
                ? ((fc.currentMatchIndex + 1) + " / " + fc.matchCount)
                : "0 / 0") : ""
            anchors.verticalCenter: parent.verticalCenter
        }

        Item { Layout.fillWidth: true; width: 8 }  // small filler

        Button {
            text: "×"
            onClicked: root.requestClose()
        }
    }
}
```

- [ ] **Step 9: Add `FindBar.qml` to `qt_add_qml_module` in `CMakeLists.txt`**

Edit `libs/markoff-live/CMakeLists.txt`. Add to the `qt_add_qml_module` SOURCES list (alphabetical with the other `qml/*.qml` entries):

```cmake
        qml/FindBar.qml
```

- [ ] **Step 10: Embed `FindBar` in `LiveView.qml`** — non-trivial structural refactor

**Current state to be aware of:** `LiveView.qml`'s root IS the `ListView` (not a wrapping container). The `binding` property, `_initialFocusSeeded` flag, `model` binding, `delegate` definition, `hit()` function, key handlers, scrollbar, and signals all hang off this root ListView. The host calls `LiveView { binding: ...; anchors.fill: parent }` and expects all those identities to be present on what they instantiate.

The refactor wraps the ListView in an outer `Item` so the FindBar can be a sibling pinned to the top. To preserve external API, the outer `Item` keeps the public-facing property (`binding`), and the inner ListView gets a new id (e.g., `listView`).

```qml
Item {
    id: root

    required property var binding   // hoisted from old root

    // Convenience aliases preserving any pre-refactor accessor patterns hosts
    // or tests may have relied on. If post-refactor tests show no consumer
    // touches these directly, the aliases can be removed in a follow-up.
    readonly property alias listView: listView
    readonly property alias findBar: findBar

    FindBar {
        id: findBar
        liveBinding: root.binding
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
    }

    ListView {
        id: listView
        anchors.top: findBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // ===== EVERYTHING BELOW: moved verbatim from the old root ListView =====
        // (Property bindings, model, delegate, ScrollBar, hit() function, key
        // handlers, signal connections, the _initialFocusSeeded flag and the
        // onCountChanged handler that uses it, MouseArea overlays for selection
        // / click handling, etc.)
        //
        // IMPORTANT path mapping during the move:
        //   - `root.X` inside the old ListView body → `listView.X` if X was a
        //     ListView-specific property (count, contentY, contentHeight,
        //     currentIndex, model, itemAt, etc.) OR a function local to the
        //     ListView (e.g. `hit()`).
        //   - `root.binding` references stay `root.binding` (binding lives on
        //     the outer Item now).
        //
        // The cleanest approach: copy the entire old root ListView body into
        // `listView`'s body verbatim, then sweep for any unqualified property
        // access that depended on the root-IS-ListView identity and rewrite to
        // the explicit `listView.X` form.

        property bool _initialFocusSeeded: false
        // ... rest of the migrated body ...
    }
}
```

The `FindBar.visible` binding is `liveBinding.findController.isActive`. When inactive, `height: 0` and `listView` occupies the full area; when active, the bar takes 32px at the top and the ListView shrinks.

**Verification after the move:**

```bash
# 1. QML parses cleanly. Build with QML compilation diagnostics enabled (Qt 6.8+
#    surfaces these via qmlsc in the build log).
cmake --build build-dev -j 8

# 2. Existing tests that touch LiveView's binding/model/cursor paths still pass.
scripts/run-tests.sh -R 'live_render_qml_integration'
```

If any test fails because it accessed an old root-as-ListView property: in the test, change `view->property("count")` (which read the ListView count off the LiveView root) to `view->findChild<QQuickItem*>("listView")->property("count")` or expose an alias.

If host applications (e.g., `apps/`, `markoff-live-app/Main.qml`) accessed the LiveView root as if it were a ListView (e.g., setting `model:` on the LiveView), they need updating too. Check:

```bash
grep -rn "LiveView" libs/markoff-live/app/ apps/ 2>/dev/null \
  | grep -v "import"
```

For any callsite that uses LiveView like a ListView, route the access through `liveView.listView.X` (via the alias) or refactor to use the binding directly.

- [ ] **Step 11: Add the QML integration test slot**

Edit `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`. Add a new slot inside the existing test class's `private slots:` section, before the `QTEST_MAIN`:

```cpp
    void find_bar_typing_highlights_and_navigation() {
        // Load a doc with multiple blocks; activate find via the binding;
        // verify the QML FindBar becomes visible; type a needle; verify
        // matches found; navigate; verify hideFindBar dismisses.
        QmlIntegrationFixture fix;
        fix.loadDocument(QByteArrayLiteral(
            "Alpha block.\n\n"
            "Beta has alpha inside.\n\n"
            "Gamma alpha alpha.\n"
        ));
        QTRY_COMPARE(fix.binding()->model()->rowCount(), 3);

        auto *fc = fix.binding()->findController();
        QVERIFY(fc != nullptr);

        fix.binding()->showFindBar();
        QVERIFY(fc->isActive());

        // Simulate user typing the needle into the FindBar's TextField.
        // (Direct setNeedle is what the TextField's onTextChanged would call.)
        fc->setNeedle(QStringLiteral("alpha"));
        QCOMPARE(fc->matchCount(), 4);  // "Alpha"+ "alpha" + "alpha"+"alpha", case-insensitive
        QCOMPARE(fc->currentMatchIndex(), 0);

        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 1);

        fix.binding()->hideFindBar();
        QVERIFY(!fc->isActive());
        QCOMPARE(fc->matchCount(), 0);
    }
```

(The exact integration-fixture API will be `fix.binding()->X()` patterns matching existing test slots in the same file. Read 2–3 existing slots first to match the convention.)

- [ ] **Step 12: Build all + run full source/find test set**

```bash
cmake --build build-dev -j 8
QT_QPA_PLATFORM=offscreen build-dev/bin/tst_live_find_controller
scripts/run-tests.sh -R 'live_render_qml_integration|live_find_controller'
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: all unit tests pass; new integration slot passes; full fast suite at 217/217 (216 baseline + 1 new test binary `tst_live_find_controller`).

- [ ] **Step 13: Commit**

```bash
git add libs/markoff-live/
git commit -m "$(cat <<'EOF'
feat(live): add QML find UI — LiveFindController + FindBar.qml + binding wiring

D9 of docs/specs/2026-05-19-markoff-live-freeze-shape-design.md.
Symmetric with the source-leaf find UI that landed at commit 0ec907d.

New artifacts:
  - Markoff::Live::LiveFindController (C++ Q_OBJECT) — drives the
    search loop. Q_PROPERTYs needle, matchCount, currentMatchIndex,
    isActive; Q_INVOKABLEs activate, deactivate, findNext, findPrevious.
    Walks LiveBlockModel rows, finds case-insensitive needle matches,
    seeks via LiveCursorState::requestTextCaretAtRow.
  - libs/markoff-live/qml/FindBar.qml — visual component: needle
    TextField, prev/next buttons, "N / M" match count, close button.
    Bound to liveBinding.findController; visible only when active.
  - LiveListModelBinding gains findController Q_PROPERTY + showFindBar /
    hideFindBar Q_INVOKABLEs. LiveView.qml embeds the FindBar anchored
    to top; collapses to 0 height when inactive.

Cross-leaf reality: source uses MarkdownView::showFindBar polymorphic
virtual; live uses Q_INVOKABLE on LiveListModelBinding (because the
binding is a QObject, not a QWidget). Both leaves now ship find UX
symmetric in feature, asymmetric in dispatch.

Tests:
  - tst_live_find_controller (C++ unit, 4 slots): needle search across
    blocks, find-next wrap, find-previous wrap, deactivate clears.
  - tst_live_render_qml_integration: new slot
    find_bar_typing_highlights_and_navigation for end-to-end.

Out of scope (deferred): ReplaceBar, regex, case-sensitivity toggle,
whole-word toggle, cross-block-spanning matches, theme integration
for FindBar background (currently hardcoded #f0f0f0).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Discipline Log entries (D7 + D8)

**Files:**
- Modify: `docs/queue.md`

Two append-only Discipline Log entries per invariant 8.

- [ ] **Step 1: Append D7 entry**

Edit `docs/queue.md`. In the "## Discipline Log" section, append:

```
- 2026-05-19 `libs/markoff-live/src/LiveEditBinding.cpp:73` — inv #7 — `m_applyingTextUpdate` re-entrance guard + public `Q_INVOKABLE isApplyingTextUpdate()` accessor. Read by 2 production QML delegates (`CodeBlockDelegate.qml:65`, `UnifiedInlineTextDelegate.qml:202`) to suppress echo reactions during the apply window. Removal requires re-architecting the QTextDocument↔MarkoffDocument echo loop (signal-blocking, scoped guards on the inner QTextDocument, or per-edit-path routing that doesn't emit `contentsChange`). Frozen with explicit acceptance at the markoff-live freeze (D7 of `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`); redesign spec TBW.
```

- [ ] **Step 2: Append D8 entry**

Append immediately after D7:

```
- 2026-05-19 `libs/markoff-live/qml/delegates/MathDelegate.qml:125` — inv #6 — `Qt.callLater(latexEdit.forceActiveFocus)` defers focus takeover after a `BlockInternalEdit` cursor variant change so QML bindings re-evaluate (latexEdit's visibility / focus chain) before the focus call. Single isolated site; removing requires extending the cursor chokepoint protocol with a synchronous focus-takeover signal (cross-delegate ripple work). Frozen with explicit acceptance at the markoff-live freeze (D8 of `docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`); E5 (math live-mode parity) retires.
```

- [ ] **Step 3: Commit**

```bash
git add docs/queue.md
git commit -m "$(cat <<'EOF'
docs(queue): log m_applyingTextUpdate + MathDelegate Qt.callLater smells

D7 + D8 of docs/specs/2026-05-19-markoff-live-freeze-shape-design.md.
Per invariant 8 ("Notice and note"): two production smells accepted
at the markoff-live freeze get explicit Discipline Log entries so
the next agent touching either seam sees the context.

D7: LiveEditBinding::m_applyingTextUpdate (invariant 7 — re-entrance
guard). Two production QML delegates read isApplyingTextUpdate() to
suppress echo. Redesign requires re-architecting the edit-echo
pipeline; deferred as a future spec.

D8: MathDelegate.qml:125 Qt.callLater (invariant 6). Single isolated
deferral so QML bindings re-evaluate after BlockInternalEdit cursor
variant change. E5 (math live-mode parity) is the natural redesign
point.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Post-implementation verification

After Task 6's commit, confirm the end state:

```bash
# 1. No LiveSelectionView remnants.
grep -rn "LiveSelectionView" libs/markoff-live/ docs/ 2>/dev/null \
  | grep -v "docs/specs/\|docs/plans/\|docs/handoff/\|docs/d-arc/"
# Expected: empty (references in archived/spec/plan/handoff docs are historical).

# 2. No pre-rewrite orphans.
grep -rn "Markoff::Source::Widget" libs/ apps/ 2>/dev/null
# Expected: empty.
ls libs/markoff-live/include/markoff/{Editor.h,EditorContext.h,FoldingTypes.h,LinkRenderer.h,ResourceProvider.h,Theme.h} 2>/dev/null
# Expected: "No such file or directory" for all.

# 3. AstBlockDiff + Coordinates in Detail.
grep -n "namespace Markoff::Live::Detail" libs/markoff-live/include/markoff/live/AstBlockDiff.h libs/markoff-live/include/markoff/live/Coordinates.h
# Expected: two matches.

# 4. LiveFindController + FindBar.qml present.
ls libs/markoff-live/include/markoff/live/LiveFindController.h \
   libs/markoff-live/src/LiveFindController.cpp \
   libs/markoff-live/qml/FindBar.qml
# Expected: all three files exist.

# 5. Full fast suite green at the new total.
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
# Expected: 217/217 (216 baseline + tst_live_find_controller).

# 6. Slow suite (optional).
scripts/run-tests.sh -R 'tst_realistic|tst_benchmark'
# Expected: 2/2 pass.
```
