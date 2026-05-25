> **Status: completed.** `libs/markoff-source/` and `SourceTextDocumentBinding` foundation relocation are in tree (commit range `89bc241`…`5915520`). Do not execute.

# `markoff-source-widget` — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a from-scratch QtWidgets Source view library on `markoff-foundation`, with a `QPlainTextEdit`-subclass editor, a line-number gutter, a find bar, and full document/session round-trip via the foundation's CRDT.

**Architecture:** New library `libs/markoff-source`. Editor is a `QPlainTextEdit` subclass; the gutter is a private `QWidget` child of the viewport (Qt CodeEditor pattern); FindBar is a separate widget the host places. Document binding (`SourceTextDocumentBinding`) relocates from `markoff-view-qml` down into `markoff-foundation` so the new widget and the existing QML SourceEditor share one implementation.

**Tech Stack:** Qt 6.8+, KF6::SyntaxHighlighting, `markoff-foundation` (CRDT + parser + theme + binding). C++20, CMake 3.19+.

**Spec:** `docs/specs/2026-04-29-source-widget-design.md`

---

## Phase A — Foundation relocation

### Task A1: Relocate `SourceTextDocumentBinding` to `markoff-foundation`

**Files:**
- Move (with rename of property): `libs/markoff-view-qml/src/SourceTextDocumentBinding.{h,cpp}` → `libs/markoff-core/include/markoff-foundation/SourceTextDocumentBinding.h` + `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-view-qml/CMakeLists.txt`
- Modify: `libs/markoff-view-qml/qml/SourceEditor.qml`

- [ ] **Step 1: Read both files**

Read the existing `libs/markoff-view-qml/src/SourceTextDocumentBinding.h` and `.cpp` to understand the public API and the `qtQuickDocument` Q_PROPERTY.

- [ ] **Step 2: Create the foundation-side header**

Move the header to `libs/markoff-core/include/markoff-foundation/SourceTextDocumentBinding.h`. Apply the rename: every reference to `qtQuickDocument` becomes `textDocument`, and the property's type changes from the QML-flavoured `QObject *` (or `QQuickTextDocument *`) to `QTextDocument *`. Update include guards. Replace any `MARKOFF_VIEW_QML_EXPORT` (or local export macro) with `MARKOFF_FOUNDATION_EXPORT`. Replace the namespace if needed (the class moves from `Markoff::View::Qml::SourceTextDocumentBinding` to `Markoff::SourceTextDocumentBinding` — fewer nested namespaces in foundation).

- [ ] **Step 3: Move the implementation**

Move the `.cpp` to `libs/markoff-core/src/SourceTextDocumentBinding.cpp`. Update the namespace, the include, and any `qtQuickDocument` references inside the implementation to `textDocument`. The internals (cycle guards `m_applyingLocalEdit` / `m_applyingRemoteEdit`, UTF-16/UTF-8 conversion helpers) are unchanged.

If the implementation had a code path extracting the underlying `QTextDocument` from a `QQuickTextDocument` (e.g. via `qquickTextDoc->textDocument()`), that path is removed; the binding now accepts a `QTextDocument *` directly.

- [ ] **Step 4: Add to foundation's CMakeLists**

In `libs/markoff-core/CMakeLists.txt`, add `SourceTextDocumentBinding.cpp` to the library's source list and the public header to the install set. The library already links Qt6::Core + Qt6::Gui (QTextDocument lives in QtGui).

- [ ] **Step 5: Remove from markoff-view-qml**

Delete `libs/markoff-view-qml/src/SourceTextDocumentBinding.{h,cpp}`. In `libs/markoff-view-qml/CMakeLists.txt`, remove the file from the source list.

- [ ] **Step 6: Update `markoff-view-qml`'s SourceEditor.qml**

In `libs/markoff-view-qml/qml/SourceEditor.qml`, find the existing line that sets the binding's `qtQuickDocument` (something like `qtQuickDocument: textArea.textDocument`) and change it to:

```qml
textDocument: textArea.textDocument.textDocument
```

The Qt 6 chain: `TextArea.textDocument` returns a `QQuickTextDocument`; that has a `textDocument()` accessor returning the underlying `QTextDocument *`. The QML reads it as `.textDocument` (auto-property).

- [ ] **Step 7: Update other markoff-view-qml use sites**

Search `libs/markoff-view-qml/src/` and `libs/markoff-view-qml/include/` for `Markoff::View::Qml::SourceTextDocumentBinding` or `<SourceTextDocumentBinding.h>` includes. Replace with `<markoff-foundation/SourceTextDocumentBinding.h>` and `Markoff::SourceTextDocumentBinding`. The QML registration (look in the QML plugin / CMake `qt_add_qml_module` call) probably names this type — update the FQN.

- [ ] **Step 8: Update tests**

`libs/markoff-view-qml/tests/tst_view_qml_source_binding.cpp` likely instantiates the binding and uses `qtQuickDocument`. Update to:
- Include path: `<markoff-foundation/SourceTextDocumentBinding.h>`
- Class name: `Markoff::SourceTextDocumentBinding`
- Property name: `textDocument`
- The test passes a `QTextDocument *` directly now — likely simpler than before (less QML scaffolding).

- [ ] **Step 9: Configure + build**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```

Expected: clean build.

- [ ] **Step 10: Run all 34 existing tests**

```bash
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_)' --output-on-failure -j 8
```

Expected: 34/34 pass. Specifically `tst_view_qml_source_binding` and `tst_view_qml_integration` exercise the binding.

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "foundation: relocate SourceTextDocumentBinding from view-qml; rename qtQuickDocument→textDocument"
```

---

## Phase B — Library scaffolding

### Task B1: Create `libs/markoff-source` skeleton

**Files:**
- Create: `libs/markoff-source/CMakeLists.txt`
- Create: `libs/markoff-source/CLAUDE.md`
- Create: `libs/markoff-source/include/markoff/source/widget/Editor.h` (skeleton)
- Create: `libs/markoff-source/include/markoff/source/widget/FindBar.h` (skeleton)
- Create: `libs/markoff-source/src/Editor.cpp` (skeleton)
- Create: `libs/markoff-source/src/FindBar.cpp` (skeleton)
- Create: `libs/markoff-source/src/Gutter.h` (skeleton)
- Create: `libs/markoff-source/src/Gutter.cpp` (skeleton)
- Create: `libs/markoff-source/tests/CMakeLists.txt`
- Create: `libs/markoff-source/tests/tst_source_widget_editor.cpp` (placeholder)
- Modify: top-level `CMakeLists.txt`

- [ ] **Step 1: Add the directory + CMake target**

`libs/markoff-source/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_source LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(markoff_source STATIC
    src/Editor.cpp
    src/FindBar.cpp
    src/Gutter.cpp
    include/markoff/source/widget/Editor.h
    include/markoff/source/widget/FindBar.h
    src/Gutter.h
)

target_include_directories(markoff_source
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff_source
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
            KF6::SyntaxHighlighting
            markoff_core
)

add_library(Markoff::Source ALIAS markoff_source)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Add to top-level CMakeLists**

In the worktree's top-level `CMakeLists.txt`, find the existing `add_subdirectory(libs/markoff-view-qml)` line and add `add_subdirectory(libs/markoff-source)` immediately after it.

- [ ] **Step 3: Stub headers + impls**

Each file gets the SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` and a minimal definition.

`include/markoff/source/widget/Editor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <markoff/core/Theme.h>

namespace Markoff { class MarkoffDocument; class SourceTextDocumentBinding; }

namespace Markoff::Source::Widget {

class Gutter;

class Editor : public QPlainTextEdit {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *document READ document
               WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Theme theme READ theme
               WRITE setTheme NOTIFY themeChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *);

    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

Q_SIGNALS:
    void documentChanged();
    void themeChanged();

private:
    Markoff::MarkoffDocument          *m_document = nullptr;
    Markoff::SourceTextDocumentBinding *m_binding = nullptr;
    Gutter                             *m_gutter  = nullptr;
    Markoff::Theme                      m_theme;
};

} // namespace Markoff::Source::Widget
```

Note: the existing class is renamed from QPlainTextEdit's standard `document()` accessor — we override it with the Markoff document. To avoid confusion, the underlying `QPlainTextEdit::document()` is still accessible as `QPlainTextEdit::document()` from inside the class. Public `document()` returns `Markoff::MarkoffDocument *`.

`src/Editor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/Editor.h>
#include "Gutter.h"

namespace Markoff::Source::Widget {

Editor::Editor(QWidget *parent) : QPlainTextEdit(parent), m_theme(Markoff::Theme::defaultLight()) {}
Editor::~Editor() = default;

Markoff::MarkoffDocument *Editor::document() const { return m_document; }
void Editor::setDocument(Markoff::MarkoffDocument *) { /* TODO Phase C */ }
Markoff::Theme Editor::theme() const { return m_theme; }
void Editor::setTheme(const Markoff::Theme &t) { m_theme = t; emit themeChanged(); }

} // namespace
```

`include/markoff/source/widget/FindBar.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

namespace Markoff::Source::Widget {

class Editor;

class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(Editor *editor, QWidget *parent = nullptr);

Q_SIGNALS:
    void closed();

private:
    Editor *m_editor = nullptr;
};

} // namespace
```

`src/FindBar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/FindBar.h>
#include <markoff/source/widget/Editor.h>

namespace Markoff::Source::Widget {

FindBar::FindBar(Editor *editor, QWidget *parent) : QWidget(parent), m_editor(editor) {}

} // namespace
```

`src/Gutter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

namespace Markoff::Source::Widget {

class Editor;

class Gutter : public QWidget {
    Q_OBJECT
public:
    explicit Gutter(Editor *editor);

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;

private:
    Editor *m_editor = nullptr;
};

} // namespace
```

`src/Gutter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Gutter.h"
#include <markoff/source/widget/Editor.h>

namespace Markoff::Source::Widget {

Gutter::Gutter(Editor *editor) : QWidget(editor->viewport()), m_editor(editor) {}
void Gutter::paintEvent(QPaintEvent *) { /* TODO Phase D */ }
QSize Gutter::sizeHint() const { return QSize(40, 0); }

} // namespace
```

- [ ] **Step 4: Test directory + placeholder test**

`libs/markoff-source/tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tst_source_widget_editor tst_source_widget_editor.cpp)
add_test(NAME tst_source_widget_editor COMMAND tst_source_widget_editor)
target_link_libraries(tst_source_widget_editor PRIVATE Qt6::Test markoff_source)
set_tests_properties(tst_source_widget_editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

`tests/tst_source_widget_editor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/source/widget/Editor.h>

class TstSourceWidgetEditor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editor_constructs() {
        Markoff::Source::Widget::Editor e;
        QVERIFY(e.document() == nullptr);
    }
};

QTEST_MAIN(TstSourceWidgetEditor)
#include "tst_source_widget_editor.moc"
```

- [ ] **Step 5: CLAUDE.md skeleton**

`libs/markoff-source/CLAUDE.md`:

```markdown
# markoff-source-widget

Fully-owned QtWidgets Source view on `markoff-foundation`. Replaces the Qutepart-based `markoff-source` (legacy) over time.

## Public surface
- `Markoff::Source::Widget::Editor` — `QPlainTextEdit` subclass; main public widget.
- `Markoff::Source::Widget::FindBar` — standalone find UI.

## Internal
- `Markoff::Source::Widget::Gutter` — line-number gutter, child of editor's viewport. Single-column at v0; polymorphic-column shape (per legacy `markoff-live::FoldGutter`) when fold arrows arrive.

## Dependencies
- Qt6 Core / Gui / Widgets
- KF6::SyntaxHighlighting
- `markoff-foundation` (Theme, MarkoffDocument, Session, SourceTextDocumentBinding, SearchEngine)

## Conventions
- C++20, Qt6.8+, CMake 3.19+.
- SPDX `GPL-3.0-or-later` on every file.
- `tr()` for user-visible strings.
- `QIcon::fromTheme()` for icons.
- Tests prefix `tst_source_widget_*`.

## Spec
`docs/specs/2026-04-29-source-widget-design.md`
```

- [ ] **Step 6: Configure + build**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_source -j 8
cmake --build build-dev --target tst_source_widget_editor -j 8
```

Expected: clean build of both.

- [ ] **Step 7: Run the placeholder test**

```bash
ctest --test-dir build-dev -R '^tst_source_widget_editor$' --output-on-failure
```

Expected: 1 PASS.

- [ ] **Step 8: Run the full suite**

```bash
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_|source_widget_)' --output-on-failure -j 8
```

Expected: 35/35 PASS (was 34, +1 placeholder).

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-source CMakeLists.txt
git commit -m "scaffold: libs/markoff-source with skeleton Editor/FindBar/Gutter"
```

---

## Phase C — Editor core

### Task C1: Implement `setDocument` + binding wiring + KSyntaxHighlighting

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/widget/Editor.h`
- Modify: `libs/markoff-source/src/Editor.cpp`
- Modify: `libs/markoff-source/tests/tst_source_widget_editor.cpp`
- Create: `libs/markoff-source/tests/tst_source_widget_binding_roundtrip.cpp`
- Modify: `libs/markoff-source/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests first**

Append to `tst_source_widget_editor.cpp`:

```cpp
    void setDocument_attaches_and_seed_text_appears() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        doc.resetContent(QByteArray("hello world"), Markoff::Origin::FirstOpen);
        e.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        // setDocument may itself trigger a parse-flow; allow brief settle.
        QTest::qWait(50);
        QCOMPARE(e.toPlainText(), QStringLiteral("hello world"));
        QCOMPARE(e.document(), &doc);
    }
```

Includes for that test method (add at top):

```cpp
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <QSignalSpy>
```

Create `tests/tst_source_widget_binding_roundtrip.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/source/widget/Editor.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>

class TstSourceWidgetBindingRoundtrip : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_propagates_to_markoff_document() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(&e, QStringLiteral("abc"));
        QTRY_COMPARE(doc.toMarkdownUtf8(), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        e.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello");
        doc.applyLocalEdit({ ed });
        QTRY_COMPARE(e.toPlainText(), QStringLiteral("hello"));
    }

    void crdt_undo_via_ctrl_z() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(&e, QStringLiteral("abc"));
        QTRY_COMPARE(doc.toMarkdownUtf8(), QByteArrayLiteral("abc"));
        QTest::keyClick(&e, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(doc.toMarkdownUtf8(), QByteArray());
    }
};

QTEST_MAIN(TstSourceWidgetBindingRoundtrip)
#include "tst_source_widget_binding_roundtrip.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_source_widget_binding_roundtrip tst_source_widget_binding_roundtrip.cpp)
add_test(NAME tst_source_widget_binding_roundtrip COMMAND tst_source_widget_binding_roundtrip)
target_link_libraries(tst_source_widget_binding_roundtrip PRIVATE Qt6::Test Qt6::Widgets markoff_source markoff_core)
set_tests_properties(tst_source_widget_binding_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run — confirm tests fail**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_source_widget_' --output-on-failure
```

Expected: `editor_constructs` PASSes; `setDocument_attaches_and_seed_text_appears` FAILs (currently a stub); both binding-roundtrip tests FAIL.

- [ ] **Step 3: Implement Editor**

Update `Editor.h` to declare members + methods:

```cpp
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Source::Widget {
class Gutter;

class Editor : public QPlainTextEdit {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *document READ document
               WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Theme theme READ theme
               WRITE setTheme NOTIFY themeChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *);
    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

Q_SIGNALS:
    void documentChanged();
    void themeChanged();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    Markoff::MarkoffDocument          *m_document = nullptr;
    Markoff::SourceTextDocumentBinding *m_binding = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter = nullptr;
    Gutter                             *m_gutter  = nullptr;
    Markoff::Theme                      m_theme;
};
} // namespace
```

`Editor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/Editor.h>
#include "Gutter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

#include <QKeyEvent>

namespace Markoff::Source::Widget {

namespace {
KSyntaxHighlighting::Repository &repo() {
    static KSyntaxHighlighting::Repository r;
    return r;
}
} // anon

Editor::Editor(QWidget *parent)
    : QPlainTextEdit(parent),
      m_binding(new Markoff::SourceTextDocumentBinding(this)),
      m_highlighter(new KSyntaxHighlighting::SyntaxHighlighter(QPlainTextEdit::document())),
      m_theme(Markoff::Theme::defaultLight())
{
    m_binding->setTextDocument(QPlainTextEdit::document());
    m_highlighter->setDefinition(repo().definitionForName("Markdown"));
    // Theme: pick a default KSH theme matching our defaultLight.
    m_highlighter->setTheme(repo().defaultTheme(KSyntaxHighlighting::Repository::LightTheme));
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
}

Editor::~Editor() = default;

Markoff::MarkoffDocument *Editor::document() const { return m_document; }

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (m_document == doc) return;
    m_document = doc;
    m_binding->setEditorDocument(doc);   // SourceTextDocumentBinding will sync content
    emit documentChanged();
}

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    if (m_gutter) m_gutter->update();
    emit themeChanged();
}

void Editor::keyPressEvent(QKeyEvent *e) {
    const auto m = e->modifiers();
    if (m_document && (m & Qt::ControlModifier)) {
        if (e->key() == Qt::Key_Z && !(m & Qt::ShiftModifier)) {
            m_document->undo();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Y || (e->key() == Qt::Key_Z && (m & Qt::ShiftModifier))) {
            m_document->redo();
            e->accept();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

} // namespace
```

Note: the relocated `SourceTextDocumentBinding`'s API matters here. The implementer reads `libs/markoff-core/include/markoff-foundation/SourceTextDocumentBinding.h` to get the exact setter names. The plan's `setTextDocument(...)` and `setEditorDocument(...)` are placeholder names — the actual setters are whatever survived the relocation rename. The Editor's binding wiring matches.

- [ ] **Step 4: Build + run tests**

```bash
cmake --build build-dev --target tst_source_widget_editor tst_source_widget_binding_roundtrip -j 8
ctest --test-dir build-dev -R '^tst_source_widget_' --output-on-failure
```

Expected: all 4 user methods PASS (placeholder + setDocument + 3 binding-roundtrip).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-source
git commit -m "feat(source-widget): Editor wires MarkoffDocument + KSyntaxHighlighting + CRDT undo"
```

---

## Phase D — Gutter

### Task D1: Line-number gutter

**Files:**
- Modify: `libs/markoff-source/src/Gutter.h`
- Modify: `libs/markoff-source/src/Gutter.cpp`
- Modify: `libs/markoff-source/include/markoff/source/widget/Editor.h`
- Modify: `libs/markoff-source/src/Editor.cpp`

- [ ] **Step 1: Wire gutter into Editor**

In `Editor.cpp`'s constructor, add (after the `m_gutter` member is constructed):

```cpp
m_gutter = new Gutter(this);
connect(this, &QPlainTextEdit::blockCountChanged,
        this, [this]() { recomputeGutterWidth(); });
connect(this, &QPlainTextEdit::updateRequest,
        this, [this](const QRect &rect, int dy) {
    if (dy) m_gutter->scroll(0, dy);
    else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    if (rect.contains(viewport()->rect())) recomputeGutterWidth();
});
recomputeGutterWidth();
```

Add to `Editor.h`:

```cpp
private:
    void recomputeGutterWidth();
    int  gutterWidth() const;

protected:
    void resizeEvent(QResizeEvent *) override;
```

And in `Editor.cpp`:

```cpp
int Editor::gutterWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 6;
}

void Editor::recomputeGutterWidth() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

void Editor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}
```

- [ ] **Step 2: Implement Gutter painter**

Replace `Gutter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Gutter.h"
#include <markoff/source/widget/Editor.h>

#include <QPainter>
#include <QTextBlock>

namespace Markoff::Source::Widget {

Gutter::Gutter(Editor *editor) : QWidget(editor), m_editor(editor) {}

QSize Gutter::sizeHint() const { return QSize(40, 0); }

void Gutter::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    const Markoff::Theme &t = m_editor->theme();

    const QColor bgBase  = t.color(Markoff::Theme::Slot::EditorBackground);
    // Slightly darker (or lighter on dark) gutter strip.
    const QColor bgGutter = bgBase.lightnessF() > 0.5
        ? bgBase.darker(108) : bgBase.lighter(115);
    const QColor fg       = t.color(Markoff::Theme::Slot::TextDefault);
    QColor digit          = fg; digit.setAlphaF(0.55);
    QColor digitActive    = fg;
    QColor sep            = fg; sep.setAlphaF(0.18);

    p.fillRect(event->rect(), bgGutter);
    // Right-edge separator
    p.setPen(sep);
    p.drawLine(width() - 1, event->rect().top(), width() - 1, event->rect().bottom());

    QTextBlock block = m_editor->firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = m_editor->blockBoundingGeometry(block)
                    .translated(m_editor->contentOffset()).top();
    qreal bottom = top + m_editor->blockBoundingRect(block).height();
    const int currentLine = m_editor->textCursor().blockNumber();

    p.setFont(m_editor->font());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            p.setPen(blockNumber == currentLine ? digitActive : digit);
            const QString num = QString::number(blockNumber + 1);
            p.drawText(0, int(top), width() - 6,
                       m_editor->fontMetrics().height(),
                       Qt::AlignRight | Qt::AlignVCenter, num);
        }
        block = block.next();
        top = bottom;
        bottom = top + m_editor->blockBoundingRect(block).height();
        ++blockNumber;
    }
}

} // namespace
```

Header: nothing else needs changing for v0; `Gutter.h` already declares `paintEvent` + `sizeHint`.

- [ ] **Step 3: Repaint gutter on cursor move**

In `Editor.cpp`'s constructor, add:

```cpp
connect(this, &QPlainTextEdit::cursorPositionChanged,
        this, [this]() { if (m_gutter) m_gutter->update(); });
```

(So the "current line" highlight on the gutter follows the cursor.)

- [ ] **Step 4: Build + run all tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_|source_widget_)' --output-on-failure -j 8
```

Expected: still all PASS (gutter is purely visual at v0; no test asserts on it directly, just that the editor still works).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-source
git commit -m "feat(source-widget): line-number gutter (theme-driven, current-line emphasis)"
```

---

## Phase E — Theme application + KSyntaxHighlighter retheme

### Task E1: Wire `setTheme` to palette + highlighter

**Files:**
- Modify: `libs/markoff-source/src/Editor.cpp`
- Modify: `libs/markoff-source/tests/tst_source_widget_editor.cpp`

- [ ] **Step 1: Add a setTheme test**

Append to `tst_source_widget_editor.cpp`:

```cpp
    void setTheme_updates_palette_base_color() {
        Markoff::Source::Widget::Editor e;
        Markoff::Theme t = Markoff::Theme::defaultLight();
        const QColor sentinel("#abcdef");
        t.setColor(Markoff::Theme::Slot::EditorBackground, sentinel);
        e.setTheme(t);
        QCOMPARE(e.palette().color(QPalette::Base), sentinel);
    }
```

(Include `<markoff-foundation/Theme.h>` if not already.)

- [ ] **Step 2: Implement palette + highlighter retheme**

In `Editor.cpp`'s `setTheme`:

```cpp
void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    QPalette p = palette();
    p.setColor(QPalette::Base,            t.color(Markoff::Theme::Slot::EditorBackground));
    p.setColor(QPalette::Text,            t.color(Markoff::Theme::Slot::TextDefault));
    p.setColor(QPalette::Highlight,       t.color(Markoff::Theme::Slot::SelectionBackground));
    p.setColor(QPalette::HighlightedText, t.color(Markoff::Theme::Slot::TextDefault));
    setPalette(p);

    // Pick a KSH theme based on background luminance.
    const bool darkUi = t.color(Markoff::Theme::Slot::EditorBackground).lightnessF() < 0.5;
    if (m_highlighter) {
        m_highlighter->setTheme(repo().defaultTheme(
            darkUi ? KSyntaxHighlighting::Repository::DarkTheme
                   : KSyntaxHighlighting::Repository::LightTheme));
        m_highlighter->rehighlight();
    }
    if (m_gutter) m_gutter->update();
    emit themeChanged();
}
```

- [ ] **Step 3: Build + run tests**

```bash
cmake --build build-dev --target tst_source_widget_editor -j 8
ctest --test-dir build-dev -R '^tst_source_widget_editor$' --output-on-failure
```

Expected: PASS (all 3 user methods including the new `setTheme_updates_palette_base_color`).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-source
git commit -m "feat(source-widget): setTheme applies palette + retones KSyntaxHighlighter"
```

---

## Phase F — FindBar

### Task F1: FindBar implementation + tests

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/widget/FindBar.h`
- Modify: `libs/markoff-source/src/FindBar.cpp`
- Create: `libs/markoff-source/tests/tst_source_widget_findbar.cpp`
- Modify: `libs/markoff-source/tests/CMakeLists.txt`

- [ ] **Step 1: Read foundation's SearchEngine API**

Read `libs/markoff-core/include/markoff-foundation/SearchEngine.h` (or `SearchController.h`) to learn the exact API for find-all / find-next. The FindBar uses whichever is the cleanest fit.

- [ ] **Step 2: Implement FindBar**

`include/markoff/source/widget/FindBar.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <QList>
#include <QTextEdit>
class QLineEdit;
class QToolButton;
class QLabel;

namespace Markoff::Source::Widget {

class Editor;

class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(Editor *editor, QWidget *parent = nullptr);

public Q_SLOTS:
    void activate();   // show + focus + (re-search current needle)
    void deactivate(); // hide + clear highlights

Q_SIGNALS:
    void closed();

private Q_SLOTS:
    void onNeedleChanged(const QString &);
    void next();
    void prev();

private:
    void recomputeMatches();
    void highlightAll();
    void seekTo(int matchIndex);
    void updateCountLabel();

    Editor      *m_editor   = nullptr;
    QLineEdit   *m_input    = nullptr;
    QToolButton *m_prev     = nullptr;
    QToolButton *m_next     = nullptr;
    QToolButton *m_close    = nullptr;
    QLabel      *m_count    = nullptr;
    QList<QTextEdit::ExtraSelection> m_matches;
    int          m_currentIndex = -1;
};

} // namespace
```

`src/FindBar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/FindBar.h>
#include <markoff/source/widget/Editor.h>

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QTextDocument>
#include <QToolButton>

namespace Markoff::Source::Widget {

FindBar::FindBar(Editor *editor, QWidget *parent)
    : QWidget(parent), m_editor(editor)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Find"));
    m_prev  = new QToolButton(this);
    m_prev->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    m_prev->setToolTip(tr("Previous match"));
    m_next  = new QToolButton(this);
    m_next->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    m_next->setToolTip(tr("Next match"));
    m_count = new QLabel(this);
    m_close = new QToolButton(this);
    m_close->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_close->setToolTip(tr("Close"));

    layout->addWidget(m_input, 1);
    layout->addWidget(m_count);
    layout->addWidget(m_prev);
    layout->addWidget(m_next);
    layout->addWidget(m_close);

    connect(m_input, &QLineEdit::textChanged, this, &FindBar::onNeedleChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &FindBar::next);
    connect(m_prev,  &QToolButton::clicked, this, &FindBar::prev);
    connect(m_next,  &QToolButton::clicked, this, &FindBar::next);
    connect(m_close, &QToolButton::clicked, this, &FindBar::deactivate);

    hide();
}

void FindBar::activate() {
    show();
    m_input->setFocus();
    m_input->selectAll();
    recomputeMatches();
}

void FindBar::deactivate() {
    hide();
    m_matches.clear();
    m_currentIndex = -1;
    m_editor->setExtraSelections({});
    emit closed();
}

void FindBar::onNeedleChanged(const QString &) {
    recomputeMatches();
    m_currentIndex = m_matches.isEmpty() ? -1 : 0;
    highlightAll();
    if (m_currentIndex >= 0) seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::recomputeMatches() {
    m_matches.clear();
    const QString needle = m_input->text();
    if (needle.isEmpty()) return;
    QTextDocument *doc = m_editor->QPlainTextEdit::document();
    QTextCursor c(doc);
    while (!(c = doc->find(needle, c)).isNull()) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = c;
        sel.format.setBackground(QColor("#ffe080"));
        m_matches << sel;
    }
}

void FindBar::highlightAll() {
    m_editor->setExtraSelections(m_matches);
}

void FindBar::seekTo(int matchIndex) {
    if (matchIndex < 0 || matchIndex >= m_matches.size()) return;
    m_editor->setTextCursor(m_matches[matchIndex].cursor);
    m_editor->ensureCursorVisible();
}

void FindBar::next() {
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_matches.size();
    seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::prev() {
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex - 1 + m_matches.size()) % m_matches.size();
    seekTo(m_currentIndex);
    updateCountLabel();
}

void FindBar::updateCountLabel() {
    if (m_matches.isEmpty()) {
        m_count->setText(m_input->text().isEmpty() ? QString() : tr("No matches"));
        return;
    }
    m_count->setText(tr("%1 of %2")
        .arg(m_currentIndex + 1).arg(m_matches.size()));
}

} // namespace
```

(The match colour `#ffe080` is the legacy `Slot::SearchMatchBackground` default — could be theme-driven later. Out of v0 polish.)

- [ ] **Step 3: Add tests**

`tests/tst_source_widget_findbar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QLineEdit>
#include <QTest>

#include <markoff/source/widget/Editor.h>
#include <markoff/source/widget/FindBar.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffEdit.h>

class TstSourceWidgetFindBar : public QObject {
    Q_OBJECT
private:
    Markoff::MarkoffDocument *makeDoc(const QByteArray &seed) {
        auto *d = new Markoff::MarkoffDocument(1);
        d->setCoalescingIdleMs(0);
        Markoff::MarkoffEdit ed; ed.oldStart = 0; ed.oldEnd = 0; ed.newText = seed;
        d->applyLocalEdit({ed});
        return d;
    }

private Q_SLOTS:
    void findbar_finds_first_match() {
        Markoff::Source::Widget::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("the quick brown fox quick"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::Widget::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("quick"));
        QTRY_VERIFY(!e.extraSelections().isEmpty());
        QCOMPARE(e.extraSelections().size(), 2);
        delete doc;
    }

    void findbar_next_prev_navigation() {
        Markoff::Source::Widget::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("aaa bbb aaa ccc aaa"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::Widget::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("aaa"));
        QTRY_COMPARE(e.extraSelections().size(), 3);
        // Cursor sits on first match initially.
        const int firstPos = e.textCursor().position();
        QMetaObject::invokeMethod(&bar, "next");
        QVERIFY(e.textCursor().position() != firstPos);
        QMetaObject::invokeMethod(&bar, "prev");
        QCOMPARE(e.textCursor().position(), firstPos);
        delete doc;
    }

    void findbar_close_clears_highlights() {
        Markoff::Source::Widget::Editor e;
        auto *doc = makeDoc(QByteArrayLiteral("hello hello"));
        e.setDocument(doc);
        e.show();
        Markoff::Source::Widget::FindBar bar(&e);
        bar.activate();
        bar.findChild<QLineEdit *>()->setText(QStringLiteral("hello"));
        QTRY_VERIFY(!e.extraSelections().isEmpty());
        bar.deactivate();
        QVERIFY(e.extraSelections().isEmpty());
        delete doc;
    }
};

QTEST_MAIN(TstSourceWidgetFindBar)
#include "tst_source_widget_findbar.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_source_widget_findbar tst_source_widget_findbar.cpp)
add_test(NAME tst_source_widget_findbar COMMAND tst_source_widget_findbar)
target_link_libraries(tst_source_widget_findbar PRIVATE Qt6::Test Qt6::Widgets markoff_source markoff_core)
set_tests_properties(tst_source_widget_findbar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_source_widget_' --output-on-failure
```

Expected: 3 source-widget test binaries pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-source
git commit -m "feat(source-widget): FindBar with next/prev/close + match-count UI"
```

---

## Phase G — Dev sandbox app

### Task G1: Stand-alone runnable app

**Files:**
- Create: `libs/markoff-source/app/CMakeLists.txt`
- Create: `libs/markoff-source/app/main.cpp`
- Modify: `libs/markoff-source/CMakeLists.txt`

- [ ] **Step 1: app CMake**

`libs/markoff-source/app/CMakeLists.txt`:

```cmake
add_executable(markoff-source-widget-app main.cpp)
target_link_libraries(markoff-source-widget-app
    PRIVATE Qt6::Widgets markoff_source markoff_core)
```

- [ ] **Step 2: main.cpp**

`libs/markoff-source/app/main.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QFile>
#include <QMainWindow>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWidget>

#include <markoff/source/widget/Editor.h>
#include <markoff/source/widget/FindBar.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/Origin.h>

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    Markoff::MarkoffDocument doc(1);
    doc.setCoalescingIdleMs(80);

    QByteArray seed;
    if (argc > 1) {
        QFile f(QString::fromUtf8(argv[1]));
        if (f.open(QIODevice::ReadOnly)) seed = f.readAll();
    }
    if (!seed.isEmpty()) doc.resetContent(seed, Markoff::Origin::FirstOpen);

    QMainWindow win;
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *editor = new Markoff::Source::Widget::Editor;
    editor->setDocument(&doc);
    auto *findbar = new Markoff::Source::Widget::FindBar(editor);

    layout->addWidget(editor, 1);
    layout->addWidget(findbar);

    win.setCentralWidget(central);
    win.resize(900, 700);
    win.setWindowTitle(QObject::tr("markoff-source-widget"));

    auto *findShortcut = new QShortcut(QKeySequence::Find, &win);
    QObject::connect(findShortcut, &QShortcut::activated, findbar,
                     &Markoff::Source::Widget::FindBar::activate);

    win.show();
    return app.exec();
}
```

- [ ] **Step 3: Hook into lib CMake**

In `libs/markoff-source/CMakeLists.txt`, append after the test-subdirectory clause:

```cmake
add_subdirectory(app)
```

- [ ] **Step 4: Build the app**

```bash
cmake --build build-dev --target markoff-source-widget-app -j 8
```

Expected: clean build. The binary is at `./build-dev/bin/markoff-source-widget-app`.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-source/app libs/markoff-source/CMakeLists.txt
git commit -m "app(source-widget): minimal QMainWindow sandbox for hand-testing"
```

---

## Phase H — Final integration check

### Task H1: full-suite verification

- [ ] **Step 1: Full build**

```bash
cmake --build build-dev -j 8
```

Expected: success.

- [ ] **Step 2: Full test suite**

```bash
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_|source_widget_)' --output-on-failure -j 8
```

Expected: 37 binaries pass (was 34, +3 new for source-widget — `tst_source_widget_editor`, `tst_source_widget_binding_roundtrip`, `tst_source_widget_findbar`).

- [ ] **Step 3: Final log**

```bash
git log --oneline -10
```

Expected: spec + 7 implementation commits at HEAD.
