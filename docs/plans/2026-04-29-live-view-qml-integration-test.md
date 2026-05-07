> **Status: completed.** Landed via `a64f722` / `89b8931` / `1406a64` / `7d8e244`. `tst_view_qml_live_view_qml.cpp` exists with all four test methods passing. Do not execute.

# Live View QML integration test — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a C++ Qt-Test that loads `MarkoffEditor.qml` in `mode: "live"`, verifies delegates render model data, and synthesizes a cross-block mouse drag — catching the dogfood bug class (commit `b03b0d0`).

**Architecture:** New file `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp` instantiates a `QQuickView` with `MarkoffEditor`, drives it with `QTest::mouse*`, and reads back via `findChild<QQuickItem*>` + property access. One supporting QML touch: an `objectName` on `LiveView.qml`'s `ListView` for discovery.

**Tech Stack:** Qt 6.8+, `QTest`, `QQuickView`, `QSignalSpy`, `QTRY_COMPARE`, `MarkoffDocument` from `markoff-foundation`, the project's static QML plugin `markoff_view_qmlplugin`.

**Spec:** `docs/specs/2026-04-29-live-view-qml-integration-test-design.md`

---

### Task 1: Add `objectName` to ListView for test discovery

**Files:**
- Modify: `libs/markoff-view-qml/qml/LiveView.qml` — `ListView { id: listView … }` block

- [ ] **Step 1: Edit `LiveView.qml`**

In the `ListView { id: listView … }` block (around line 41–46), add `objectName: "listView"` immediately after `id: listView`.

After:
```qml
ListView {
    id: listView
    objectName: "listView"
    anchors.fill: parent
    clip: true
    spacing: 12
```

- [ ] **Step 2: Build to confirm no QML breakage**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```

Expected: success, no `qmllint` errors mentioning `objectName`.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-view-qml/qml/LiveView.qml
git commit -m "test(view-qml): tag ListView with objectName for findChild lookup"
```

---

### Task 2: Add CMake target for the new test (skeleton only)

**Files:**
- Modify: `libs/markoff-view-qml/tests/CMakeLists.txt`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp` (skeleton)

- [ ] **Step 1: Create the test skeleton**

Write `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QtPlugin>

Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

class TstLiveViewQml : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QSKIP("QGuiApplication required for QQuickView");
        }
    }

    void placeholder() {
        QVERIFY(true);
    }
};

QTEST_MAIN(TstLiveViewQml)
#include "tst_view_qml_live_view_qml.moc"
```

- [ ] **Step 2: Append CMake stanza to tests/CMakeLists.txt**

Append after the `tst_view_qml_app_smoke` stanza (end of file):

```cmake
add_executable(tst_view_qml_live_view_qml tst_view_qml_live_view_qml.cpp)
add_test(NAME tst_view_qml_live_view_qml COMMAND tst_view_qml_live_view_qml)
target_link_libraries(tst_view_qml_live_view_qml
    PRIVATE Qt6::Test Qt6::Qml Qt6::Quick markoff_view_qml markoff_view_qmlplugin markoff_core)
qt6_import_qml_plugins(tst_view_qml_live_view_qml)
set_tests_properties(tst_view_qml_live_view_qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Configure + build the new target**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
```

Expected: builds clean.

- [ ] **Step 4: Run the placeholder to confirm CMake/test wiring**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: `1 passed`.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-view-qml/tests/CMakeLists.txt libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp
git commit -m "test(view-qml): scaffold tst_view_qml_live_view_qml"
```

---

### Task 3: Test 1 — `delegates_render_model_text`

Catches Bug 1 (`required property` + `DelegateChoice` silently failed to inject role data, leaving every text-bearing delegate empty).

**Files:**
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`

- [ ] **Step 1: Replace `placeholder()` with the new test method**

Replace the class body with:

```cpp
private Q_SLOTS:
    void initTestCase() {
        if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QSKIP("QGuiApplication required for QQuickView");
        }
    }

    void delegates_render_model_text() {
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

        // Seed the document and wait for parse + delegate instantiation.
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

        // Walk the contentItem children and verify per-row properties.
        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        // Index delegates by their blockIndex property (order in childItems()
        // is not guaranteed by QQuickItem).
        QHash<int, QQuickItem *> delegates;
        for (QQuickItem *child : contentItem->childItems()) {
            QVariant bi = child->property("blockIndex");
            if (bi.isValid() && bi.toInt() >= 0) {
                delegates.insert(bi.toInt(), child);
            }
        }
        QCOMPARE(delegates.size(), 5);

        QCOMPARE(delegates.value(0)->property("blockText").toString(),
                 QStringLiteral("Heading"));
        QCOMPARE(delegates.value(1)->property("blockText").toString(),
                 QStringLiteral("Para text."));
        // Row 2 is HR — no text-bearing property.
        QCOMPARE(delegates.value(3)->property("imageSrc").toString(),
                 QStringLiteral("http://example.com/img.png"));
        QCOMPARE(delegates.value(3)->property("imageAlt").toString(),
                 QStringLiteral("alt"));
        QCOMPARE(delegates.value(4)->property("codeText").toString(),
                 QStringLiteral("x = 1"));
    }
```

Note: the previous `placeholder()` test slot is removed; the `initTestCase` slot stays unchanged.

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
```

Expected: builds clean.

- [ ] **Step 3: Run**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: 1 test passes (the new `delegates_render_model_text`). If `blockText` reads come back empty, the build is at a state where Bug 1 has regressed — investigate before continuing.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp
git commit -m "test(view-qml): assert delegates render model text (catches Bug 1)"
```

---

### Task 4: Test 2 — `mouse_drag_selects_across_block_kinds`

Catches Bugs 2 (`model.index` not a role), 3 (`positionAt` missing on Item-wrapped delegates), 4 (`blockIndex` missing on non-text delegates).

**Files:**
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp`

- [ ] **Step 1: Add the new test method**

Append the following method to the class, after `delegates_render_model_text`:

```cpp
    void mouse_drag_selects_across_block_kinds() {
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

        auto *contentItem = qvariant_cast<QQuickItem *>(
            listView->property("contentItem"));
        QVERIFY(contentItem);

        QHash<int, QQuickItem *> delegates;
        for (QQuickItem *child : contentItem->childItems()) {
            QVariant bi = child->property("blockIndex");
            if (bi.isValid() && bi.toInt() >= 0) {
                delegates.insert(bi.toInt(), child);
            }
        }
        QCOMPARE(delegates.size(), 5);

        // Translate the centre of row 1 (paragraph) and row 3 (image) into
        // window coordinates. Use mid-X to avoid the leading 12-px margin.
        auto *para = delegates.value(1);
        auto *img  = delegates.value(3);
        QVERIFY(para);
        QVERIFY(img);

        const QPointF paraScene =
            para->mapToScene(QPointF(para->width() / 2, para->height() / 2));
        const QPointF imgScene =
            img->mapToScene(QPointF(img->width() / 2, img->height() / 2));
        const QPoint paraPos = paraScene.toPoint();
        const QPoint imgPos = imgScene.toPoint();

        // Synthesize a click+drag through the HR (row 2) into the image (row 3).
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, paraPos);
        QTest::qWait(20);

        // Move in 4 steps so the MouseArea sees intermediate positions.
        const int steps = 4;
        for (int i = 1; i <= steps; ++i) {
            QPoint mid(paraPos.x() + (imgPos.x() - paraPos.x()) * i / steps,
                       paraPos.y() + (imgPos.y() - paraPos.y()) * i / steps);
            QTest::mouseMove(&view, mid);
            QTest::qWait(20);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, imgPos);
        QTest::qWait(20);

        // The selection model lives under LiveListModelBinding (id: binding).
        // Walk the live-view subtree to find it.
        QObject *binding = nullptr;
        for (QObject *child : root->findChildren<QObject *>()) {
            if (QString::fromLatin1(child->metaObject()->className())
                    .endsWith(QLatin1String("LiveListModelBinding"))) {
                binding = child;
                break;
            }
        }
        QVERIFY(binding);

        QObject *selModel = qvariant_cast<QObject *>(
            binding->property("selectionModel"));
        QVERIFY(selModel);

        QCOMPARE(selModel->property("anchorBlock").toInt(), 1);
        QCOMPARE(selModel->property("activeBlock").toInt(), 3);
        QVERIFY(selModel->property("anchorOffset").toInt() > 0);
    }
```

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
```

Expected: builds clean. If `selectionModel`'s exposed property names differ from `anchorBlock` / `activeBlock` / `anchorOffset`, look at `libs/markoff-view-qml/src/LiveSelectionModel.cpp` and adjust.

- [ ] **Step 3: Run**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: 2 tests pass (`delegates_render_model_text` + `mouse_drag_selects_across_block_kinds`).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-view-qml/tests/tst_view_qml_live_view_qml.cpp
git commit -m "test(view-qml): cross-block mouse drag selection (catches Bugs 2/3/4)"
```

---

### Task 5: Verify the test catches each named regression

Don't assume the test guards what it claims. For each named bug, briefly mutate the source so the bug is reintroduced, run the test, observe failure, then restore. Skip on the bug that requires re-architecting QML (Bug 4 is implicitly checked by Bug 2's mutation).

- [ ] **Step 1: Verify Bug 1 catch — mutate `LiveView.qml`**

In `libs/markoff-view-qml/qml/LiveView.qml`, inside the `DelegateChoice { roleValue: "paragraph" ... }` block, temporarily delete the line `blockText: model.text`. With no explicit binding the property's default `""` wins.

Build:
```bash
cmake --build build-dev --target tst_view_qml_live_view_qml -j 8
```

Run:
```bash
ctest --test-dir build-dev -R '^tst_view_qml_live_view_qml$' --output-on-failure
```

Expected: `delegates_render_model_text` FAILS at the row-1 `blockText` `QCOMPARE` (reads `""` instead of `"Para text."`).

Restore the line, rebuild, rerun — passes.

- [ ] **Step 2: Verify Bug 2 catch — mutate `LiveView.qml`**

In `LiveView.qml`'s ParagraphDelegate `DelegateChoice`, change `blockIndex: index` to `blockIndex: 0`. Build + run.

Expected: `mouse_drag_selects_across_block_kinds` FAILS — `anchorBlock` reads as 0, not 1.

Restore. Rebuild. Rerun — passes.

- [ ] **Step 3: Verify Bug 3 catch — mutate `ParagraphDelegate.qml`**

In `ParagraphDelegate.qml`, temporarily replace the body of `function positionAt(x, y)` with `return 0`. Build + run.

Expected: `mouse_drag_selects_across_block_kinds` FAILS at `QVERIFY(selModel->property("anchorOffset").toInt() > 0)`.

Restore. Rebuild. Rerun — passes.

- [ ] **Step 4: No commit for this task**

These mutations are temporary verifications, not persistent. Confirm `git status` is clean before moving on:

```bash
git status
```

Expected: `nothing to commit, working tree clean`.

---

### Task 6: Full-suite run + verify no regressions

- [ ] **Step 1: Build everything**

```bash
cmake --build build-dev -j 8
```

Expected: success.

- [ ] **Step 2: Run all view-qml tests**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_' --output-on-failure -j 8
```

Expected: 6 tests pass (was 5, +1 new).

- [ ] **Step 3: Run all foundation + view-qml tests (the 33-test baseline)**

```bash
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_)' --output-on-failure -j 8
```

Expected: 34 passed (was 33, +1 new).

- [ ] **Step 4: No further commit**

All commits landed under earlier tasks. `git status` should be clean.

```bash
git status
git log --oneline -8
```

Expected:
- Clean working tree.
- Most-recent commits include "test(view-qml): cross-block mouse drag…", "test(view-qml): assert delegates render model text…", "test(view-qml): scaffold tst_view_qml_live_view_qml", "test(view-qml): tag ListView with objectName…", "spec: live-view QML integration test…".
