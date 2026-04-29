# Live View — selection highlight on non-text blocks + delegate theming

**Status:** spec
**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`
**Tracks follow-ups:** §1 (selection highlight on HR + image) and §2 (theming) of `docs/handoff/2026-04-29-live-render-followups-SESSION-BRIEF.md`

## 1. Motivation

Two cosmetic / theme-correctness issues surfaced during dogfood:

1. When a user drags a multi-block selection through the HR or image blocks, those blocks aren't visually shown as selected, even though `LiveSelectionModel::collectSelectedText` correctly includes them. Visually the selection appears "to skip" them.
2. `CodeBlockDelegate.qml` and `ImageDelegate.qml` hardcode dark backgrounds (`#1e1e1e` / `#222`). They ignore `Markoff::Theme` entirely, which means they can't follow a light theme or a Plasma colour scheme.

These are user-facing polish gaps. They share a structural fix: thread the right properties through the `DelegateChooser` to non-text delegates (which currently receive only `blockIndex` plus their kind-specific role data).

## 2. Scope

**In scope:**

- HorizontalRuleDelegate + ImageDelegate gain a `selectionModel` property and a tinted overlay rectangle that becomes visible whenever the block is in the active selection.
- Add a `theme` property to LiveView and to the three delegates that currently hardcode colours (CodeBlockDelegate, ImageDelegate, HorizontalRuleDelegate). HR's `theme` is added now to keep the property surface uniform across non-text delegates, even though HR's divider colour stays out of scope (see below).
- CodeBlockDelegate replaces hardcoded `#1e1e1e` (background) and `#dcdcdc` (text) with `Theme::Slot::CodeBlockBackground` / `Theme::Slot::CodeBlock`.
- ImageDelegate's alt-fallback Rectangle replaces hardcoded `#222` with `Theme::Slot::CodeBlockBackground` (slot reuse — YAGNI on a dedicated `ImageBackdrop` slot until a real difference materialises).
- Two new C++ test methods in `tst_view_qml_live_view_qml.cpp`.

**Out of scope:**

- HR divider colour (the `#888` 1-px line). Brief §2 specifies image + code-block backgrounds; HR's divider is a separate decision.
- Heading delegate's hardcoded font sizes. Heading already takes a level prop but ignores Theme's font-size multipliers; that's a separate clean-up.
- Cross-mode theme propagation, theme-loader plugin, palette inheritance from QtQuick.Controls. Out of scope.
- A dedicated `Slot::ImageBackdrop`. Reuse `CodeBlockBackground` until divergence is needed.

## 3. Architecture

### 3.1 Property threading

Today only text-bearing delegates (Paragraph, Heading, CodeBlock) receive a `selectionModel`. Non-text ones (HR, Image) receive only `blockIndex`. `LiveView.qml`'s `DelegateChooser` configures these via explicit property bindings inside each `DelegateChoice`.

`MarkoffEditor.qml` already has a `theme` property, currently passed only to `SourceEditor`. We forward it to `LiveView`.

After this change, every non-text delegate's `DelegateChoice` block in `LiveView.qml` receives:

- `blockIndex: index`
- `selectionModel: binding.selectionModel`
- `theme: root.theme`
- (kind-specific role data, e.g. `imageSrc: model.imageSrc`)

Text-bearing delegates also gain `theme: root.theme` for consistency, even though this spec doesn't change their visuals (theme threading is a one-time wiring; future delegate work consumes it without re-plumbing).

Per the Bug-1 lesson (commit `b03b0d0`), every property is declared on the delegate as `property TYPE name: <default>`, never `required`. Default for `theme` is `null`; default for `selectionModel` is `null`.

### 3.2 Selection overlay

Both HR and Image gain a sibling Rectangle anchored to fill the delegate, drawn last so it overlays content:

```qml
Rectangle {
    anchors.fill: parent
    color: root.theme ? root.theme.selectionBackground : "#406080"
    opacity: 0.35
    visible: root.selectionModel
        && root.selectionModel.rangeForBlock(root.blockIndex).x !== -1
    objectName: "selectionOverlay"   // for test discovery
}
```

A `Connections { target: root.selectionModel; function onSelectionChanged() {...} }` is **not** needed — the `visible` binding re-evaluates when its dependencies change, and `selectionChanged` notifies through `rangeForBlock`. The fallback `#406080` covers the null-theme case (tests that instantiate delegates without a theme).

The image's underlying alt-text fallback content stays opaque under the overlay; we don't dim it. The overlay's 0.35 alpha reads as a tint, which is the standard "this block is in the selection" affordance.

### 3.3 Theme consumption

Per delegate:

- **CodeBlockDelegate:**
  - Outer `Rectangle.color`: `root.theme ? root.theme.codeBlockBackground : "#1e1e1e"`.
  - Inner `TextEdit.color`: `root.theme ? root.theme.codeBlock : "#dcdcdc"`.
- **ImageDelegate:**
  - Alt-fallback `Rectangle.color`: `root.theme ? root.theme.codeBlockBackground : "#222"`.
  - Alt-fallback `Text.color`: keep `#ccc` for now (out of scope; not flagged in brief).
- **HorizontalRuleDelegate:** receives `theme` for property-surface uniformity but the divider colour stays `#888` (out of scope).

The fallback hex literals in each binding are essential: tests and bare-instantiated delegates frequently leave `theme: null`. Without the fallback, accessing a slot property throws and the delegate fails to render.

#### 3.3.1 New Q_PROPERTYs on Theme

`Markoff::Theme` is a `Q_GADGET` value type. Today its slot accessor `color(Slot)` is **not** `Q_INVOKABLE`, and `Slot` is **not** registered as a QML enum namespace, so QML cannot call `theme.color(Theme.CodeBlockBackground)`. Rather than register a foreign QML namespace for the entire `Slot` enum (50+ values, most unused in QML), we add three read-only `Q_PROPERTY`s on `Theme` for the slots delegates need:

```cpp
// In Theme.h, alongside existing Q_GADGET members:
Q_PROPERTY(QColor codeBlockBackground READ codeBlockBackground)
Q_PROPERTY(QColor codeBlock           READ codeBlockColor)
Q_PROPERTY(QColor selectionBackground READ selectionBackground)

QColor codeBlockBackground() const { return color(Slot::CodeBlockBackground); }
QColor codeBlockColor()      const { return color(Slot::CodeBlock); }
QColor selectionBackground() const { return color(Slot::SelectionBackground); }
```

(The `codeBlockColor()` getter is named with the `Color` suffix to avoid colliding with the underlying `color(Slot)` method.)

Q_GADGET properties don't carry NOTIFY signals — value-type rebinding propagates changes when the whole `Theme` is reassigned via `EditorBackend::setTheme`, which is how QML already receives Theme updates today. No JSON serialisation churn (slot values still serialise via the existing `m_colors` hash; the new properties are pure views).

Future delegates that need additional theme slots add their own Q_PROPERTY view on Theme. The naming convention is to mirror the slot identifier in lowerCamelCase.

### 3.4 LiveView seam

`LiveView.qml` gains:

```qml
property var theme   // Markoff::Theme value type; default undefined → delegates fall back
```

`MarkoffEditor.qml` adds `theme: root.theme` to the existing `LiveView { ... }` block, parallel to the `editorBackend: sourceEditor.editorBackend` line.

## 4. Test plan

Two new methods in `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`:

### 4.1 `selection_highlight_appears_on_hr_and_image()`

Catches: regression of overlay-visible binding on either non-text delegate.

1. Same setup as Test 2 (`mouse_drag_selects_across_block_kinds`), same 5-block fixture.
2. Drag from row 1 (paragraph) through row 4 (code-block) so rows 2 (HR) and 3 (image) are inside the selected range.
3. Locate each delegate via the existing collectDelegates helper.
4. For row 2 (HR) and row 3 (image), `findChild<QQuickItem*>("selectionOverlay")` and assert `visible == true`.

### 4.2 `delegates_consume_theme_colors()`

Catches: regression of the theme-binding plumbing.

1. Build a custom Markoff::Theme with sentinel colours:
   - `Slot::CodeBlockBackground` = `#abcdef`.
   - `Slot::CodeBlock` = `#fedcba`.
2. Pass that theme via context property (instead of `Theme::defaultLight()`).
3. Same 5-block fixture; wait for delegates.
4. Locate `CodeBlockDelegate` (row 4):
   - The outer Rectangle is the delegate's root → `delegate->property("color")` → expect `QColor("#abcdef")`.
   - Find the inner `TextEdit` and assert its `color == #fedcba`.
5. Locate `ImageDelegate` (row 3):
   - The alt-fallback Rectangle gets `objectName: "altFallback"` so we can find it.
   - Assert its `color == #abcdef`.

## 5. Files touched

- `libs/markoff-view-qml/qml/MarkoffEditor.qml` — add `theme: root.theme` to the existing `LiveView { ... }` block.
- `libs/markoff-view-qml/qml/LiveView.qml` — add `property var theme`; thread `theme: root.theme` and (for non-text delegates) `selectionModel: binding.selectionModel` through every `DelegateChoice` block.
- `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml` — add `selectionModel` + `theme` properties, add overlay Rectangle.
- `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml` — add `selectionModel` + `theme` properties, add overlay Rectangle, add `objectName: "altFallback"` to alt-text Rectangle, theme its colour.
- `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml` — add `theme` property, theme background and text colours.
- `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`, `HeadingDelegate.qml` — add `theme` property (uniform surface; no visual change yet).
- `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp` — two new test methods.

- `libs/markoff-foundation/include/markoff-foundation/Theme.h` — three new read-only `Q_PROPERTY` views (`codeBlockBackground`, `codeBlock`, `selectionBackground`) plus their inline getters.

No CMake changes. No QML plugin / registration changes. No `Theme.cpp` changes (getters are inline).

## 6. Verification

- `tst_view_qml_live_view_qml` grows from 2 → 4 test methods.
- Full suite: `ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_)' --output-on-failure -j 8` should report 34 tests (count unchanged at the test-binary level; Qt's QTEST_MAIN counts the binary as 1).
- Manual smoke: `./build-dev/bin/markoff-view-qml-app --live /tmp/live-test.md`, drag across HR and image, observe tinted overlay; toggle to a darker theme (default light vs dark) and observe code-block / image backgrounds change.

## 7. Constraints

- Build with `-j 8`.
- Don't touch master.
- Eight Phase-2 v0 invariants (design spec §4) hold; this change touches none of them.
- Bug-1 lesson: every new delegate property uses `property TYPE name: <default>`, not `required property`.
