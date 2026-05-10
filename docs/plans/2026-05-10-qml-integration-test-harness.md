# QML integration test harness — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `tst_live_render_qml_integration` — a single test executable with five slots that loads production `Main.qml`, drives input through `LiveRealisticInputHarness`, and asserts on buffer/model/delegate state to close the regression class that the mock-based unit tests can't catch.

**Architecture:** Extract an OBJECT library from `libs/markoff-live/app/` so the test target can call `loadFromModule("org.markoff.live.app", "Main")` against the byte-identical production QML. New `QmlIntegrationFixture` owns engine + document + session + MainController and exposes three-layer accessors. Reuse the existing dormant `LiveRealisticInputHarness` for keystrokes; extend with one wheel-event method. Tests run with `QT_QPA_PLATFORM=offscreen`.

**Tech Stack:** C++20, Qt 6.8+ (Core/Gui/Quick/QuickControls2/Qml/Widgets/Test), QQmlApplicationEngine, CMake 3.19+, QTest.

**Spec:** [`docs/specs/2026-05-10-qml-integration-test-harness-design.md`](../specs/2026-05-10-qml-integration-test-harness-design.md) (commit `98d448d`).

**Working directory:** `/home/clinton/dev/Markoff/.worktrees/foundation-exploration/` (branch `exploration/new-foundation`).

**Build commands (always `-j 8`):**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON     # only if missing
cmake --build build-dev --target <target> -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

**Commit convention:** subject `<library>: <description>` (no cluster footer). Trailer:

```
Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Extract `markoff-live-app-internal` OBJECT library

**Files:**
- Modify: `libs/markoff-live/app/CMakeLists.txt`

This is a CMake-only refactor. No source files change. The OBJECT library owns `MainController.{h,cpp}` and the `Main.qml` qml-module registration; the existing `markoff-live-app` executable becomes `main.cpp` + a link against the OBJECT library. The same OBJECT library is consumed by the new test target in Task 4.

- [ ] **Step 1: Read the current file**

The file is 27 lines (you may have already read it). Confirm the current shape:

```cmake
qt_add_executable(markoff-live-app
    main.cpp
    MainController.h
    MainController.cpp
)
qt_add_qml_module(markoff-live-app
    URI org.markoff.live.app
    VERSION 1.0
    QML_FILES Main.qml
)
target_link_libraries(markoff-live-app PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_live markoff_liveplugin markoff_core
)
```

- [ ] **Step 2: Replace the file contents**

Overwrite `libs/markoff-live/app/CMakeLists.txt` with:

```cmake
# OBJECT library: shared between the production executable and the
# tst_live_render_qml_integration test target. Owns MainController and
# the org.markoff.live.app qml module so both consumers reach
# `loadFromModule("org.markoff.live.app", "Main")` against the
# byte-identical Main.qml. See:
#   docs/specs/2026-05-10-qml-integration-test-harness-design.md §3.2.

qt_add_library(markoff-live-app-internal OBJECT
    MainController.h
    MainController.cpp
)

qt_add_qml_module(markoff-live-app-internal
    URI org.markoff.live.app
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(markoff-live-app-internal PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    markoff_live markoff_liveplugin markoff_core
)

target_include_directories(markoff-live-app-internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

qt_add_executable(markoff-live-app
    main.cpp
)

target_link_libraries(markoff-live-app PRIVATE
    markoff-live-app-internal
)

# qt_import_qml_plugins removed: a holdover from pre-Qt-6.5 patterns.
# qt_add_qml_module(STATIC ...) on Qt 6.5+ handles plugin importation
# automatically; the explicit call walked transitive deps and tried
# to resolve Qt6::<plugin> targets in this scope (where they aren't
# imported), producing ~21 spurious "link target does not exist"
# warnings per app build. See docs/2026-05-03-cmake-warnings-rationale.md.
```

- [ ] **Step 3: Reconfigure and build the executable**

Run from the worktree root:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff-live-app -j 8
```

Expected: configure succeeds (no "duplicate qml module" errors); build succeeds and produces `build-dev/bin/markoff-live-app`.

- [ ] **Step 4: Smoke the executable launches**

```bash
printf '# Hello\n\nParagraph.\n' > /tmp/qml-harness-smoke.md
QT_QPA_PLATFORM=offscreen ./build-dev/bin/markoff-live-app /tmp/qml-harness-smoke.md &
APP_PID=$!
sleep 2
kill $APP_PID 2>/dev/null
wait $APP_PID 2>/dev/null
echo "exit: $?"
```

Expected: process runs for ~2 seconds without crashing. Exit on SIGTERM is fine (143 or similar). No "module not found" or "qmldir" errors on stderr.

- [ ] **Step 5: Run the full markoff-live test suite to confirm no regression**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: same green count as before the refactor. (Record the count for comparison after Task 11.)

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/app/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: extract app/CMakeLists.txt OBJECT library

Prep work for tst_live_render_qml_integration: factor MainController
and the org.markoff.live.app qml-module registration out of the
markoff-live-app executable into a shared OBJECT library. The executable
target becomes main.cpp + a link; the new test target (next commit) links
against the same OBJECT library so loadFromModule reaches the same
Main.qml without duplication.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Add `wheelEvent` to LiveRealisticInputHarness

**Files:**
- Modify: `libs/markoff-live/tests/LiveRealisticInputHarness.h`

The harness already has `keyClick`, `typeChar`, `typeString`, `burst`, `idle`. Add one method for wheel-event dispatch (used by test slot #5).

- [ ] **Step 1: Read the current header**

The file is 86 lines. Confirm the includes are `<QCoreApplication> <QQuickWindow> <QTest>`. The class members are `m_window` (`QQuickWindow*`) and `m_defaultGapMs` (`int`).

- [ ] **Step 2: Add the wheelEvent method**

Insert this method into the `LiveRealisticInputHarness` class, immediately after the existing `idle()` method (before `defaultGapMs()`):

```cpp
    /// Dispatch a Ctrl-modifier wheel event for zoom testing.
    ///
    /// QTest has no wheel-event convenience; construct QWheelEvent directly
    /// and send via QCoreApplication. Wheel events on offscreen QPA are less
    /// battle-tested than keys; if this proves flaky see spec §6.3.
    void wheelEvent(QPoint posInWindow,
                    int deltaY,
                    Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QWheelEvent ev(
            /*pos=*/QPointF(posInWindow),
            /*globalPos=*/QPointF(m_window->mapToGlobal(posInWindow)),
            /*pixelDelta=*/QPoint(0, 0),
            /*angleDelta=*/QPoint(0, deltaY),
            /*buttons=*/Qt::NoButton,
            /*modifiers=*/mods,
            /*phase=*/Qt::NoScrollPhase,
            /*inverted=*/false);
        QCoreApplication::sendEvent(m_window, &ev);
        QTest::qWait(m_defaultGapMs);
        QCoreApplication::processEvents();
    }
```

Also add `#include <QWheelEvent>` to the includes block at the top of the file.

- [ ] **Step 3: Verify the harness still compiles**

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: success. (No tests yet consume the harness; this just verifies the header parses.)

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/LiveRealisticInputHarness.h
git commit -m "$(cat <<'EOF'
markoff-live: add wheelEvent to LiveRealisticInputHarness

Adds a single Ctrl+wheel dispatch helper to the dormant harness in prep
for tst_live_render_qml_integration's zoom slot. QTest has no wheel
convenience; construct QWheelEvent and route via QCoreApplication.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Skeleton `QmlIntegrationFixture` + first failing smoke test

**Files:**
- Create: `libs/markoff-live/tests/QmlIntegrationFixture.h`
- Create: `libs/markoff-live/tests/QmlIntegrationFixture.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

Land the test target, the fixture's bones, and a smoke slot that proves the engine loads against production Main.qml. Layer accessors and the rest of the helpers come in Tasks 4–5.

- [ ] **Step 1: Write the failing smoke test**

Create `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QQuickWindow>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

class TestLiveRenderQmlIntegration : public QObject {
    Q_OBJECT

private Q_SLOTS:

    /// Smoke: loads empty doc against production Main.qml, window exposes,
    /// model has exactly one paragraph block (per tst_live_render_empty_doc_focus).
    void loads_production_main_against_empty_doc() {
        QmlIntegrationFixture fix(/*markdown=*/"", /*expectedRowCount=*/1);
        QVERIFY(fix.window() != nullptr);
        QVERIFY(fix.window()->isExposed() || fix.window()->isVisible());
        QVERIFY(fix.model() != nullptr);
        QCOMPARE(fix.model()->rowCount(), 1);
    }
};

QTEST_MAIN(TestLiveRenderQmlIntegration)
#include "tst_live_render_qml_integration.moc"
```

- [ ] **Step 2: Write the fixture header**

Create `libs/markoff-live/tests/QmlIntegrationFixture.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

#include "LiveRealisticInputHarness.h"

class QAbstractItemModel;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;
class QTemporaryFile;
class MainController;

namespace Markoff {
class MarkoffDocument;
class Session;
} // namespace Markoff

namespace Markoff::Live::Test {

/// Loads production Main.qml against a fresh MarkoffDocument and drives
/// it via LiveRealisticInputHarness. Tests use this to assert on three
/// layers (buffer / model / delegate) per the spec §5.1 convention.
class QmlIntegrationFixture {
public:
    /// Loads `markdown` into a fresh MarkoffDocument and brings the
    /// production Main.qml up against it. Blocks until the window is
    /// exposed and the model has `expectedRowCount` rows.
    /// On failure, the constructor uses QVERIFY/QCOMPARE to fail the
    /// enclosing test; tests must construct fixtures at slot scope.
    explicit QmlIntegrationFixture(const QByteArray &markdown,
                                   int expectedRowCount);
    ~QmlIntegrationFixture();

    QmlIntegrationFixture(const QmlIntegrationFixture &) = delete;
    QmlIntegrationFixture &operator=(const QmlIntegrationFixture &) = delete;

    Markoff::MarkoffDocument *document() const { return m_doc.get(); }
    Markoff::Session         *session()  const { return m_session; }
    QQmlApplicationEngine    *engine()   const { return m_engine.get(); }
    QQuickWindow             *window()   const { return m_window; }

    QObject            *binding();
    QAbstractItemModel *model();

    LiveRealisticInputHarness &harness() { return *m_harness; }

private:
    quint16 m_replicaId = 0;
    std::unique_ptr<QTemporaryFile>          m_tmpFile;
    std::unique_ptr<Markoff::MarkoffDocument> m_doc;
    Markoff::Session                         *m_session = nullptr;
    std::unique_ptr<MainController>           m_mainController;
    std::unique_ptr<QQmlApplicationEngine>    m_engine;
    QQuickWindow                             *m_window = nullptr;
    QObject                                  *m_binding = nullptr;
    QAbstractItemModel                       *m_model = nullptr;
    std::unique_ptr<LiveRealisticInputHarness> m_harness;
};

} // namespace Markoff::Live::Test
```

- [ ] **Step 3: Write the fixture implementation**

Create `libs/markoff-live/tests/QmlIntegrationFixture.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "QmlIntegrationFixture.h"

#include <QAbstractItemModel>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>

#include "MainController.h"  // from markoff-live-app-internal OBJECT lib

namespace Markoff::Live::Test {

QmlIntegrationFixture::QmlIntegrationFixture(const QByteArray &markdown,
                                             int expectedRowCount)
{
    m_replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);

    m_tmpFile = std::make_unique<QTemporaryFile>();
    QVERIFY2(m_tmpFile->open(), "QTemporaryFile open failed");

    m_doc = std::make_unique<Markoff::MarkoffDocument>(m_replicaId);
    m_doc->loadFromMarkdown(markdown);
    m_doc->markSaved(m_doc->d2EditSequence());

    m_session = m_doc->createSession();

    m_mainController = std::make_unique<MainController>(
        m_doc.get(), m_tmpFile->fileName());

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ctxDocument"), m_doc.get());
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ctxSession"), m_session);
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ctxMain"), m_mainController.get());

    m_engine->loadFromModule("org.markoff.live.app", "Main");
    QVERIFY2(!m_engine->rootObjects().isEmpty(),
             "loadFromModule produced no root object — check qml module URI");

    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    QVERIFY2(m_window != nullptr, "root object is not a QQuickWindow");

    QVERIFY2(QTest::qWaitForWindowExposed(m_window, 5000),
             "window did not expose within 5s under offscreen QPA");

    // Resolve LiveListModelBinding via root context (set up by Main.qml's
    // `LiveListModelBinding { id: modelBinding }`).
    m_binding = m_window->findChild<QObject *>(QString(), Qt::FindChildrenRecursively);
    // Walk children looking for an object with `model` and `fontScale` properties
    // (matches LiveListModelBinding). Defer the exact lookup to Task 5; for the
    // smoke test we only need rowCount > 0 reachable via the model property.
    for (QObject *child : m_window->findChildren<QObject *>()) {
        const QMetaObject *mo = child->metaObject();
        if (mo->indexOfProperty("fontScale") != -1
            && mo->indexOfProperty("model") != -1
            && mo->indexOfProperty("document") != -1) {
            m_binding = child;
            break;
        }
    }
    QVERIFY2(m_binding != nullptr, "LiveListModelBinding not found in QML tree");

    m_model = qobject_cast<QAbstractItemModel *>(
        m_binding->property("model").value<QObject *>());
    QVERIFY2(m_model != nullptr, "binding.model is not a QAbstractItemModel");

    // Wait for the expected row count (load may have parsed async).
    if (m_model->rowCount() != expectedRowCount) {
        QSignalSpy spy(m_model, &QAbstractItemModel::rowsInserted);
        const int deadline = 2000;
        QElapsedTimer t; t.start();
        while (m_model->rowCount() != expectedRowCount && t.elapsed() < deadline) {
            spy.wait(100);
            QCoreApplication::processEvents();
        }
    }
    QCOMPARE(m_model->rowCount(), expectedRowCount);

    m_harness = std::make_unique<LiveRealisticInputHarness>(m_window);
}

QmlIntegrationFixture::~QmlIntegrationFixture() = default;

QObject *QmlIntegrationFixture::binding()           { return m_binding; }
QAbstractItemModel *QmlIntegrationFixture::model()  { return m_model; }

} // namespace Markoff::Live::Test
```

Note: the `QElapsedTimer` requires `#include <QElapsedTimer>`. Add it to the includes block.

- [ ] **Step 4: Register the test target**

Append this to `libs/markoff-live/tests/CMakeLists.txt`:

```cmake
# Queue #3: QML integration harness. Loads production Main.qml via the
# markoff-live-app-internal OBJECT library; runs offscreen.
qt_add_executable(tst_live_render_qml_integration
    tst_live_render_qml_integration.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_qml_integration PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal)
add_test(NAME tst_live_render_qml_integration
         COMMAND tst_live_render_qml_integration)
set_tests_properties(tst_live_render_qml_integration
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: Build the test target**

```bash
cmake -S . -B build-dev
cmake --build build-dev --target tst_live_render_qml_integration -j 8
```

Expected: clean build. If you see "MainController.h not found", the OBJECT library's `target_include_directories(... PUBLIC ...)` from Task 1 didn't take effect — verify Task 1 Step 2 was applied correctly.

- [ ] **Step 6: Run the smoke test**

```bash
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: `Passed`. If `qWaitForWindowExposed` times out, see spec §6.1 fallbacks — try `QTRY_VERIFY(window->isVisible() && window->width() > 0, 5000)` instead. Record any fallback used in the commit message.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/tests/QmlIntegrationFixture.h \
        libs/markoff-live/tests/QmlIntegrationFixture.cpp \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: QmlIntegrationFixture skeleton + smoke test

New test target tst_live_render_qml_integration. The fixture loads
production Main.qml via the markoff-live-app-internal OBJECT library,
wires the three context properties, waits for window exposure and
expected row count. Smoke slot: empty doc → 1 paragraph block.

Three-layer accessors and the four real regression slots come next.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Add three-layer state accessors to the fixture

**Files:**
- Modify: `libs/markoff-live/tests/QmlIntegrationFixture.h`
- Modify: `libs/markoff-live/tests/QmlIntegrationFixture.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Add `bufferText` / `modelText` / `delegateText` / `delegateCursorPos` plus the helper they share (`delegateAt` walks the realised ListView). Add a failing test that exercises the assertion convention.

- [ ] **Step 1: Write the failing test**

Append this slot to the `TestLiveRenderQmlIntegration` class (above `QTEST_MAIN`):

```cpp
    /// Three-layer convention smoke: after load, all three layers agree on
    /// the empty-paragraph text. No edits driven; this guards the accessors.
    void three_layer_accessors_agree_after_load() {
        QmlIntegrationFixture fix(/*markdown=*/"", /*expectedRowCount=*/1);

        const auto blockIds = fix.document()->iterateBlocks();
        QCOMPARE(blockIds.size(), 1u);

        QCOMPARE(fix.bufferText(blockIds[0]), QByteArray(""));
        QCOMPARE(fix.modelText(0),            QString(""));
        QCOMPARE(fix.delegateText(0),         QString(""));
        QCOMPARE(fix.delegateCursorPos(0),    0);
    }
```

- [ ] **Step 2: Run to confirm it fails to compile**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
```

Expected: compile error on `bufferText` / `modelText` / `delegateText` / `delegateCursorPos` (undeclared).

- [ ] **Step 3: Add accessor declarations to the fixture header**

Insert into `QmlIntegrationFixture.h`, in the public section after `model()`:

```cpp
    // Resolved QML objects (cached on first access)
    QQuickItem *listView();
    QQuickItem *delegateAt(int row);
    QQuickItem *delegateTextEdit(int row); // recursive find of TextEdit child

    // Three-layer state (per spec §5.1)
    QByteArray bufferText(Markoff::BlockId);
    QString    modelText(int row);
    QString    delegateText(int row);
    int        delegateCursorPos(int row);
```

Also add `#include <markoff/core/CrdtProxies.h>` to the top of the header for `Markoff::BlockId`.

Add a cached `QQuickItem *m_listView = nullptr;` to the private members.

- [ ] **Step 4: Implement the accessors**

Append to `QmlIntegrationFixture.cpp`:

```cpp
QQuickItem *QmlIntegrationFixture::listView() {
    if (m_listView)
        return m_listView;
    // LiveView (the ListView from Main.qml) is the binding's sibling under
    // ApplicationWindow's contentItem. Walk visible children for one whose
    // metaObject is named "QQuickListView".
    for (QObject *child : m_window->findChildren<QObject *>()) {
        auto *item = qobject_cast<QQuickItem *>(child);
        if (!item) continue;
        if (qstrcmp(item->metaObject()->className(), "QQuickListView") == 0) {
            m_listView = item;
            break;
        }
    }
    Q_ASSERT(m_listView != nullptr);
    return m_listView;
}

QQuickItem *QmlIntegrationFixture::delegateAt(int row) {
    QQuickItem *lv = listView();
    if (!lv) return nullptr;
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(lv, "itemAtIndex", Qt::DirectConnection,
                              Q_RETURN_ARG(QQuickItem *, item),
                              Q_ARG(int, row));
    return item;
}

// Recursive descent: find the first QQuickTextEdit-typed descendant.
static QQuickItem *findTextEditDescendant(QQuickItem *root) {
    if (!root) return nullptr;
    if (qstrcmp(root->metaObject()->className(), "QQuickTextEdit") == 0)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (auto *found = findTextEditDescendant(child))
            return found;
    }
    return nullptr;
}

QQuickItem *QmlIntegrationFixture::delegateTextEdit(int row) {
    QQuickItem *d = delegateAt(row);
    return d ? findTextEditDescendant(d) : nullptr;
}

QByteArray QmlIntegrationFixture::bufferText(Markoff::BlockId id) {
    return m_doc->blockText(id);
}

QString QmlIntegrationFixture::modelText(int row) {
    // role 258 = TextRole per LiveBlockModel::roleNames() (KindRole=257,
    // TextRole=258 in the existing enum). Look up by role name to avoid
    // hardcoded numeric drift.
    const auto roles = m_model->roleNames();
    int textRole = -1;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == QByteArray("text")) {
            textRole = it.key();
            break;
        }
    }
    Q_ASSERT(textRole != -1);
    return m_model->data(m_model->index(row, 0), textRole).toString();
}

QString QmlIntegrationFixture::delegateText(int row) {
    QQuickItem *te = delegateTextEdit(row);
    return te ? te->property("text").toString() : QString();
}

int QmlIntegrationFixture::delegateCursorPos(int row) {
    QQuickItem *te = delegateTextEdit(row);
    return te ? te->property("cursorPosition").toInt() : -1;
}
```

- [ ] **Step 5: Build**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
```

Expected: clean build.

- [ ] **Step 6: Run the test**

```bash
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: both slots pass. If `delegateTextEdit(0)` returns null, the delegate hasn't been realised yet — call the new `waitForDelegateAt` in Task 5 first. For the empty-doc case the first delegate should be present once the window is exposed, but on flake replay add a `QTRY_VERIFY(fix.delegateTextEdit(0) != nullptr, 2000)` to the test before the accessor calls.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/tests/QmlIntegrationFixture.h \
        libs/markoff-live/tests/QmlIntegrationFixture.cpp \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: three-layer accessors on QmlIntegrationFixture

bufferText (doc->blockText), modelText (model.data via role-name lookup),
delegateText + delegateCursorPos (recursive QQuickTextEdit descent
through the delegate hierarchy). Per spec §5.1 the three layers are
asserted in tandem so failures pinpoint which pipeline link broke.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Add wait helpers + focus accessor

**Files:**
- Modify: `libs/markoff-live/tests/QmlIntegrationFixture.h`
- Modify: `libs/markoff-live/tests/QmlIntegrationFixture.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Add `waitForRowCount`, `waitForDelegateAt`, and `focusedDelegate`. The slot tests in Tasks 6–10 need these.

- [ ] **Step 1: Add a failing test exercising the wait helpers**

Append to the test class:

```cpp
    /// Wait helpers smoke: loading a two-block doc, the second delegate
    /// becomes realised within timeout; focusedDelegate is non-null.
    void wait_helpers_resolve_two_block_doc() {
        QmlIntegrationFixture fix(/*markdown=*/"A\n\nB",
                                  /*expectedRowCount=*/2);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));
        // At least one delegate has focus (the production ListView focus
        // policy auto-focuses the first row on load).
        QVERIFY(fix.focusedDelegate() != nullptr);
    }
```

- [ ] **Step 2: Confirm it fails to compile**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
```

Expected: compile error on `waitForDelegateAt` and `focusedDelegate`.

- [ ] **Step 3: Add declarations**

Insert into `QmlIntegrationFixture.h` public section:

```cpp
    // Wait helpers (return false on timeout; tests should QVERIFY).
    bool waitForRowCount(int expected, int timeoutMs = 2000);
    bool waitForDelegateAt(int row, int timeoutMs = 2000);

    /// Returns the delegate (or null) that currently has activeFocus —
    /// either it or one of its children. Used by Tasks 8+ to assert
    /// focus migration after Enter / arrow keys.
    QQuickItem *focusedDelegate();
```

- [ ] **Step 4: Implement**

Append to `QmlIntegrationFixture.cpp`:

```cpp
bool QmlIntegrationFixture::waitForRowCount(int expected, int timeoutMs) {
    if (m_model->rowCount() == expected)
        return true;
    QSignalSpy insSpy(m_model, &QAbstractItemModel::rowsInserted);
    QSignalSpy rmSpy(m_model, &QAbstractItemModel::rowsRemoved);
    QElapsedTimer t; t.start();
    while (m_model->rowCount() != expected && t.elapsed() < timeoutMs) {
        insSpy.wait(100);
        rmSpy.wait(50);
        QCoreApplication::processEvents();
    }
    return m_model->rowCount() == expected;
}

bool QmlIntegrationFixture::waitForDelegateAt(int row, int timeoutMs) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs) {
        if (delegateAt(row) != nullptr)
            return true;
        QTest::qWait(25);
        QCoreApplication::processEvents();
    }
    return delegateAt(row) != nullptr;
}

QQuickItem *QmlIntegrationFixture::focusedDelegate() {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        QQuickItem *d = delegateAt(row);
        if (!d) continue;
        // The delegate "has focus" if itself or any descendant TextEdit is the
        // active focus item.
        if (d->hasActiveFocus())
            return d;
        QQuickItem *te = findTextEditDescendant(d);
        if (te && te->hasActiveFocus())
            return d;
    }
    return nullptr;
}
```

Note: `findTextEditDescendant` was defined as a static in `QmlIntegrationFixture.cpp` in Task 4 — leave it as static; both `delegateTextEdit` and `focusedDelegate` use it from within the same translation unit.

- [ ] **Step 5: Build and test**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: all three slots pass. If `focusedDelegate()` returns null on the two-block load, the production ListView may not auto-focus row 0 on a multi-row load — confirm by reading `LiveView.qml`'s focus property and adjusting the assertion to `QTRY_VERIFY(fix.focusedDelegate() != nullptr, 2000)` (give it more time to settle).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/tests/QmlIntegrationFixture.h \
        libs/markoff-live/tests/QmlIntegrationFixture.cpp \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: wait helpers + focusedDelegate on QmlIntegrationFixture

waitForRowCount (QSignalSpy on rowsInserted/rowsRemoved),
waitForDelegateAt (poll itemAtIndex), focusedDelegate (walks rows
looking for the active-focus delegate or TextEdit descendant). Lands
the API surface tasks 6-10 will consume.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Slot — typing preserves insertion order

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

The first real regression slot. Production failure mode was "abc" → "cba" in the delegate while buffer held "abc". With the three-layer convention, this test catches both directions of the inconsistency.

- [ ] **Step 1: Write the failing slot**

Append to the test class:

```cpp
    /// Typing-reverses-chars regression killer. Type "abc" into an
    /// auto-focused empty paragraph; all three layers must agree
    /// on "abc" with cursor at position 3.
    void typing_preserves_insertion_order() {
        QmlIntegrationFixture fix(/*markdown=*/"", /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        // Production ListView's `focus: true` and Main.qml's first-row
        // auto-focus convention should give us focus immediately — but
        // give the event loop a beat to settle so the harness types into
        // the realised TextEdit, not the bare window.
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.harness().typeString("abc");

        const auto blockIds = fix.document()->iterateBlocks();
        QCOMPARE(blockIds.size(), 1u);

        QCOMPARE(fix.bufferText(blockIds[0]), QByteArray("abc"));
        QCOMPARE(fix.modelText(0),            QString("abc"));
        QCOMPARE(fix.delegateText(0),         QString("abc"));
        QCOMPARE(fix.delegateCursorPos(0),    3);
    }
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: `Passed`. If buffer == "abc" but delegate != "abc", the harness types but the TextEdit isn't receiving — verify focused delegate is the empty paragraph's TextEdit by adding `qDebug() << fix.focusedDelegate()->metaObject()->className()` temporarily.

If delegate == "abc" but buffer != "abc", the contentsChange → `d2ApplyBufferEdit` path is broken (production bug — escalate, don't paper over).

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — typing preserves insertion order

Three-layer regression guard: type "abc" into auto-focused empty
paragraph, assert buffer/model/delegate all show "abc" with
cursorPosition 3. This is the typing-reverses-chars regression
killer (queue.md #3 priority 1).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Slot — Shift+Enter creates visible newline

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Loads "Heading", End-positions cursor, sends Shift+Enter, asserts the buffer holds "Heading\n" and the delegate shows two visible lines. Connects to queue.md #4 (the chop-`\n` investigation).

- [ ] **Step 1: Write the failing slot**

Append to the test class:

```cpp
    /// Shift+Enter inserts a soft break into the paragraph buffer; the
    /// delegate's TextEdit must visibly render two lines.
    void shift_enter_creates_visible_newline() {
        QmlIntegrationFixture fix(/*markdown=*/"Heading",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // End-position the cursor inside the focused TextEdit.
        fix.harness().keyClick(Qt::Key_End);

        // Sanity: cursor at end of "Heading" (qtPos 7).
        QCOMPARE(fix.delegateCursorPos(0), 7);

        // Drive Shift+Enter.
        fix.harness().keyClick(Qt::Key_Return, Qt::ShiftModifier);

        const auto blockIds = fix.document()->iterateBlocks();
        QCOMPARE(blockIds.size(), 1u);

        QCOMPARE(fix.bufferText(blockIds[0]), QByteArray("Heading\n"));

        // Delegate must show the newline visibly. The chop in
        // LiveListModelBinding::onD2Changed:311-312 (see queue.md #4)
        // may strip it from model.text — if this fails on delegateText
        // contains '\n', that is exactly queue #4's bug; do not paper
        // over, surface it.
        const QString dt = fix.delegateText(0);
        QVERIFY2(dt.contains(QLatin1Char('\n')),
                 qPrintable(QString("delegate text missing \\n: %1").arg(dt)));

        QCOMPARE(fix.delegateCursorPos(0), 8);
    }
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: `Passed` OR a clear failure on `delegateText contains '\n'` (which is queue #4's bug surfacing — see Step 3 for handling).

- [ ] **Step 3: Handle either outcome**

If passed: great. Commit and proceed.

If failed on the `\n`-contains assertion: this is queue.md #4 in the flesh. Two options:
- **Option A (preferred):** leave the test as-is. It correctly fails; this is the regression guard that should drive #4's fix. Annotate the slot with `QEXPECT_FAIL("", "queue.md #4 chop-\\n bug — fix lands in a follow-up plan", Continue);` placed immediately before the failing `QVERIFY2`. Commit with a message that mentions #4.
- **Option B:** skip the slot with `QSKIP("queue.md #4")` and file a separate PR for #4 first. Only choose B if Option A's expected-fail breaks ctest reporting.

If failed on a different assertion (cursorPos != 8, or buffer != "Heading\n"): that's a production bug unrelated to #4. Escalate; do not paper over.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — Shift+Enter creates visible newline

Loads "Heading", End-positions cursor, types Shift+Enter. Asserts
buffer holds "Heading\n", delegate text contains '\n', cursor at
position 8. Connects to queue.md #4 (chop-\n investigation): if the
delegate-text \n assertion fails, that is the bug — guard rather
than paper over.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Slot — Enter at paragraph-end migrates focus

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Connects to queue.md #2 concern #7 (cursor lost on Enter) — `Component.onCompleted` match check captured at construction time may miss late-arriving structural signals.

- [ ] **Step 1: Write the failing slot**

Append:

```cpp
    /// Enter at paragraph-end creates a new block and migrates focus
    /// to it. The "cursor lost on Enter" regression class (queue.md #2
    /// concern #7) lives here.
    void enter_at_paragraph_end_migrates_focus() {
        QmlIntegrationFixture fix(/*markdown=*/"A", /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.harness().keyClick(Qt::Key_End);
        QCOMPARE(fix.delegateCursorPos(0), 1);

        fix.harness().keyClick(Qt::Key_Return);

        QVERIFY(fix.waitForRowCount(2, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        // Focus must end up on row 1 (the new block). Give the focus
        // delivery up to 2s — structuralRowsInserted resolves via the
        // pending-cursor-request slot in LiveCursorState; the delegate
        // forces active focus on cursorChanged.
        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(1), 2000);
        QCOMPARE(fix.delegateCursorPos(1), 0);
    }
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: `Passed`. If `focusedDelegate` is still row 0 after Enter, this exposes queue.md #2 concern #7/#8 in production — surface loudly via `qDebug() << "focused row:" << <index>` and escalate. Do not relax the assertion.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — Enter at paragraph-end migrates focus

Loads "A", Enter at end, asserts rowCount → 2 and focus migrates from
row 0 to row 1 with cursor at position 0. The "cursor lost on Enter"
regression class from queue.md #2 concern #7 is guarded here.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Slot — arrow-up walks then crosses blocks

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Test the cross-block arrow-up path. Within-block wrapped-line behaviour is already covered by `tst_live_render_e2_nav_arrows` against `MockTextEdit`; this slot covers the production-realistic path.

- [ ] **Step 1: Write the failing slot**

Append:

```cpp
    /// Two single-line paragraphs. Cursor at end of row 1; arrow-up
    /// crosses to row 0 (no within-block wrapping to walk).
    void arrow_up_walks_then_crosses_blocks() {
        QmlIntegrationFixture fix(
            /*markdown=*/"first paragraph\n\nsecond paragraph",
            /*expectedRowCount=*/2);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        // Click row 1 to focus it. The harness keyClick targets the window,
        // which routes through QML focus — but we need row 1 specifically.
        // Use the binding's cursor API: requestTextCaretAtRow exists on
        // LiveCursorState and is exposed Q_INVOKABLE.
        QObject *cursorState = fix.binding()->property("cursorState")
                                              .value<QObject *>();
        QVERIFY(cursorState != nullptr);
        QMetaObject::invokeMethod(cursorState, "requestTextCaretAtRow",
                                  Qt::DirectConnection,
                                  Q_ARG(int, 1),
                                  Q_ARG(int, 16)); // end of "second paragraph"

        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(1), 2000);
        QCOMPARE(fix.delegateCursorPos(1), 16);

        fix.harness().keyClick(Qt::Key_Up);

        // For a single-line block, arrow-up crosses to the previous block.
        // Column preservation lands cursor on row 0 at col=15 (length of
        // "first paragraph") because col=16 is past the end — Qt's
        // QTextLayout clamps to the closest visual position.
        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(0), 2000);
        // qtPos is preserved as much as the target line allows;
        // assert it landed at or before line end.
        QVERIFY(fix.delegateCursorPos(0) <= 15);
    }
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: `Passed`. If `requestTextCaretAtRow` invocation fails ("no such method"), the method may be private or the Q_INVOKABLE annotation may not be present on the API surface — read `libs/markoff-live/include/markoff/live/LiveCursorState.h` and adapt the invoke call. The fallback path is `QTest::mouseClick(fix.window(), Qt::LeftButton, ...)` at the delegate's center.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — arrow-up walks then crosses blocks

Two single-line paragraphs, cursor at end of row 1, arrow-up crosses
to row 0. Focus and cursor position assertions. Covers the
production-realistic cross-block arrow path that the existing
MockTextEdit-based nav test cannot exercise.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Slot — Ctrl+wheel zooms font scale

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

Best-effort per spec §6.3. If wheel-event dispatch on offscreen doesn't work reliably, the fallback is `QSKIP` with a follow-up queue item — the four key-driven slots still land.

- [ ] **Step 1: Write the failing slot**

Append:

```cpp
    /// Ctrl+wheel increases LiveListModelBinding.fontScale; first delegate's
    /// TextEdit follows via the `font.pixelSize = theme.pixelSizeFor(slot)
    /// * fontScale` binding in ParagraphDelegate.qml.
    void ctrl_wheel_zooms_font_scale() {
        QmlIntegrationFixture fix(/*markdown=*/"sample",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        const qreal scaleBefore = fix.binding()->property("fontScale").toReal();
        QVERIFY(scaleBefore > 0.0);

        QQuickItem *te = fix.delegateTextEdit(0);
        QVERIFY(te != nullptr);
        const int pixelSizeBefore = te->property("font").value<QFont>().pixelSize();

        fix.harness().wheelEvent(QPoint(100, 100), /*deltaY=*/120,
                                 Qt::ControlModifier);

        const qreal scaleAfter = fix.binding()->property("fontScale").toReal();

        // If Ctrl+wheel isn't wired in LiveView.qml (queue #1 added the
        // QActions but Ctrl+wheel routing is separate), scale won't change.
        // Per spec §6.3 + risk 6.3, skip with a follow-up queue item.
        if (qFuzzyCompare(scaleAfter, scaleBefore)) {
            QSKIP("Ctrl+wheel not wired; track as follow-up to queue #1. "
                  "Open a new queue item or fold into E2.6 polish.");
        }

        QVERIFY2(scaleAfter > scaleBefore,
                 qPrintable(QString("expected scale increase: before=%1 after=%2")
                            .arg(scaleBefore).arg(scaleAfter)));

        const int pixelSizeAfter = te->property("font").value<QFont>().pixelSize();
        QVERIFY2(pixelSizeAfter > pixelSizeBefore,
                 qPrintable(QString("expected pixelSize increase: before=%1 after=%2")
                            .arg(pixelSizeBefore).arg(pixelSizeAfter)));
    }
```

Also add `#include <QFont>` near the top of the test file if it's not already pulled transitively.

- [ ] **Step 2: Build and run**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
ctest --test-dir build-dev -R '^tst_live_render_qml_integration$' --output-on-failure
```

Expected: `Passed` or `Skipped` with the queue follow-up message. Either is acceptable per spec §6.3.

- [ ] **Step 3: If skipped, file the follow-up**

If `QSKIP` fired, append to `docs/queue.md` (do not block the commit on this — just add the follow-up item):

```markdown
## #N — Wire Ctrl+wheel zoom in LiveView.qml

**Effort:** ~1 day. **Status:** surfaced by tst_live_render_qml_integration's
`ctrl_wheel_zooms_font_scale` slot. E2.6 (queue #1) added the QAction-driven
zoom path; this item wires the wheel-input route through to the same
LiveActionController action.
```

Insert at appropriate priority (it's small; tail of the active items is fine).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp docs/queue.md
git commit -m "$(cat <<'EOF'
markoff-live: slot — Ctrl+wheel zooms font scale

Best-effort wheel-event slot per spec §6.3. Increases fontScale via
Ctrl+wheel and asserts the delegate's TextEdit pixelSize follows.
Skipped with a follow-up queue item if Ctrl+wheel is not yet wired
in LiveView.qml (E2.6 only landed the QAction path).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

(If no queue.md change, drop it from the `git add`.)

---

## Task 11: Closeout — full ctest, smoke app, queue.md, activity log

**Files:**
- Modify: `docs/queue.md` (queue #3 closure banner + table edit)

Run the full test suite, smoke the production app, mark queue.md #3 as closed, append an activity-log entry.

- [ ] **Step 1: Full ctest pass**

```bash
ctest --test-dir build-dev -j 8 --output-on-failure
```

Expected: all green, count = (count before Task 1) + 6 slots in `tst_live_render_qml_integration`. Record any newly-failing tests; investigate before proceeding (they're either Task 1's CMake refactor catching something or genuine flakes — distinguish before committing).

- [ ] **Step 2: Smoke production app**

```bash
printf '# Hello\n\nParagraph.\n\n- item\n' > /tmp/qml-harness-closeout.md
QT_QPA_PLATFORM=offscreen timeout 3 ./build-dev/bin/markoff-live-app /tmp/qml-harness-closeout.md
echo "exit: $?"
```

Expected: process runs ~3 seconds and `timeout` returns 124. No crash; no "module not found" or "qmldir" errors on stderr.

- [ ] **Step 3: Update queue.md**

Replace the body of `## #3 — QML integration-test harness` in `docs/queue.md` with:

```markdown
## #3 — QML integration-test harness ✅ IMPLEMENTED 2026-05-10

**Effort:** ~1 day. **Status:** implemented in
`tst_live_render_qml_integration` (commit chain ending Task 11).

Six slots cover the queue-listed regression class. The harness loads
production Main.qml via the markoff-live-app-internal OBJECT library
and drives input through LiveRealisticInputHarness (now wired up for
the first time since it was authored). Three-layer assertion
convention enforced: every edit asserts on buffer/model/delegate so
failures pinpoint the broken pipeline link.

Follow-ups (if any) tracked as separate queue items.
```

Also add an entry to the top of the file's banner block, mirroring the
2026-05-10 banners that exist:

```markdown
> **2026-05-10 — Item #3 implemented.** Spec
> `docs/specs/2026-05-10-qml-integration-test-harness-design.md`; plan
> `docs/plans/2026-05-10-qml-integration-test-harness.md`. New target
> `tst_live_render_qml_integration` runs offscreen; six slots green
> (or five + one skipped if Ctrl+wheel isn't wired). Full ctest still
> green.
```

- [ ] **Step 4: Final commit**

```bash
git add docs/queue.md
git commit -m "$(cat <<'EOF'
docs: close queue #3 — QML integration test harness implemented

tst_live_render_qml_integration ships with 6 slots covering the
typing-class regressions called out in queue #3. The OBJECT-library
refactor lets the harness reach production Main.qml byte-identically.
Three-layer assertion convention (buffer/model/delegate) catches the
discrepancy class the existing mock-based unit tests miss.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify the tag stays at v0.7.0-e2.6**

```bash
git tag --points-at HEAD
git log --oneline -15
```

Expected: no tag at HEAD (the work landed after `v0.7.0-e2.6`). The held tag is still on the pre-Task-1 SHA. Closeout complete.

---

## Self-review

**1. Spec coverage:**

- Spec §2 in-scope items: Task 1 (CMake refactor), Task 2 (harness ext), Tasks 3–5 (fixture), Tasks 6–10 (5 slots), Task 11 (closeout) — all covered.
- Spec §3.1 single executable / 5 slots: covered Tasks 6–10 (plus 2 fixture smoke slots from Tasks 3 + 5 → 7 slots total; spec allowance was "five+"); single executable per Task 3 Step 4.
- Spec §3.2 OBJECT library: Task 1.
- Spec §3.3 fixture surface: Tasks 3, 4, 5. All public members from spec §3.3 declared.
- Spec §3.4 wheelEvent: Task 2.
- Spec §4 five scenarios: Tasks 6 → 4.1, Task 7 → 4.2, Task 8 → 4.3, Task 9 → 4.4, Task 10 → 4.5.
- Spec §5.1 three-layer assertion convention: enforced in every edit-driving slot (Tasks 6, 7, 8) via buffer + model + delegate triples.
- Spec §5.2 naming: `lowercase_with_underscores` slot names, `tst_live_render_qml_integration` target.
- Spec §5.3 SPDX, namespace, prefix: enforced in every Create step.
- Spec §6 risks: §6.1 (ApplicationWindow on offscreen) — fallback noted in Task 3 Step 6; §6.2 (delegate object name) — fixture uses recursive `findTextEditDescendant` (Task 4); §6.3 (wheel offscreen) — `QSKIP` path in Task 10; §6.4 (app refactor) — Task 1 Step 4 smoke + Task 11 Step 2 smoke; §6.5 (per-slot startup cost) — only measured if needed, no preemptive change.
- Spec §7 acceptance criteria: covered by Task 11.
- Spec §8 file manifest: matches the Files: blocks across the tasks.
- Spec §9 decisions: reflected in the plan structure (one exe, OBJECT lib, three layers, no gate, harness reuse, wheel best-effort).

**2. Placeholder scan:** No "TBD", "TODO", or "fill in" left in the plan. Every code step shows complete code. Every command step shows exact commands and expected output.

**3. Type consistency:**
- `QmlIntegrationFixture` API: same signatures in declaration (Task 3) and uses (Tasks 4–10). `bufferText(Markoff::BlockId)`, `modelText(int)`, `delegateText(int)`, `delegateCursorPos(int)`, `delegateAt(int)`, `delegateTextEdit(int)`, `listView()`, `waitForRowCount(int, int)`, `waitForDelegateAt(int, int)`, `focusedDelegate()` — all consistent.
- `Markoff::BlockId` — used consistently; pulled via `#include <markoff/core/CrdtProxies.h>` in Task 4 Step 3.
- `MainController` — non-namespaced (matches `app/MainController.h`).
- `LiveRealisticInputHarness::wheelEvent(QPoint, int, Qt::KeyboardModifiers)` — declared in Task 2, called in Task 10. Signatures match.
- `findTextEditDescendant` — defined as file-static in Task 4 Step 4; reused in Task 5 Step 4 (same TU). Consistent.

No fixes needed.
