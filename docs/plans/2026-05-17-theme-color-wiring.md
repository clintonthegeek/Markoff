# Theme color wiring (E2.6 extension) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire every visible color in `markoff-live` through `Markoff::Theme` slots so Ctrl+Shift+D visibly inverts the editor and the QtQuick `palette.*` namespace is retired as a color source.

**Architecture:** Add one `Q_INVOKABLE QColor themeColorFor(int slot)` to `LiveListModelBinding`, expose `Markoff::Theme` as a QML uncreatable type via a `QML_FOREIGN` shim, then sweep ~19 delegate color sites onto theme-driven bindings. Propagation relies on the buffer-alternation in `LiveListModelBinding::setTheme` (commit `0aef0f2`).

**Tech Stack:** C++20, Qt 6.8+, QML, CTest, `scripts/run-tests.sh` for offscreen runs. Build cap `-j 8`.

**Working tree:** `.worktrees/foundation-exploration/` on branch `exploration/new-foundation`. All paths in this plan are relative to that worktree root.

**Spec:** `docs/specs/2026-05-17-theme-color-wiring-design.md`.

---

## File map

**Create:**
- `libs/markoff-live/include/markoff/live/ThemeForeign.h` — `QML_FOREIGN` shim exposing `Markoff::Theme` (and its `Q_ENUM(Slot)`) to QML as the `Theme` type.

**Modify (C++):**
- `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` — add `themeColorFor` declaration.
- `libs/markoff-live/src/LiveListModelBinding.cpp` — implement `themeColorFor`.
- `libs/markoff-live/CMakeLists.txt` — register `ThemeForeign.h` in the QML module SOURCES list.

**Modify (QML):**
- `libs/markoff-live/app/Main.qml` — ApplicationWindow `color:` binding.
- `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — sites at lines 89, 97, 118, 175; add `selectionColor`/`selectedTextColor` to the inner TextEdit.
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` — sites at lines 11, 47, 147, 166; add selection bindings.
- `libs/markoff-live/qml/delegates/ImageDelegate.qml` — sites at lines 46, 47, 51, 60, 96.
- `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml` — sites at lines 13, 22.
- `libs/markoff-live/qml/delegates/MathDelegate.qml` — sites at lines 49, 84, 101.

**Modify (tests):**
- `libs/markoff-live/tests/tst_live_render_theme_toggle_propagation.cpp` — extend with `themeColorFor_reflects_active_buffer`.
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` — add 4–5 new slots.

**Modify (docs):**
- `libs/markoff-live/CLAUDE.md` — add the color-binding convention section (P5).
- `docs/e-arc/e-arc-status.md` — recent-changes log entry per phase.

---

## Pre-flight (run once before Task 1)

- [ ] **Verify build is clean.** Run `cmake --build build-dev -j 8` and confirm no errors. Run `scripts/run-tests.sh -R 'tst_live_render_theme'` and confirm 5/5 pass.

---

## Phase P1 — API + QML registration

### Task 1: `themeColorFor` Q_INVOKABLE on `LiveListModelBinding`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_theme_toggle_propagation.cpp`

- [ ] **Step 1: Write the failing test slot.**

Add to `tst_live_render_theme_toggle_propagation.cpp` after the existing `invokable_proxies_reflect_active_theme_after_toggle` slot:

```cpp
void themeColorFor_reflects_active_buffer() {
    LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
    const int kEditorBackground =
        static_cast<int>(Theme::Slot::EditorBackground);

    // Initial buffer = defaultLight.
    QCOMPARE(b.themeColorFor(kEditorBackground),
             Theme::defaultLight().color(Theme::Slot::EditorBackground));

    b.applyDefaultTheme(/*dark=*/true);
    QCOMPARE(b.themeColorFor(kEditorBackground),
             Theme::defaultDark().color(Theme::Slot::EditorBackground));

    b.applyDefaultTheme(/*dark=*/false);
    QCOMPARE(b.themeColorFor(kEditorBackground),
             Theme::defaultLight().color(Theme::Slot::EditorBackground));
}

void themeColorFor_invalid_slot_returns_invalid_qcolor() {
    LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
    QVERIFY(!b.themeColorFor(99999).isValid());
}
```

- [ ] **Step 2: Run the test — expect compile failure.**

Run: `cmake --build build-dev --target tst_live_render_theme_toggle_propagation -j 8`
Expected: compile error — `themeColorFor` is not a member of `LiveListModelBinding`.

- [ ] **Step 3: Add the declaration.**

In `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`, after the existing `themeIsItalic` declaration (around line 127):

```cpp
    Q_INVOKABLE QColor themeColorFor(int slot) const;
```

Include `<QColor>` at the top if not already present (it transitively is via `Theme.h`; verify).

- [ ] **Step 4: Add the implementation.**

In `libs/markoff-live/src/LiveListModelBinding.cpp`, after the existing `themeIsItalic` implementation (around line 331), add:

```cpp
QColor LiveListModelBinding::themeColorFor(int slot) const
{
    return d->themeBuffers[d->activeThemeIdx]
        .color(static_cast<Markoff::Theme::Slot>(slot));
}
```

- [ ] **Step 5: Run the test — expect pass.**

Run: `cmake --build build-dev --target tst_live_render_theme_toggle_propagation -j 8 && scripts/run-tests.sh --bin tst_live_render_theme_toggle_propagation`
Expected: 5/5 PASS (the original 3 + the 2 new slots).

- [ ] **Step 6: Falsifiability proof.**

Temporarily revert the implementation to `return QColor("#ff00ff");` and rebuild + run the test. Both new slots must fail (the first because the color is wrong, the second because `#ff00ff` is valid). Revert your revert. Do NOT commit the stub.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_render_theme_toggle_propagation.cpp
git commit -m "feat(live): themeColorFor Q_INVOKABLE proxy on LiveListModelBinding

P1 task 1 of theme-color-wiring spec. Mirrors the existing
themePixelSizeFor/themeFamilyFor pattern: reads from the active theme
buffer via the buffer-alternation mechanism so QML bindings re-propagate
on dark toggle.

Two new test slots on tst_live_render_theme_toggle_propagation pin the
behaviour: themeColorFor_reflects_active_buffer (toggle round-trip
returns the right colors) and themeColorFor_invalid_slot_returns_invalid_qcolor."
```

---

### Task 2: Expose `Markoff::Theme` to QML via `QML_FOREIGN`

**Files:**
- Create: `libs/markoff-live/include/markoff/live/ThemeForeign.h`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Create the shim header.**

Create `libs/markoff-live/include/markoff/live/ThemeForeign.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/Theme.h>
#include <QtQmlIntegration>

namespace Markoff::Live {

/// QML_FOREIGN shim exposing Markoff::Theme (a Q_GADGET in markoff-core)
/// as the QML type `Theme` in the org.markoff.live module. Theme is value-
/// only — QML cannot construct one. The `Q_ENUM(Slot)` on Markoff::Theme
/// is reachable as `Theme.<name>` in QML.
struct ThemeForeign {
    Q_GADGET
    QML_FOREIGN(Markoff::Theme)
    QML_NAMED_ELEMENT(Theme)
    QML_UNCREATABLE("Theme is a value type; use LiveListModelBinding.theme")
};

}  // namespace Markoff::Live
```

- [ ] **Step 2: Register in the QML module.**

In `libs/markoff-live/CMakeLists.txt`, add `include/markoff/live/ThemeForeign.h` to the `SOURCES` list of `qt_add_qml_module(markoff_live ...)`. Insert it alphabetically near the other `include/markoff/live/...` headers, e.g. right after the `BlockKindRegistry.h` line at ~line 37.

- [ ] **Step 3: Build the library.**

Run: `cmake -S . -B build-dev && cmake --build build-dev --target markoff_live -j 8`
Expected: clean build. AUTOMOC picks up the new header.

- [ ] **Step 4: Smoke-test the QML access path.**

Add a temporary scratch test to confirm `Theme.EditorBackground` resolves from QML. The fastest path: add a new `void theme_slot_enum_resolves_from_qml()` slot in `tst_live_render_theme_toggle_propagation.cpp` using `QQmlComponent` to load a tiny inline QML:

```cpp
void theme_slot_enum_resolves_from_qml() {
    QQmlEngine engine;
    QQmlComponent c(&engine);
    c.setData(R"(
        import QtQml
        import org.markoff.live 1.0
        QtObject {
            property int slot: Theme.EditorBackground
        }
    )", QUrl());
    QScopedPointer<QObject> obj(c.create());
    if (!obj) qWarning() << c.errorString();
    QVERIFY(obj);
    QCOMPARE(obj->property("slot").toInt(),
             static_cast<int>(Markoff::Theme::Slot::EditorBackground));
}
```

Add `#include <QQmlEngine>` and `#include <QQmlComponent>` at the top of the test file. Link `Qt6::Qml` in the test's `target_link_libraries` in `libs/markoff-live/tests/CMakeLists.txt`. (Find the existing `tst_live_render_theme_toggle_propagation` entry and add `Qt6::Qml` to its link line.)

- [ ] **Step 5: Run the smoke test.**

Run: `cmake --build build-dev --target tst_live_render_theme_toggle_propagation -j 8 && scripts/run-tests.sh --bin tst_live_render_theme_toggle_propagation`
Expected: 6/6 PASS.

If the QML can't resolve `Theme.EditorBackground`, the registration form is wrong. Fallback: try `QML_NAMED_ELEMENT(Theme) + QML_FOREIGN_NAMESPACE` patterns from Qt docs, or move the registration to a non-foreign Q_OBJECT wrapper. Update the spec's §"Risks" R3 with what you found.

- [ ] **Step 6: Falsifiability proof.**

Temporarily comment out the `QML_NAMED_ELEMENT(Theme)` line in `ThemeForeign.h`. Rebuild. The new smoke test must fail with a QML error about `Theme` being unknown. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/ThemeForeign.h \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/tst_live_render_theme_toggle_propagation.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "feat(live): expose Markoff::Theme to QML via QML_FOREIGN shim

P1 task 2 of theme-color-wiring spec. ThemeForeign.h is a thin
markoff-live header that registers Markoff::Theme (a Q_GADGET in
markoff-core) as the org.markoff.live QML type 'Theme'. The Q_ENUM(Slot)
becomes reachable as Theme.<name>, letting delegate bindings spell
slot names symbolically instead of with numeric literals.

Smoke test theme_slot_enum_resolves_from_qml pins the resolution path."
```

---

## Phase P2 — Window background + TextEdit selection colors

### Task 3: ApplicationWindow background binds to `EditorBackground`

**Files:**
- Modify: `libs/markoff-live/app/Main.qml`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Write the failing integration test.**

In `tst_live_render_qml_integration.cpp`, after the existing `ctrl_wheel_zooms_font_scale` slot, add:

```cpp
/// Toggling dark mode inverts the ApplicationWindow background colour.
/// Pins the EditorBackground slot binding wired in Main.qml.
void dark_toggle_inverts_window_background() {
    QmlIntegrationFixture fix(/*markdown=*/"sample",
                              /*expectedRowCount=*/1);
    QVERIFY(fix.waitForDelegateAt(0, 2000));

    const QColor lightBg = fix.window()->property("color").value<QColor>();
    QCOMPARE(lightBg,
             Markoff::Theme::defaultLight().color(
                 Markoff::Theme::Slot::EditorBackground));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    const QColor darkBg = fix.window()->property("color").value<QColor>();
    QCOMPARE(darkBg,
             Markoff::Theme::defaultDark().color(
                 Markoff::Theme::Slot::EditorBackground));

    // Round-trip back to light.
    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, false));
    QCoreApplication::processEvents();
    QCOMPARE(fix.window()->property("color").value<QColor>(), lightBg);
}
```

Add `#include <markoff/core/Theme.h>` at the top of the test file if not already there.

- [ ] **Step 2: Run — expect fail.**

Run: `cmake --build build-dev --target tst_live_render_qml_integration -j 8 && scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_inverts_window_background`
Expected: FAIL — `lightBg` does not equal `EditorBackground`; it's the OS default.

- [ ] **Step 3: Wire the binding in Main.qml.**

In `libs/markoff-live/app/Main.qml`, add `color:` to the `ApplicationWindow` between `visible: true` and `title: ctxMain.title`:

```qml
ApplicationWindow {
    id: window
    width: 900
    height: 700
    visible: true
    color: (modelBinding.theme)
           ? modelBinding.themeColorFor(Theme.EditorBackground)
           : "#ffffff"
    title: ctxMain.title

    LiveListModelBinding {
        id: modelBinding
        ...
```

Make sure `import org.markoff.live 1.0` is at the top of the file (it should already be).

- [ ] **Step 4: Run — expect pass.**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_inverts_window_background`
Expected: PASS.

- [ ] **Step 5: Run full live-render suite to catch regressions.**

Run: `scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: same pre-existing failure count as the pre-flight baseline; no new failures.

- [ ] **Step 6: Falsifiability proof.**

Revert the `color:` binding in Main.qml (delete the line). Rebuild. `dark_toggle_inverts_window_background` must fail. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/app/Main.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): ApplicationWindow background binds to EditorBackground

P2 task 1 of theme-color-wiring. ApplicationWindow.color now reads from
Markoff::Theme via LiveListModelBinding.themeColorFor; Ctrl+Shift+D
visibly inverts the window background. Fallback #ffffff for the
transient !theme construction state.

New QML integration slot dark_toggle_inverts_window_background pins the
binding and round-trips through both directions of the toggle."
```

---

### Task 4: TextEdit `selectionColor`/`selectedTextColor` on every text-bearing delegate

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/MathDelegate.qml` (only if it has a TextEdit; verify)
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Audit which delegates have TextEdit children.**

Run: `grep -l 'TextEdit\b' libs/markoff-live/qml/delegates/*.qml`
Expected output names: at minimum `UnifiedInlineTextDelegate.qml` and `CodeBlockDelegate.qml`. Verify whether `MathDelegate.qml` and `ImageDelegate.qml` (caption TextEdit) have one.

For each TextEdit found, the bindings to add are:

```qml
selectionColor: (root.liveBinding && root.liveBinding.theme)
                ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
                : "#b0d0ff"
selectedTextColor: (root.liveBinding && root.liveBinding.theme)
                   ? root.liveBinding.themeColorFor(Theme.EditorBackground)
                   : "#ffffff"
```

(Note: `root.liveBinding` references the delegate's binding property. If a delegate uses a different identifier, replace accordingly. Verify by reading the delegate's `required property` declaration.)

- [ ] **Step 2: Write the failing test.**

In `tst_live_render_qml_integration.cpp`, add:

```cpp
/// Toggling dark mode changes TextEdit selectionColor on the first
/// realized text-bearing delegate.
void dark_toggle_changes_textedit_selection_color() {
    QmlIntegrationFixture fix(/*markdown=*/"sample text",
                              /*expectedRowCount=*/1);
    QVERIFY(fix.waitForDelegateAt(0, 2000));
    QQuickItem *te = fix.delegateTextEdit(0);
    QVERIFY(te != nullptr);

    const QColor lightSel = te->property("selectionColor").value<QColor>();
    QCOMPARE(lightSel,
             Markoff::Theme::defaultLight().color(
                 Markoff::Theme::Slot::SelectionBackground));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    const QColor darkSel = te->property("selectionColor").value<QColor>();
    QCOMPARE(darkSel,
             Markoff::Theme::defaultDark().color(
                 Markoff::Theme::Slot::SelectionBackground));
}
```

- [ ] **Step 3: Run — expect fail.**

Run: `cmake --build build-dev --target tst_live_render_qml_integration -j 8 && scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_textedit_selection_color`
Expected: FAIL — `lightSel` matches the OS default, not `SelectionBackground`.

- [ ] **Step 4: Add selection bindings to each TextEdit identified in Step 1.**

For each TextEdit, add the two bindings from Step 1 (preserve existing properties). Common location: alongside the existing `text:`, `font.*`, `cursorPosition:` bindings.

- [ ] **Step 5: Run — expect pass.**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_textedit_selection_color`
Expected: PASS.

- [ ] **Step 6: Run full suite.**

Run: `scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: no new failures.

- [ ] **Step 7: Falsifiability proof.**

Comment out the `selectionColor:` binding you added in `UnifiedInlineTextDelegate.qml`. Rebuild. `dark_toggle_changes_textedit_selection_color` must fail. Revert.

- [ ] **Step 8: Commit.**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/qml/delegates/CodeBlockDelegate.qml \
        libs/markoff-live/qml/delegates/MathDelegate.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): TextEdit selection colours bind to Theme

P2 task 2 of theme-color-wiring. selectionColor and selectedTextColor
on every TextEdit-bearing delegate now read from Markoff::Theme
(SelectionBackground / EditorBackground). Dark toggle visibly inverts
selection rendering.

New QML integration slot dark_toggle_changes_textedit_selection_color
pins the binding."
```

(Only stage files that actually changed — if MathDelegate has no TextEdit, drop it from the `git add`.)

---

## Phase P3 — `UnifiedInlineTextDelegate` color migration

### Task 5: TextEdit text color per kind (site at line 175)

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Write the failing test.**

In `tst_live_render_qml_integration.cpp`:

```cpp
/// Per-kind TextEdit colour reads from the correct theme slot.
/// Doc has paragraph, H1, blockquote, list-item — each delegate's
/// TextEdit colour matches the kind's theme slot, and changes on toggle.
void dark_toggle_changes_textedit_color_per_kind() {
    const QString md = "Paragraph text\n\n"
                       "# Heading One\n\n"
                       "> A quote\n\n"
                       "- list item\n";
    QmlIntegrationFixture fix(md, /*expectedRowCount=*/4);
    QVERIFY(fix.waitForDelegateAt(3, 2000));

    auto colorAt = [&](int row) -> QColor {
        QQuickItem *te = fix.delegateTextEdit(row);
        Q_ASSERT(te);
        return te->property("color").value<QColor>();
    };

    using Slot = Markoff::Theme::Slot;
    const auto L = Markoff::Theme::defaultLight();
    QCOMPARE(colorAt(0), L.color(Slot::TextDefault));
    QCOMPARE(colorAt(1), L.color(Slot::Heading1));
    QCOMPARE(colorAt(2), L.color(Slot::Quote));
    QCOMPARE(colorAt(3), L.color(Slot::TextDefault));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    const auto D = Markoff::Theme::defaultDark();
    QCOMPARE(colorAt(0), D.color(Slot::TextDefault));
    QCOMPARE(colorAt(1), D.color(Slot::Heading1));
    QCOMPARE(colorAt(2), D.color(Slot::Quote));
    QCOMPARE(colorAt(3), D.color(Slot::TextDefault));
}
```

- [ ] **Step 2: Run — expect fail.**

Run: `cmake --build build-dev --target tst_live_render_qml_integration -j 8 && scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_textedit_color_per_kind`
Expected: FAIL — paragraph/heading/blockquote/list all return the same `palette.text` color.

- [ ] **Step 3: Replace the TextEdit `color:` binding.**

In `UnifiedInlineTextDelegate.qml`, at line 175 (the inner TextEdit `color: palette.text`), replace with:

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(root.themeSlot)
       : "#222222"
```

The existing `themeSlot` property (declared at line 37) already dispatches `TextDefault` / `Heading1..6` / `Quote` by kind.

- [ ] **Step 4: Run — expect pass.**

Run: `scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_textedit_color_per_kind`
Expected: PASS.

- [ ] **Step 5: Full suite.**

Run: `scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: no new failures.

- [ ] **Step 6: Falsifiability proof.**

Re-replace the binding with `color: palette.text`. Rebuild. The test must fail on all four `colorAt` assertions. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): UnifiedInlineTextDelegate TextEdit colour via Theme

P3 task 1 of theme-color-wiring. The text-bearing delegate's inner
TextEdit colour now flows through Markoff::Theme via the existing
per-kind themeSlot dispatch — TextDefault for paragraph and list-item,
Heading1..6 for headings, Quote for blockquote.

New QML integration slot dark_toggle_changes_textedit_color_per_kind
pins all four kinds light + dark."
```

---

### Task 6: Blockquote bar, list-marker selection, list-marker text

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
/// Blockquote left-bar colour and list-item marker colour both bind to
/// theme slots and react to dark toggle.
void dark_toggle_changes_blockquote_bar_and_list_marker() {
    const QString md = "> Quote\n\n- item\n";
    QmlIntegrationFixture fix(md, /*expectedRowCount=*/2);
    QVERIFY(fix.waitForDelegateAt(1, 2000));

    // Locate the blockquote bar Rectangle by objectName.
    QQuickItem *quoteDelegate = fix.delegateAt(0);
    QQuickItem *bar = quoteDelegate->findChild<QQuickItem*>("blockquoteBar");
    QVERIFY(bar);
    QCOMPARE(bar->property("color").value<QColor>(),
             Markoff::Theme::defaultLight().color(
                 Markoff::Theme::Slot::Quote));

    // Marker label.
    QQuickItem *itemDelegate = fix.delegateAt(1);
    QQuickItem *marker = itemDelegate->findChild<QQuickItem*>("listMarkerLabel");
    QVERIFY(marker);
    QCOMPARE(marker->property("color").value<QColor>(),
             Markoff::Theme::defaultLight().color(
                 Markoff::Theme::Slot::TextDefault));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    QCOMPARE(bar->property("color").value<QColor>(),
             Markoff::Theme::defaultDark().color(
                 Markoff::Theme::Slot::Quote));
    QCOMPARE(marker->property("color").value<QColor>(),
             Markoff::Theme::defaultDark().color(
                 Markoff::Theme::Slot::TextDefault));
}
```

- [ ] **Step 2: Run — expect fail.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_blockquote_bar_and_list_marker`
Expected: FAIL — either the `findChild` returns null (no `objectName` set yet) or the colour is `palette.highlight`/`palette.text`.

- [ ] **Step 3: Add `objectName` markers + replace bindings.**

In `UnifiedInlineTextDelegate.qml`:

For the `blockquoteBar` Rectangle (around line 80–91), add `objectName: "blockquoteBar"` and replace `color: palette.highlight`:

```qml
Rectangle {
    id: blockquoteBar
    objectName: "blockquoteBar"
    visible: root.kind === "blockquote"
    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
    width: 3
    color: (root.liveBinding && root.liveBinding.theme)
           ? root.liveBinding.themeColorFor(Theme.Quote)
           : "#666666"
    opacity: 0.6
}
```

For the list-marker selection backdrop Rectangle (line 94–99), replace `color: palette.highlight`:

```qml
Rectangle {
    anchors.fill: markerLabel
    visible: root.kind === "list-item" && root._fullySelected
    color: (root.liveBinding && root.liveBinding.theme)
           ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
           : "#b0d0ff"
    z: -1
}
```

For the marker label (around line 118), find the `markerLabel` Text element (search for `id: markerLabel`) and add `objectName: "listMarkerLabel"`, then replace the `color:` binding:

```qml
Text {
    id: markerLabel
    objectName: "listMarkerLabel"
    // ... existing properties ...
    color: root._fullySelected
           ? ((root.liveBinding && root.liveBinding.theme)
              ? root.liveBinding.themeColorFor(Theme.EditorBackground)
              : "#ffffff")
           : ((root.liveBinding && root.liveBinding.theme)
              ? root.liveBinding.themeColorFor(root.markerSlot)
              : "#222222")
}
```

(The `markerSlot` property at line 56 is already `TextDefault`.)

- [ ] **Step 4: Run — expect pass.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_blockquote_bar_and_list_marker`
Expected: PASS.

- [ ] **Step 5: Full suite.**

`scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: no new failures.

- [ ] **Step 6: Falsifiability proof.**

Revert the `blockquoteBar` color binding to `palette.highlight`. The test must fail on the bar assertion. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): blockquote bar + list marker bind through Theme

P3 task 2 of theme-color-wiring. Blockquote left bar reads
Theme.Quote; list-marker selection backdrop reads SelectionBackground;
marker text uses TextDefault normally and EditorBackground when the
marker is selected (inverse contrast on the selection bg).
objectName markers added so QML integration tests can target the
internal Rectangles."
```

---

## Phase P4 — Remaining delegate migrations

### Task 7: `CodeBlockDelegate` color sites

**Files:**
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
/// CodeBlock root background + body text + language label all bind
/// through Theme. Dark toggle visibly inverts all three.
void dark_toggle_changes_codeblock_colors() {
    const QString md = "```cpp\nint main() { return 0; }\n```\n";
    QmlIntegrationFixture fix(md, /*expectedRowCount=*/1);
    QVERIFY(fix.waitForDelegateAt(0, 2000));

    QQuickItem *root = fix.delegateAt(0);
    QQuickItem *body = fix.delegateTextEdit(0);
    QVERIFY(root && body);

    using Slot = Markoff::Theme::Slot;
    const auto L = Markoff::Theme::defaultLight();
    QCOMPARE(root->property("color").value<QColor>(),
             L.color(Slot::CodeBlockBackground));
    QCOMPARE(body->property("color").value<QColor>(),
             L.color(Slot::CodeBlock));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    const auto D = Markoff::Theme::defaultDark();
    QCOMPARE(root->property("color").value<QColor>(),
             D.color(Slot::CodeBlockBackground));
    QCOMPARE(body->property("color").value<QColor>(),
             D.color(Slot::CodeBlock));
}
```

- [ ] **Step 2: Run — expect fail.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_codeblock_colors`
Expected: FAIL.

- [ ] **Step 3: Update `CodeBlockDelegate.qml` bindings.**

Replace site at line 11 (root Rectangle `color: Qt.rgba(0, 0, 0, 0.05)`):

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.CodeBlockBackground)
       : "#f4f4f4"
```

Replace TextEdit `color: palette.text` at line 47:

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.CodeBlock)
       : "#222222"
```

Replace language label `color: palette.mid` at line 147:

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.Quote)
       : "#666666"
```

Replace the second `color: palette.text` at line 166:

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.CodeBlock)
       : "#222222"
```

- [ ] **Step 4: Run — expect pass.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_codeblock_colors`
Expected: PASS.

- [ ] **Step 5: Full suite.**

`scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: no new failures.

- [ ] **Step 6: Falsifiability proof.**

Revert the root-Rectangle color binding to `Qt.rgba(0, 0, 0, 0.05)`. Test must fail on `root->property("color")` assertion. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/qml/delegates/CodeBlockDelegate.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): CodeBlockDelegate colours through Theme

P4 task 1 of theme-color-wiring. Code-block root surface reads
CodeBlockBackground; body text reads CodeBlock; language label and
secondary muted text read Quote. Falls back to hex literals matching
the defaultLight values during construction.

New QML integration slot dark_toggle_changes_codeblock_colors pins
all three surfaces light + dark."
```

---

### Task 8: `ImageDelegate` color sites

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ImageDelegate.qml`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
/// ImageDelegate placeholder surface + muted accents bind via Theme.
void dark_toggle_changes_image_placeholder_colors() {
    const QString md = "![missing](nonexistent.png)\n";
    QmlIntegrationFixture fix(md, /*expectedRowCount=*/1);
    QVERIFY(fix.waitForDelegateAt(0, 2000));

    QQuickItem *delegate = fix.delegateAt(0);
    QQuickItem *placeholder =
        delegate->findChild<QQuickItem*>("imagePlaceholder");
    QVERIFY(placeholder);

    using Slot = Markoff::Theme::Slot;
    QCOMPARE(placeholder->property("color").value<QColor>(),
             Markoff::Theme::defaultLight().color(Slot::CodeBlockBackground));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    QCOMPARE(placeholder->property("color").value<QColor>(),
             Markoff::Theme::defaultDark().color(Slot::CodeBlockBackground));
}
```

- [ ] **Step 2: Run — expect fail.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_image_placeholder_colors`
Expected: FAIL — either the `findChild` returns null or the colour is `palette.alternateBase`.

- [ ] **Step 3: Update `ImageDelegate.qml`.**

For each color site identified in the §"File map":

- Line 46 (`color: palette.alternateBase`): add `objectName: "imagePlaceholder"` to the Rectangle, replace color:

```qml
Rectangle {
    objectName: "imagePlaceholder"
    // existing properties
    color: (root.liveBinding && root.liveBinding.theme)
           ? root.liveBinding.themeColorFor(Theme.CodeBlockBackground)
           : "#f4f4f4"
    border.color: (root.liveBinding && root.liveBinding.theme)
                  ? root.liveBinding.themeColorFor(Theme.Quote)
                  : "#666666"
```

- Line 51, 60 (`color: palette.mid`): replace with the `Theme.Quote` binding (with `"#666666"` fallback).
- Line 96 (`border.color: palette.highlight`): replace with `Theme.SelectionBackground` (fallback `"#b0d0ff"`).

- [ ] **Step 4: Run — expect pass.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_image_placeholder_colors`
Expected: PASS.

- [ ] **Step 5: Full suite.**

`scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: no new failures.

- [ ] **Step 6: Falsifiability proof.**

Revert placeholder Rectangle `color:` to `palette.alternateBase`. Test must fail. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/qml/delegates/ImageDelegate.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): ImageDelegate colours through Theme

P4 task 2 of theme-color-wiring. Placeholder surface reads
CodeBlockBackground; muted borders + icon + caption read Quote;
selected-border reads SelectionBackground. objectName markers added
for integration test targeting."
```

---

### Task 9: `HorizontalRuleDelegate` color sites

**Files:**
- Modify: `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
/// HR rule line + selection border bind via Theme.
void dark_toggle_changes_hr_colors() {
    const QString md = "para\n\n---\n\nmore\n";
    QmlIntegrationFixture fix(md, /*expectedRowCount=*/3);
    QVERIFY(fix.waitForDelegateAt(2, 2000));

    QQuickItem *hr = fix.delegateAt(1);
    QQuickItem *rule = hr->findChild<QQuickItem*>("hrRule");
    QVERIFY(rule);

    QCOMPARE(rule->property("color").value<QColor>(),
             Markoff::Theme::defaultLight().color(
                 Markoff::Theme::Slot::Quote));

    QMetaObject::invokeMethod(fix.binding(), "applyDefaultTheme",
                              Q_ARG(bool, true));
    QCoreApplication::processEvents();

    QCOMPARE(rule->property("color").value<QColor>(),
             Markoff::Theme::defaultDark().color(
                 Markoff::Theme::Slot::Quote));
}
```

- [ ] **Step 2: Run — expect fail.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_hr_colors`
Expected: FAIL.

- [ ] **Step 3: Update `HorizontalRuleDelegate.qml`.**

For the rule Rectangle at line 13, add `objectName: "hrRule"` and replace color:

```qml
Rectangle {
    objectName: "hrRule"
    // existing properties
    color: root.isSelected
           ? ((root.liveBinding && root.liveBinding.theme)
              ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
              : "#b0d0ff")
           : ((root.liveBinding && root.liveBinding.theme)
              ? root.liveBinding.themeColorFor(Theme.Quote)
              : "#666666")
}
```

For the selection border at line 22 (`border.color: palette.highlight`), replace with the `Theme.SelectionBackground` binding.

- [ ] **Step 4: Run — expect pass.**

`scripts/run-tests.sh --bin tst_live_render_qml_integration dark_toggle_changes_hr_colors`
Expected: PASS.

- [ ] **Step 5: Full suite.**

`scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'`
Expected: no new failures.

- [ ] **Step 6: Falsifiability proof.**

Revert the rule `color:` to `root.isSelected ? palette.highlight : palette.mid`. Test must fail. Revert.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "feat(live): HorizontalRuleDelegate colours through Theme

P4 task 3 of theme-color-wiring. HR rule reads Quote normally and
SelectionBackground when selected; selection border reads
SelectionBackground. objectName 'hrRule' added for integration test
targeting."
```

---

### Task 10: `MathDelegate` color sites

**Files:**
- Modify: `libs/markoff-live/qml/delegates/MathDelegate.qml`

- [ ] **Step 1: Replace bindings.**

Math is uncovered by an integration test (math rendering uses jkqtmathtext, not a QML-introspectable color property), so this task is structural-only — bindings updated, full suite must remain green.

For each site:
- Line 49 (`color: palette.mid`): replace with `Theme.Quote` binding (fallback `"#666666"`).
- Line 84 (`color: palette.text`): replace with `Theme.TextDefault` binding (fallback `"#222222"`).
- Line 101 (`border.color: palette.highlight`): replace with `Theme.SelectionBackground` binding (fallback `"#b0d0ff"`).

- [ ] **Step 2: Build + full suite.**

```bash
cmake --build build-dev --target markoff_live -j 8
scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'
```
Expected: clean build; no new failures.

- [ ] **Step 3: Falsifiability sanity-check.**

This delegate has no integration test, so falsifiability is a manual eyeball: confirm that `git diff libs/markoff-live/qml/delegates/MathDelegate.qml` shows three `palette.*` → theme-binding replacements with matching fallback colors.

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/qml/delegates/MathDelegate.qml
git commit -m "feat(live): MathDelegate colours through Theme

P4 task 4 of theme-color-wiring. Math placeholder muted text reads
Quote; body text reads TextDefault; selected border reads
SelectionBackground. No new integration test — math rendering is via
jkqtmathtext and isn't QML-property-introspectable. Sweep covered by
P5's palette.* audit."
```

---

## Phase P5 — Cleanup + convention

### Task 11: `palette.*` sweep

**Files:**
- Audit: `libs/markoff-live/qml/`

- [ ] **Step 1: Inventory remaining references.**

Run: `grep -rn 'palette\.' libs/markoff-live/qml/`

Each remaining reference falls into one of:
- **Intentional** — for text-cursor cosmetics, OS-native widget appearance (e.g., `RemoteCursorOverlay.qml` collab-cursor uses a remote-data color, not Theme; not a violation).
- **Missed** — a color site this plan didn't enumerate. Update with the same `(theme) ? themeColorFor(...) : fallback` pattern, mapping to the most appropriate slot. Reuse existing slots per the no-new-slots non-goal.

- [ ] **Step 2: Document intentional exceptions.**

For every `palette.*` that survives, add a one-line `// palette intentional — <reason>` comment above the line. The audit is now self-documenting.

- [ ] **Step 3: Build + full suite.**

```bash
cmake --build build-dev --target markoff_live -j 8
scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_live_render_e2_5_perf_bulk_paste'
```
Expected: clean build; no new failures.

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/qml/
git commit -m "chore(live): palette.* audit — mark surviving uses intentional

P5 task 1 of theme-color-wiring. Every remaining 'palette.X' in
libs/markoff-live/qml/ has either been migrated to a Theme binding or
documented inline with '// palette intentional — <reason>'. The
audit is now self-documenting; future agents can grep for the
remaining cases."
```

---

### Task 12: Update `libs/markoff-live/CLAUDE.md` with the color-binding convention

**Files:**
- Modify: `libs/markoff-live/CLAUDE.md`

- [ ] **Step 1: Add a new section.**

In `libs/markoff-live/CLAUDE.md`, add a section after "Inline-format highlighter (E1)":

```markdown
## Color binding convention (E2.6)

Every visible color in a delegate reads from `Markoff::Theme` via
`LiveListModelBinding`'s Q_INVOKABLE proxies. The pattern:

\`\`\`qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.TextDefault)
       : "#222222"
\`\`\`

- The `root.liveBinding.theme` LHS read gives QML a NOTIFY dependency
  on the Theme Q_PROPERTY. Without this anchor, `themeColorFor` calls
  would not re-evaluate on dark toggle.
- The `:" #xxxxxx"` fallback is the corresponding `defaultLight()`
  color, applied during the transient construction state before
  `liveBinding` is wired.
- Slot names spell symbolically via `Theme.<Name>` — the
  `Markoff::Theme` Q_GADGET is exposed via `QML_FOREIGN` in
  `ThemeForeign.h`.

Do **not** use `palette.text` / `palette.highlight` / `palette.mid` /
`palette.alternateBase` for editor colors. The QtQuick Controls palette
is OS-driven and won't follow our Theme. The only intentional surviving
palette usage in delegates is documented inline with
`// palette intentional — <reason>`.

Two slots are deliberately reused as multi-purpose accents:

- **`Quote`** — blockquote text + HR + placeholder borders + muted
  secondary accent. If E3+ callout coloring needs separation, the slot
  splits in its own spec.
- **`CodeBlockBackground`** — code-block surface + image-placeholder
  surface.

Spec: `docs/specs/2026-05-17-theme-color-wiring-design.md`.
```

- [ ] **Step 2: Commit.**

```bash
git add libs/markoff-live/CLAUDE.md
git commit -m "docs(live): color-binding convention in CLAUDE.md

P5 task 2 of theme-color-wiring. Documents the (theme && liveBinding) ?
themeColorFor(slot) : fallback pattern, the no-palette.* rule for editor
colors, and the intentional Quote / CodeBlockBackground multi-purpose
slot reuse."
```

---

### Task 13: Update `docs/e-arc/e-arc-status.md`

**Files:**
- Modify: `docs/e-arc/e-arc-status.md`

- [ ] **Step 1: Update TL;DR.**

In `docs/e-arc/e-arc-status.md`, replace the "2026-05-17 update" TL;DR entry (top of TL;DR section) with one that reflects the completion:

```markdown
> **2026-05-17 update (theme-color-wiring complete).** E2.5 tagged `v0.7.0-e2.5`. E2.6 was extended to cover the theme-color-wiring gap surfaced by the dark-toggle dogfood — every delegate now reads colours from `Markoff::Theme` (commits for tasks 1–13 of `docs/plans/2026-05-17-theme-color-wiring.md`). Tag `v0.7.0-e2.6` releases when interactive re-dogfood confirms Ctrl+Shift+D visibly inverts the editor.
```

- [ ] **Step 2: Append recent-changes-log row.**

Add to the recent-changes log table (the first data row, right under the header):

```markdown
| 2026-05-17 | (theme-color-wiring) | **E2.6 extension — full Theme color wiring.** Spec `docs/specs/2026-05-17-theme-color-wiring-design.md`; plan `docs/plans/2026-05-17-theme-color-wiring.md`. Added `themeColorFor(int)` Q_INVOKABLE and `ThemeForeign.h` (`QML_FOREIGN` for `Markoff::Theme`); migrated 19 color sites across Main.qml + 5 delegates from `palette.*`/hardcoded hex to `themeColorFor(Theme.X)` bindings. Tag `v0.7.0-e2.6` held pending re-dogfood. |
```

- [ ] **Step 3: Update E2.6 row in the phase board.**

Change E2.6's status cell from `dogfood` (re-pass pending) to `dogfood (re-pass pending — Theme colors wired)`, and append to the Notes column: `Theme-color-wiring extension complete (2026-05-17): all 19 color sites migrated via spec 2026-05-17-theme-color-wiring-design.md.`

- [ ] **Step 4: Commit.**

```bash
git add docs/e-arc/e-arc-status.md
git commit -m "docs: e-arc-status — theme-color-wiring complete

Phase board: E2.6 notes extended with theme-color-wiring completion.
Recent-changes log: row added for 2026-05-17 theme-color-wiring
spec+plan execution. TL;DR points at the spec + plan and notes the
v0.7.0-e2.6 tag remains held pending interactive re-dogfood."
```

---

## Re-dogfood checklist (post-Task 13, for the user)

After all tasks land, the user runs interactive dogfood:

- [ ] **Ctrl+wheel** still zooms text in/out smoothly.
- [ ] **Ctrl+= / Ctrl+- / Ctrl+0** still work for zoom.
- [ ] **Ctrl+Shift+D** now visibly inverts the entire editor — window background turns dark, default text turns light, code-block background darkens, headings stay readable on the new background, blockquote bar remains visible.
- [ ] **Selection** (drag, Shift+arrow) renders with the new theme's selection color light/dark.
- [ ] Round-trip Ctrl+Shift+D back to light — everything returns to the original colors.

If all pass: tag `v0.7.0-e2.6` at the head of the theme-color-wiring chain.

---

## Pre-merge sanity checks (run at the end of execution, before re-dogfood handoff)

- [ ] `scripts/run-tests.sh` — full suite, confirm the pre-existing failure count from pre-flight is unchanged (~3 known pre-existing).
- [ ] `cmake --build build-dev -j 8` from a clean configure (`rm -rf build-dev && cmake -S . -B build-dev && cmake --build build-dev -j 8`) — confirms the new headers are correctly listed in the QML module SOURCES and AUTOMOC sees them.
- [ ] `grep -rn 'palette\.' libs/markoff-live/qml/` — every remaining match is followed by `// palette intentional — ` on the same or adjacent line.
- [ ] The chain of commits from this plan is linear and revertible task-by-task.
