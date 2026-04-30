> **Status: completed.** Landed via `bd75ecf` (Theme Q_PROPERTYs) + `78b8f22` (overlay + theme threading). All four tests in `tst_view_qml_live_view_qml.cpp` pass. Do not execute.

# Live-view selection highlight + delegate theming — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tinted overlay to the HR + image delegates whenever they're inside the active selection, and route the three theme slots (`CodeBlockBackground`, `CodeBlock`, `SelectionBackground`) through Markoff's `Theme` so CodeBlockDelegate / ImageDelegate stop hard-coding `#1e1e1e` and `#222`.

**Architecture:** Add three read-only `Q_PROPERTY`s on `Markoff::Theme` (the slots delegates need). Thread `theme` from `MarkoffEditor.qml` through `LiveView.qml` into every `DelegateChoice`; thread `selectionModel` through the two non-text delegates. Each delegate consumes theme via fallback bindings (`theme ? theme.foo : "#default"`). HR + Image gain a sibling overlay Rectangle whose `visible` watches `selectionModel.rangeForBlock(blockIndex).x !== -1`.

**Tech Stack:** Qt 6.8+, QML 6, `Markoff::Theme` (Q_GADGET), existing `LiveSelectionModel` API.

**Spec:** `docs/specs/2026-04-29-live-view-selection-and-theming-design.md`

---

### Task 1: Add Q_PROPERTY views on Theme

Three read-only properties so QML can do `theme.codeBlockBackground` etc.

**Files:**
- Modify: `libs/markoff-foundation/include/markoff-foundation/Theme.h`

- [ ] **Step 1: Add Q_PROPERTY declarations and inline getters to Theme.h**

In `libs/markoff-foundation/include/markoff-foundation/Theme.h`, immediately after the existing `Q_ENUM(FontRole)` line (around line 45), add:

```cpp
    // QML-facing color views over the most-used slots. New delegates that
    // need additional slot views add their own here in lowerCamelCase.
    Q_PROPERTY(QColor codeBlockBackground READ codeBlockBackground)
    Q_PROPERTY(QColor codeBlock           READ codeBlockColor)
    Q_PROPERTY(QColor selectionBackground READ selectionBackground)

    QColor codeBlockBackground() const { return color(Slot::CodeBlockBackground); }
    QColor codeBlockColor()      const { return color(Slot::CodeBlock); }
    QColor selectionBackground() const { return color(Slot::SelectionBackground); }
```

- [ ] **Step 2: Build foundation**

Run:
```bash
cmake --build build-dev --target markoff_foundation -j 8
```

Expected: success.

- [ ] **Step 3: Run foundation tests to confirm no regressions**

Run:
```bash
ctest --test-dir build-dev -R '^tst_foundation_' --output-on-failure -j 8
```

Expected: all foundation tests pass (count unchanged).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-foundation/include/markoff-foundation/Theme.h
git commit -m "foundation: expose theme color slots as Q_PROPERTYs for QML"
```

---

### Task 2: Failing test — `delegates_consume_theme_colors`

Add the theming test BEFORE implementing the QML changes. The test will fail until the delegates honour the theme.

**Files:**
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`

- [ ] **Step 1: Append the new test method to the class**

Open `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`. Append the following test method to the `TstLiveViewQml` class, after `mouse_drag_selects_across_block_kinds`:

```cpp
    void delegates_consume_theme_colors() {
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);

        // Build a custom theme with sentinel colours.
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        const QColor sentinelBg("#abcdef");
        const QColor sentinelFg("#fedcba");
        theme.setColor(Markoff::Theme::Slot::CodeBlockBackground, sentinelBg);
        theme.setColor(Markoff::Theme::Slot::CodeBlock, sentinelFg);

        QQuickView view;
        view.engine()->rootContext()->setContextProperty("doc", &doc);
        view.engine()->rootContext()->setContextProperty(
            "theme", QVariant::fromValue(theme));
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(600, 800);

        QQmlComponent component(view.engine());
        component.setData(
            "import QtQuick\n"
            "import QtQuick.Controls\n"
            "import org.markoff.view.qml\n"
            "MarkoffEditor {\n"
            "    width: 600; height: 800\n"
            "    document: doc\n"
            "    theme: theme\n"
            "    mode: \"live\"\n"
            "}\n",
            QUrl());
        if (component.isError()) qWarning() << component.errors();
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 5);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto collectDelegates = [contentItem]() {
            QHash<int, QQuickItem *> out;
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() >= 0) {
                    out.insert(bi.toInt(), child);
                }
            }
            return out;
        };
        QHash<int, QQuickItem *> delegates;
        QTRY_VERIFY((delegates = collectDelegates()).size() == 5);

        // Row 4: code block. Its outer Rectangle is the delegate root.
        auto *codeBlock = delegates.value(4);
        QVERIFY(codeBlock);
        QCOMPARE(codeBlock->property("color").value<QColor>(), sentinelBg);

        // The inner TextEdit is a child of the code-block Rectangle. Find by
        // metaObject class name (QQuickTextEdit).
        QQuickItem *codeTextEdit = nullptr;
        for (QQuickItem *child : codeBlock->findChildren<QQuickItem *>()) {
            if (QString::fromLatin1(child->metaObject()->className())
                    .startsWith(QLatin1String("QQuickTextEdit"))) {
                codeTextEdit = child;
                break;
            }
        }
        QVERIFY(codeTextEdit);
        QCOMPARE(codeTextEdit->property("color").value<QColor>(), sentinelFg);

        // Row 3: image (alt-fallback Rectangle uses CodeBlockBackground).
        auto *imageDelegate = delegates.value(3);
        QVERIFY(imageDelegate);
        QQuickItem *altFallback =
            imageDelegate->findChild<QQuickItem *>(QStringLiteral("altFallback"));
        QVERIFY(altFallback);
        QCOMPARE(altFallback->property("color").value<QColor>(), sentinelBg);
    }
```

- [ ] **Step 2: Build the test**

```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
```

Expected: builds clean.

- [ ] **Step 3: Run and verify the test FAILS**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: FAIL on `delegates_consume_theme_colors`. The first failure should be on the `codeBlock->property("color")` comparison: the actual colour is the hardcoded `#1e1e1e`, not the sentinel `#abcdef`. (Or earlier if `altFallback` objectName doesn't exist — that's also expected pre-fix.)

- [ ] **Step 4: No commit yet — failing tests committed in Task 4 alongside the fix**

---

### Task 3: Failing test — `selection_highlight_appears_on_hr_and_image`

Same gating: write the failing test first.

**Files:**
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`

- [ ] **Step 1: Append the second test method**

Append to the class, after `delegates_consume_theme_colors`:

```cpp
    void selection_highlight_appears_on_hr_and_image() {
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);

        QQuickView view;
        view.engine()->rootContext()->setContextProperty("doc", &doc);
        view.engine()->rootContext()->setContextProperty(
            "theme", QVariant::fromValue(Markoff::Theme::defaultLight()));
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(600, 800);

        QQmlComponent component(view.engine());
        component.setData(
            "import QtQuick\n"
            "import QtQuick.Controls\n"
            "import org.markoff.view.qml\n"
            "MarkoffEditor {\n"
            "    width: 600; height: 800\n"
            "    document: doc\n"
            "    theme: theme\n"
            "    mode: \"live\"\n"
            "}\n",
            QUrl());
        if (component.isError()) qWarning() << component.errors();
        QVERIFY(!component.isError());

        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(root);
        root->setParentItem(view.contentItem());
        root->setParent(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });
        QVERIFY(parseSpy.wait(2000));

        QQuickItem *listView = root->findChild<QQuickItem *>(
            QStringLiteral("listView"));
        QVERIFY(listView);
        QTRY_COMPARE(listView->property("count").toInt(), 5);

        QMetaObject::invokeMethod(listView, "forceLayout");
        view.grabWindow();

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        auto collectDelegates = [contentItem]() {
            QHash<int, QQuickItem *> out;
            for (QQuickItem *child : contentItem->childItems()) {
                QVariant bi = child->property("blockIndex");
                if (bi.isValid() && bi.toInt() >= 0) {
                    out.insert(bi.toInt(), child);
                }
            }
            return out;
        };
        QHash<int, QQuickItem *> delegates;
        QTRY_VERIFY((delegates = collectDelegates()).size() == 5);

        auto *para = delegates.value(1);
        auto *code = delegates.value(4);
        QVERIFY(para);
        QVERIFY(code);

        // Drag from row 1 (paragraph) through HR + image into row 4 (code).
        const QPointF paraScene =
            para->mapToScene(QPointF(para->width() / 2, para->height() / 2));
        const QPointF codeScene =
            code->mapToScene(QPointF(code->width() / 2, code->height() / 2));
        const QPoint paraPos = paraScene.toPoint();
        const QPoint codePos = codeScene.toPoint();

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, paraPos);
        QTest::qWait(20);
        const int steps = 5;
        for (int i = 1; i <= steps; ++i) {
            QPoint mid(paraPos.x() + (codePos.x() - paraPos.x()) * i / steps,
                       paraPos.y() + (codePos.y() - paraPos.y()) * i / steps);
            QTest::mouseMove(&view, mid);
            QTest::qWait(20);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, codePos);
        QTest::qWait(20);

        // Row 2 (HR) and row 3 (image) must have a visible selection overlay.
        auto *hr = delegates.value(2);
        auto *img = delegates.value(3);
        QVERIFY(hr);
        QVERIFY(img);

        QQuickItem *hrOverlay =
            hr->findChild<QQuickItem *>(QStringLiteral("selectionOverlay"));
        QVERIFY(hrOverlay);
        QVERIFY(hrOverlay->isVisible());

        QQuickItem *imgOverlay =
            img->findChild<QQuickItem *>(QStringLiteral("selectionOverlay"));
        QVERIFY(imgOverlay);
        QVERIFY(imgOverlay->isVisible());
    }
```

- [ ] **Step 2: Build the test**

```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
```

Expected: builds clean.

- [ ] **Step 3: Run and verify the test FAILS**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: FAIL on `selection_highlight_appears_on_hr_and_image` at the first `findChild<QQuickItem*>("selectionOverlay")` — there's no overlay yet.

- [ ] **Step 4: No commit — Task 4 commits both new tests with the fix**

---

### Task 4: Thread theme + selectionModel through LiveView seam

Add `theme` to `LiveView.qml` and `MarkoffEditor.qml`; thread `theme` and `selectionModel` into every `DelegateChoice`. This is the wiring half — delegates haven't gained their new properties yet, so `theme:` on delegates that don't declare it is a no-op until Tasks 5–7.

**Files:**
- Modify: `libs/markoff-view-qml/qml/MarkoffEditor.qml`
- Modify: `libs/markoff-view-qml/qml/LiveView.qml`

- [ ] **Step 1: Forward theme from MarkoffEditor to LiveView**

In `libs/markoff-view-qml/qml/MarkoffEditor.qml`, in the existing `LiveView { ... }` block (around line 47–54), add a `theme: root.theme` line right under `editorBackend: sourceEditor.editorBackend`:

```qml
    LiveView {
        id: liveView
        anchors.fill: parent
        anchors.bottomMargin: searchBar.visible ? searchBar.implicitHeight : 0
        editorBackend: sourceEditor.editorBackend
        theme: root.theme
        visible: root.mode === "live"
        enabled: root.mode === "live"
    }
```

- [ ] **Step 2: Add theme property to LiveView**

In `libs/markoff-view-qml/qml/LiveView.qml`, just below the `property var editorBackend` line (around line 18), add:

```qml
    property var theme   // Markoff::Theme value type; null → delegates fall back to hex defaults
```

- [ ] **Step 3: Thread theme + selectionModel through every DelegateChoice**

In `libs/markoff-view-qml/qml/LiveView.qml`, replace the entire DelegateChooser block. The new bindings are:

- All five DelegateChoices gain `theme: root.theme`.
- HR and Image DelegateChoices additionally gain `selectionModel: binding.selectionModel` (the text-bearing ones already have it).

Replace the `delegate: DelegateChooser { ... }` block (around lines 48–92) with:

```qml
        delegate: DelegateChooser {
            role: "kind"

            DelegateChoice {
                roleValue: "paragraph"
                ParagraphDelegate {
                    blockIndex: index
                    blockText: model.text
                    selectionModel: binding.selectionModel
                    theme: root.theme
                }
            }
            DelegateChoice {
                roleValue: "heading"
                HeadingDelegate {
                    blockIndex: index
                    blockText: model.text
                    headingLevel: model.headingLevel
                    selectionModel: binding.selectionModel
                    theme: root.theme
                }
            }
            DelegateChoice {
                roleValue: "hr"
                HorizontalRuleDelegate {
                    blockIndex: index
                    selectionModel: binding.selectionModel
                    theme: root.theme
                }
            }
            DelegateChoice {
                roleValue: "image"
                ImageDelegate {
                    blockIndex: index
                    imageSrc: model.imageSrc
                    imageAlt: model.imageAlt
                    imageTitle: model.imageTitle
                    selectionModel: binding.selectionModel
                    theme: root.theme
                }
            }
            DelegateChoice {
                roleValue: "code_block"
                CodeBlockDelegate {
                    blockIndex: index
                    codeLanguage: model.codeLanguage
                    codeText: model.codeText
                    selectionModel: binding.selectionModel
                    theme: root.theme
                }
            }
        }
```

- [ ] **Step 4: Build to confirm QML still compiles (delegates haven't grown the new properties yet, so this should produce QML warnings but not compile errors — the engine drops unknown property bindings)**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```

Expected: success. There may be qmllint warnings about `theme` being unknown on certain delegates; we land them in the next tasks.

- [ ] **Step 5: No commit — Tasks 5–7 add the receiving properties; commit comes after the full wiring works**

---

### Task 5: HorizontalRuleDelegate — gain selectionModel + theme + overlay

**Files:**
- Modify: `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml`

- [ ] **Step 1: Replace the entire file**

Overwrite `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml` with:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root

    property int blockIndex: -1
    property var selectionModel: null
    property var theme: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    height: 16

    /// HR has no text content; positionAt returns 0 (start-of-block).
    function positionAt(x, y) { return 0 }
    readonly property int textLength: 0

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: "#888"
    }

    Rectangle {
        objectName: "selectionOverlay"
        anchors.fill: parent
        color: root.theme ? root.theme.selectionBackground : "#406080"
        opacity: 0.35
        visible: root.selectionModel
            && root.selectionModel.rangeForBlock(root.blockIndex).x !== -1
    }
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```

Expected: success.

- [ ] **Step 3: No commit — combined with Tasks 6 + 7**

---

### Task 6: ImageDelegate — gain selectionModel + theme + overlay + altFallback theming

**Files:**
- Modify: `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml`

- [ ] **Step 1: Replace the entire file**

Overwrite `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml` with:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root

    property int    blockIndex: -1
    property string imageSrc: ""
    property string imageAlt: ""
    property string imageTitle: ""
    property var    selectionModel: null
    property var    theme: null

    /// Image has no text content; positionAt returns 0 (start-of-block).
    function positionAt(x, y) { return 0 }
    readonly property int textLength: 0

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    implicitHeight: image.status === Image.Ready ? image.implicitHeight : altLabel.implicitHeight + 16

    Image {
        id: image
        anchors.left: parent.left
        anchors.right: parent.right
        source: root.imageSrc
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        visible: status === Image.Ready
    }

    Rectangle {
        id: altLabel
        objectName: "altFallback"
        visible: image.status !== Image.Ready
        anchors.fill: parent
        color: root.theme ? root.theme.codeBlockBackground : "#222"
        Text {
            anchors.centerIn: parent
            color: "#ccc"
            text: root.imageAlt.length > 0
                ? "[image: " + root.imageAlt + "]"
                : "[image: " + root.imageSrc + "]"
            font.italic: true
        }
    }

    Rectangle {
        objectName: "selectionOverlay"
        anchors.fill: parent
        color: root.theme ? root.theme.selectionBackground : "#406080"
        opacity: 0.35
        visible: root.selectionModel
            && root.selectionModel.rangeForBlock(root.blockIndex).x !== -1
    }
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```

Expected: success.

- [ ] **Step 3: No commit — combined with Task 7**

---

### Task 7: CodeBlockDelegate + Paragraph + Heading — gain theme

ParagraphDelegate and HeadingDelegate get the `theme` property for surface uniformity. CodeBlockDelegate consumes it.

**Files:**
- Modify: `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`
- Modify: `libs/markoff-view-qml/qml/delegates/HeadingDelegate.qml`

- [ ] **Step 1: Replace CodeBlockDelegate.qml**

Overwrite `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml` with:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.kde.syntaxhighlighting

Rectangle {
    id: root

    property int    blockIndex: -1
    property string codeLanguage: ""
    property string codeText: ""
    property var    selectionModel: null
    property var    theme: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    color: root.theme ? root.theme.codeBlockBackground : "#1e1e1e"
    radius: 4
    implicitHeight: textEdit.implicitHeight + 16

    function positionAt(x, y) { return textEdit.positionAt(x, y) }
    readonly property int textLength: textEdit.length

    TextEdit {
        id: textEdit
        anchors.fill: parent
        anchors.margins: 8
        text: root.codeText
        textFormat: TextEdit.PlainText
        readOnly: true
        selectByMouse: false
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: root.theme ? root.theme.codeBlock : "#dcdcdc"

        Connections {
            target: root.selectionModel
            function onSelectionChanged() {
                const r = root.selectionModel.rangeForBlock(root.blockIndex)
                if (r.x === -1) {
                    textEdit.deselect()
                } else {
                    const end = Math.min(r.y, textEdit.length)
                    textEdit.select(r.x, end)
                }
            }
        }
    }

    SyntaxHighlighter {
        textEdit: textEdit
        definition: root.codeLanguage.length > 0 ? root.codeLanguage : "Markdown"
    }
}
```

- [ ] **Step 2: Add `theme` property to ParagraphDelegate**

In `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`, find the property declarations near the top (lines 8–10) and add a `theme` line:

```qml
    property int    blockIndex: -1
    property string blockText: ""
    property var    selectionModel: null
    property var    theme: null
```

No other changes to ParagraphDelegate.

- [ ] **Step 3: Add `theme` property to HeadingDelegate**

In `libs/markoff-view-qml/qml/delegates/HeadingDelegate.qml`, find the property declarations near the top (lines 8–11) and add a `theme` line:

```qml
    property int    blockIndex: -1
    property string blockText: ""
    property int    headingLevel: 0
    property var    selectionModel: null
    property var    theme: null
```

No other changes to HeadingDelegate.

- [ ] **Step 4: Full build**

```bash
cmake --build build-dev -j 8
```

Expected: success.

- [ ] **Step 5: Run the new tests**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: all four user test methods green —
- `delegates_render_model_text`
- `mouse_drag_selects_across_block_kinds`
- `delegates_consume_theme_colors`
- `selection_highlight_appears_on_hr_and_image`

(QtTest output also shows `initTestCase` and `cleanupTestCase` PASS lines; ignore those when counting.)

- [ ] **Step 6: Commit the whole feature**

```bash
git add libs/markoff-view-qml/qml/MarkoffEditor.qml \
        libs/markoff-view-qml/qml/LiveView.qml \
        libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml \
        libs/markoff-view-qml/qml/delegates/ImageDelegate.qml \
        libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml \
        libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml \
        libs/markoff-view-qml/qml/delegates/HeadingDelegate.qml \
        libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp
git commit -m "feat(view-qml): selection overlay on HR/image + theme-driven backgrounds"
```

---

### Task 8: Verify regression catches

For each behaviour the new tests guard, mutate the source, confirm the test fails, restore.

- [ ] **Step 1: Verify selection-overlay catch — mutate HorizontalRuleDelegate**

In `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml`, change the overlay's `visible:` line to `visible: false`.

Build + run:
```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: `selection_highlight_appears_on_hr_and_image` FAILS at `QVERIFY(hrOverlay->isVisible())`.

Restore the original `visible:` binding, rebuild, rerun — passes.

- [ ] **Step 2: Verify theme-binding catch — mutate CodeBlockDelegate**

In `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml`, change the outer Rectangle's `color:` to `color: "#1e1e1e"` (drop the theme branch).

Build + run:
```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: `delegates_consume_theme_colors` FAILS — actual colour is `#1e1e1e`, expected `#abcdef`.

Restore, rebuild, rerun — passes.

- [ ] **Step 3: Confirm clean tree**

```bash
git status
```

Expected: `working tree clean` (the only untracked entry is the pre-existing `libs/jkqtmathtext` from before this work).

---

### Task 9: Full-suite sanity check

- [ ] **Step 1: Full build**

```bash
cmake --build build-dev -j 8
```

Expected: success.

- [ ] **Step 2: Full test run**

```bash
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_)' --output-on-failure -j 8
```

Expected: 34 binaries pass (count unchanged; the 2 new test methods are inside the existing `tst_view_qml_live_view_qml`).

- [ ] **Step 3: Final log check**

```bash
git log --oneline -5
```

Expected the most-recent commits include:
- `feat(view-qml): selection overlay on HR/image + theme-driven backgrounds`
- `foundation: expose theme color slots as Q_PROPERTYs for QML`
- `spec: live-view selection highlight + delegate theming`
