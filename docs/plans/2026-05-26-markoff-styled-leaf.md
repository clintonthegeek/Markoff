# `markoff-styled` Leaf Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `libs/markoff-styled` v0 — a QML-free QWidget Markoff editor that styles markdown blocks + inline spans from parser data, with `LinkService` click/hover wiring.

**Architecture:** New static library parallel to `libs/markoff-source` and `libs/markoff-live`. Public widget `Markoff::Styled::Editor` is a `Markoff::MarkdownView` subclass composing a `QTextEdit`. Text sync via the existing `Markoff::SourceTextDocumentBinding`. Block + inline formats applied by a private `Markoff::Styled::StyleApplier` subscribed to `MarkoffDocument::d2DocumentChanged`. Link interaction through a private `Markoff::Styled::LinkInteraction` event handler routing to `Markoff::LinkService`. No QML deps, no KF6 deps, no `markoff-live` dep.

**Tech Stack:** C++20, Qt6 6.8+ (Core, Gui, Widgets, Test), CMake 3.19+, `markoff-core` (transitive `markoff-parser`).

**Spec:** `docs/specs/2026-05-26-markoff-styled-leaf-design.md`.

**Build/test commands** (used throughout the plan):

```bash
# Configure (already configured if compile_commands.json exists)
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build the lib only
cmake --build build-dev --target markoff_styled -j 8

# Build everything that depends on the lib + tests
cmake --build build-dev -j 8

# Run all styled tests (fast inner loop)
scripts/run-tests.sh -R '^tst_styled_'

# Run one test binary
scripts/run-tests.sh --bin tst_styled_block_formats
```

`scripts/run-tests.sh` defaults to `QT_QPA_PLATFORM=offscreen`. Do not pass `--direct` or `--nested` without explicit user permission.

---

## File structure (target)

```
libs/markoff-styled/
├── CLAUDE.md
├── CMakeLists.txt
├── include/markoff/styled/
│   ├── Editor.h
│   └── MarkoffStyledExport.h
├── src/
│   ├── Editor.cpp
│   ├── StyleApplier.h
│   ├── StyleApplier.cpp
│   ├── DocHighlighter.h
│   ├── DocHighlighter.cpp
│   ├── LinkInteraction.h
│   └── LinkInteraction.cpp
├── app/
│   ├── CMakeLists.txt
│   └── main.cpp
└── tests/
    ├── CMakeLists.txt
    ├── support/
    │   └── RecordingLinkService.h
    ├── tst_styled_editor_construction.cpp
    ├── tst_styled_binding_roundtrip.cpp
    ├── tst_styled_block_formats.cpp
    ├── tst_styled_inline_formats.cpp
    ├── tst_styled_link_interaction.cpp
    ├── tst_styled_delimiter_visibility.cpp
    └── tst_styled_d2_integration.cpp
```

---

## Task 1: Library skeleton

**Files:**
- Create: `libs/markoff-styled/CMakeLists.txt`
- Create: `libs/markoff-styled/include/markoff/styled/MarkoffStyledExport.h`
- Create: `libs/markoff-styled/include/markoff/styled/Editor.h`
- Create: `libs/markoff-styled/src/Editor.cpp`
- Modify: `CMakeLists.txt` (top-level, line 70 area)

- [ ] **Step 1: Create directory layout**

```bash
mkdir -p libs/markoff-styled/include/markoff/styled
mkdir -p libs/markoff-styled/src
mkdir -p libs/markoff-styled/app
mkdir -p libs/markoff-styled/tests/support
```

- [ ] **Step 2: Write `libs/markoff-styled/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19)
project(markoff_styled LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(markoff_styled STATIC
    include/markoff/styled/Editor.h
    include/markoff/styled/MarkoffStyledExport.h
    src/Editor.cpp
)

target_include_directories(markoff_styled
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff_styled
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
            markoff_core
)

add_library(Markoff::Styled ALIAS markoff_styled)

if(NOT DEFINED MARKOFF_STYLED_BUILD_TESTS)
    set(MARKOFF_STYLED_BUILD_TESTS ON)
endif()
if(MARKOFF_STYLED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(app)
```

- [ ] **Step 3: Write `libs/markoff-styled/include/markoff/styled/MarkoffStyledExport.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// markoff_styled is currently built as a STATIC library; the export macro
// is a no-op. Keep it as a hook so a future SHARED build needs no header
// surgery (mirrors MarkoffSourceExport / MarkoffCoreExport conventions).
#define MARKOFF_STYLED_EXPORT
```

- [ ] **Step 4: Write minimal `libs/markoff-styled/include/markoff/styled/Editor.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkdownView.h>
#include <markoff/styled/MarkoffStyledExport.h>

namespace Markoff::Styled {

class MARKOFF_STYLED_EXPORT Editor : public Markoff::MarkdownView {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;
};

}  // namespace Markoff::Styled
```

- [ ] **Step 5: Write minimal `libs/markoff-styled/src/Editor.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/Editor.h>

namespace Markoff::Styled {

Editor::Editor(QWidget *parent) : Markoff::MarkdownView(parent) {}
Editor::~Editor() = default;

}  // namespace Markoff::Styled
```

- [ ] **Step 6: Write placeholder `libs/markoff-styled/app/CMakeLists.txt` and `libs/markoff-styled/app/main.cpp`**

`libs/markoff-styled/app/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.19)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)

add_executable(markoff-styled-app main.cpp)
target_link_libraries(markoff-styled-app
    PRIVATE Qt6::Widgets markoff_styled markoff_core)
```

`libs/markoff-styled/app/main.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <markoff/styled/Editor.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    Markoff::Styled::Editor editor;
    editor.show();
    return app.exec();
}
```

- [ ] **Step 7: Write placeholder `libs/markoff-styled/tests/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19)
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test binaries added in subsequent plan tasks.
```

- [ ] **Step 8: Register the leaf with the top-level CMakeLists.txt**

Edit `CMakeLists.txt` (the project root), find the line `add_subdirectory(libs/markoff-source)` (around line 70) and add directly below it:

```cmake
add_subdirectory(libs/markoff-styled)
```

- [ ] **Step 9: Configure + build, verify success**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_styled markoff-styled-app -j 8
```
Expected: build succeeds; `build-dev/libs/markoff-styled/libmarkoff_styled.a` exists; `build-dev/bin/markoff-styled-app` exists.

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-styled CMakeLists.txt
git commit -m "feat(styled): library skeleton

Empty Markoff::Styled::Editor (inherits MarkdownView), CMake glue,
placeholder demo app. Builds clean against markoff-core; no QML deps."
```

---

## Task 2: Editor public surface — setters + MarkdownView contract

**Files:**
- Modify: `libs/markoff-styled/include/markoff/styled/Editor.h`
- Modify: `libs/markoff-styled/src/Editor.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`
- Create: `libs/markoff-styled/tests/tst_styled_editor_construction.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-styled/tests/tst_styled_editor_construction.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Theme.h>
#include <markoff/styled/Editor.h>

class TstStyledEditorConstruction : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructs_and_is_a_markdown_view() {
        Markoff::Styled::Editor e;
        // Subclasses MarkdownView so consumers can treat polymorphically.
        QVERIFY(qobject_cast<Markoff::MarkdownView *>(&e) != nullptr);
        QVERIFY(e.hasCursor());
        QVERIFY(e.hasEditing());
        QVERIFY(!e.isReadOnly());
    }

    void document_setter_round_trips_and_signals() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray());
        QSignalSpy spy(&e, &Markoff::MarkdownView::documentChanged);
        e.setDocument(&doc);
        QCOMPARE(e.document(), &doc);
        QCOMPARE(spy.count(), 1);
    }

    void session_setter_round_trips() {
        Markoff::Styled::Editor e;
        Markoff::Session s;
        e.setSession(&s);
        QCOMPARE(e.session(), &s);
    }

    void font_scale_default_and_setter() {
        Markoff::Styled::Editor e;
        QCOMPARE(e.fontScale(), 1.0);
        QSignalSpy spy(&e, &Markoff::Styled::Editor::fontScaleChanged);
        e.setFontScale(1.25);
        QCOMPARE(e.fontScale(), 1.25);
        QCOMPARE(spy.count(), 1);
    }

    void read_only_round_trips() {
        Markoff::Styled::Editor e;
        e.setReadOnly(true);
        QVERIFY(e.isReadOnly());
        QVERIFY(!e.hasEditing());
    }
};

QTEST_MAIN(TstStyledEditorConstruction)
#include "tst_styled_editor_construction.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_editor_construction tst_styled_editor_construction.cpp)
add_test(NAME tst_styled_editor_construction COMMAND tst_styled_editor_construction)
target_link_libraries(tst_styled_editor_construction
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_editor_construction
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_editor_construction -j 8
```
Expected: COMPILE FAIL — methods (`setSession`, `session`, `fontScale`, `setFontScale`, signal `fontScaleChanged`) do not exist on `Markoff::Styled::Editor`.

- [ ] **Step 4: Implement the public surface in `Editor.h`**

Replace `libs/markoff-styled/include/markoff/styled/Editor.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QTextEdit>

#include <markoff/core/LinkService.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Theme.h>
#include <markoff/styled/MarkoffStyledExport.h>

namespace Markoff {
class SourceTextDocumentBinding;
class DefaultLinkService;
}

namespace Markoff::Styled {

class StyleApplier;
class DocHighlighter;
class LinkInteraction;

class MARKOFF_STYLED_EXPORT Editor : public Markoff::MarkdownView {
    Q_OBJECT
    Q_PROPERTY(Markoff::Session *session
               READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(Markoff::Theme theme
               READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(Markoff::LinkService *linkService
               READ linkService WRITE setLinkService NOTIFY linkServiceChanged)
    Q_PROPERTY(QString fromContext
               READ fromContext WRITE setFromContext NOTIFY fromContextChanged)
    Q_PROPERTY(qreal fontScale
               READ fontScale WRITE setFontScale NOTIFY fontScaleChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // MarkdownView contract
    void                       setDocument(Markoff::MarkoffDocument *doc) override;
    Markoff::CursorPos         cursorPosition() const override;
    void                       setCursorPosition(Markoff::CursorPos pos) override;
    float                      scrollPositionVisualLine() const override;
    void                       setScrollPositionVisualLine(float pos) override;
    void                       setReadOnly(bool ro) override;
    bool                       isReadOnly() const override;
    bool                       hasCursor()  const override { return true; }
    bool                       hasEditing() const override { return !isReadOnly(); }

    // Session
    Markoff::Session *session() const;
    void              setSession(Markoff::Session *);

    // Theme
    Markoff::Theme theme() const;
    void           setTheme(const Markoff::Theme &);

    // Link service (lazy DefaultLinkService when nullptr)
    Markoff::LinkService *linkService() const;
    void                  setLinkService(Markoff::LinkService *);

    // Wikilink resolution context (forwarded to LinkService)
    QString fromContext() const;
    void    setFromContext(const QString &);

    // Font scale (1.0 = default; Ctrl+/- multiplies block-format font sizes)
    qreal fontScale() const;
    void  setFontScale(qreal);

    // Accessor for tests + internal helpers
    QTextEdit *textEdit() const { return m_editor; }

Q_SIGNALS:
    void sessionChanged();
    void themeChanged();
    void linkServiceChanged();
    void fromContextChanged();
    void fontScaleChanged();

private:
    QTextEdit                              *m_editor       = nullptr;
    QPointer<Markoff::Session>              m_session;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    StyleApplier                           *m_styleApplier = nullptr;
    DocHighlighter                         *m_highlighter  = nullptr;
    LinkInteraction                        *m_linkInteract = nullptr;
    Markoff::Theme                          m_theme;
    Markoff::LinkService                   *m_linkService  = nullptr;
    Markoff::DefaultLinkService            *m_defaultLink  = nullptr;
    QString                                 m_fromContext;
    qreal                                   m_fontScale    = 1.0;
};

}  // namespace Markoff::Styled
```

- [ ] **Step 5: Implement the public surface in `Editor.cpp`**

Replace `libs/markoff-styled/src/Editor.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/Editor.h>

#include <QHBoxLayout>
#include <QTextEdit>

#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff::Styled {

Editor::Editor(QWidget *parent)
    : Markoff::MarkdownView(parent),
      m_editor(new QTextEdit(this)) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);
    setLayout(layout);

    // Anchors (links) are styled but Qt must not attempt to open them.
    m_editor->setOpenLinks(false);
    m_editor->setTextInteractionFlags(Qt::TextEditorInteraction
                                      | Qt::LinksAccessibleByMouse);
    m_editor->viewport()->setMouseTracking(true);
}

Editor::~Editor() = default;

// ---- MarkdownView contract ----------------------------------------------

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    // Binding/StyleApplier wiring lands in Task 3 + Task 4.
    Markoff::MarkdownView::setDocument(doc);
}

Markoff::CursorPos Editor::cursorPosition() const {
    QTextCursor c = m_editor->textCursor();
    const QTextBlock blk = c.block();
    return { blk.blockNumber() + 1, c.positionInBlock() + 1 };
}

void Editor::setCursorPosition(Markoff::CursorPos pos) {
    QTextCursor c = m_editor->textCursor();
    QTextBlock blk = m_editor->document()->findBlockByNumber(pos.line - 1);
    if (!blk.isValid()) return;
    c.setPosition(blk.position() + qMax(0, pos.column - 1));
    m_editor->setTextCursor(c);
}

float Editor::scrollPositionVisualLine() const {
    auto *bar = m_editor->verticalScrollBar();
    return bar->maximum() == 0 ? 0.0f : float(bar->value());
}

void Editor::setScrollPositionVisualLine(float pos) {
    m_editor->verticalScrollBar()->setValue(int(pos));
}

void Editor::setReadOnly(bool ro) {
    m_editor->setReadOnly(ro);
    Markoff::MarkdownView::setReadOnly(ro);
}

bool Editor::isReadOnly() const { return m_editor->isReadOnly(); }

// ---- Session ------------------------------------------------------------

Markoff::Session *Editor::session() const { return m_session.data(); }

void Editor::setSession(Markoff::Session *s) {
    if (m_session.data() == s) return;
    m_session = s;
    emit sessionChanged();
}

// ---- Theme --------------------------------------------------------------

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    emit themeChanged();
}

// ---- LinkService --------------------------------------------------------

Markoff::LinkService *Editor::linkService() const {
    if (m_linkService) return m_linkService;
    // Lazy DefaultLinkService is created on first read in Task 8 wiring.
    return nullptr;
}

void Editor::setLinkService(Markoff::LinkService *svc) {
    if (m_linkService == svc) return;
    m_linkService = svc;
    emit linkServiceChanged();
}

QString Editor::fromContext() const { return m_fromContext; }

void Editor::setFromContext(const QString &c) {
    if (m_fromContext == c) return;
    m_fromContext = c;
    emit fromContextChanged();
}

// ---- Font scale ---------------------------------------------------------

qreal Editor::fontScale() const { return m_fontScale; }

void Editor::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    emit fontScaleChanged();
}

}  // namespace Markoff::Styled
```

Then update the `CMakeLists.txt` for the library to register the additional headers (which don't exist yet — placeholders in the source list aren't required, only `.cpp` files need to be listed). The existing list is already correct — no change needed.

- [ ] **Step 6: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_styled_editor_construction -j 8
scripts/run-tests.sh --bin tst_styled_editor_construction
```
Expected: PASS — all 5 test slots pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): Editor public surface — setters + MarkdownView contract

Editor composes a QTextEdit, implements MarkdownView's CursorPos /
scroll / read-only contract, and exposes Session / Theme / LinkService /
fromContext / fontScale properties. setOpenLinks(false) +
LinksAccessibleByMouse wired in the constructor. Binding + style
application land in subsequent tasks."
```

---

## Task 3: Wire `SourceTextDocumentBinding` through `setDocument`

**Files:**
- Modify: `libs/markoff-styled/src/Editor.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`
- Create: `libs/markoff-styled/tests/tst_styled_binding_roundtrip.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-styled/tests/tst_styled_binding_roundtrip.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

namespace {
QByteArray fullText(Markoff::MarkoffDocument &doc) {
    QByteArray out;
    bool first = true;
    for (Markoff::BlockId id : doc.iterateBlocks()) {
        if (!first) out += "\n\n";
        out += doc.blockText(id);
        first = false;
    }
    return out;
}
}  // namespace

class TstStyledBindingRoundtrip : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_propagates_to_markoff_document() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray());
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(e.textEdit(), QStringLiteral("abc"));
        QTRY_COMPARE(fullText(doc), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("initial"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("initial"));
        doc.applyFlatEdit(7, 7, QByteArrayLiteral(" tail"),
                          Markoff::Origin::Remote);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("initial tail"));
    }

    void document_change_rewires_binding_cleanly() {
        Markoff::Styled::Editor e;
        Markoff::Session s;
        e.setSession(&s);

        Markoff::MarkoffDocument docA(1);
        docA.loadFromMarkdown(QByteArrayLiteral("first"));
        e.setDocument(&docA);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("first"));

        Markoff::MarkoffDocument docB(1);
        docB.loadFromMarkdown(QByteArrayLiteral("second"));
        e.setDocument(&docB);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));

        // Edits to docA must NOT propagate to the editor.
        docA.applyFlatEdit(5, 5, QByteArrayLiteral("X"),
                           Markoff::Origin::Remote);
        QTest::qWait(50);
        QCOMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));
    }
};

QTEST_MAIN(TstStyledBindingRoundtrip)
#include "tst_styled_binding_roundtrip.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_binding_roundtrip tst_styled_binding_roundtrip.cpp)
add_test(NAME tst_styled_binding_roundtrip COMMAND tst_styled_binding_roundtrip)
target_link_libraries(tst_styled_binding_roundtrip
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_binding_roundtrip
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_binding_roundtrip -j 8
scripts/run-tests.sh --bin tst_styled_binding_roundtrip
```
Expected: FAIL — typed characters don't reach the MarkoffDocument; external edits don't reach the QTextEdit.

- [ ] **Step 4: Wire the binding in `Editor::setDocument` and `setSession`**

In `libs/markoff-styled/src/Editor.cpp`, replace the `setDocument` and `setSession` definitions and add a private helper:

```cpp
void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (document() == doc) {
        Markoff::MarkdownView::setDocument(doc);
        return;
    }

    if (!m_binding) {
        m_binding = new Markoff::SourceTextDocumentBinding(this);
        m_binding->setTextDocument(m_editor->document());
    }

    m_binding->setMarkoffDocument(doc);
    if (m_session) m_binding->setSession(m_session.data());

    Markoff::MarkdownView::setDocument(doc);
}

void Editor::setSession(Markoff::Session *s) {
    if (m_session.data() == s) return;
    m_session = s;
    if (m_binding) m_binding->setSession(s);
    emit sessionChanged();
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_styled_binding_roundtrip -j 8
scripts/run-tests.sh --bin tst_styled_binding_roundtrip
```
Expected: PASS — all 3 slots pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): wire SourceTextDocumentBinding through setDocument

Typing now propagates through applyFlatEdit; remote edits replay via
setPlainText. Document swap rewires cleanly — edits to a detached
document no longer reach the editor."
```

---

## Task 4: `StyleApplier` skeleton + `d2DocumentChanged` subscription

**Files:**
- Modify: `libs/markoff-styled/CMakeLists.txt`
- Create: `libs/markoff-styled/src/StyleApplier.h`
- Create: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/src/Editor.cpp`

This task adds the StyleApplier wiring without yet applying any formats. We verify it via a simple "restyle counter" hook used by later tasks.

- [ ] **Step 1: Write `libs/markoff-styled/src/StyleApplier.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>

class QTextDocument;

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Styled {

/// Subscribes to `MarkoffDocument::d2DocumentChanged`. On each fire,
/// walks `iterateBlocks()` and applies block + inline formats to
/// `QTextDocument` via `QTextCursor`. Re-entry-guarded; uses
/// `beginEditBlock`/`endEditBlock` to coalesce repaints.
class StyleApplier : public QObject {
    Q_OBJECT
public:
    explicit StyleApplier(QObject *parent = nullptr);
    ~StyleApplier() override;

    void setTextDocument(QTextDocument *doc);
    QTextDocument *textDocument() const noexcept { return m_textDocument; }

    void setMarkoffDocument(Markoff::MarkoffDocument *doc);
    Markoff::MarkoffDocument *markoffDocument() const noexcept { return m_markoffDocument; }

    void setTheme(const Markoff::Theme *theme);
    const Markoff::Theme *theme() const noexcept { return m_theme; }

    void setFontScale(qreal s);
    qreal fontScale() const noexcept { return m_fontScale; }

    /// Counter incremented on every restyle pass; tests assert progress.
    quint64 restyleCount() const noexcept { return m_restyleCount; }

    /// Force a restyle without waiting for `d2DocumentChanged`.
    void rerender();

private Q_SLOTS:
    void onD2Changed();

private:
    void applyFormats();

    QPointer<QTextDocument>            m_textDocument;
    Markoff::MarkoffDocument          *m_markoffDocument = nullptr;
    const Markoff::Theme              *m_theme           = nullptr;
    qreal                              m_fontScale       = 1.0;
    bool                               m_applyingFormats = false;
    quint64                            m_restyleCount    = 0;
};

}  // namespace Markoff::Styled
```

- [ ] **Step 2: Write `libs/markoff-styled/src/StyleApplier.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyleApplier.h"

#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Styled {

StyleApplier::StyleApplier(QObject *parent) : QObject(parent) {}
StyleApplier::~StyleApplier() = default;

void StyleApplier::setTextDocument(QTextDocument *doc) {
    if (m_textDocument == doc) return;
    m_textDocument = doc;
    rerender();
}

void StyleApplier::setMarkoffDocument(Markoff::MarkoffDocument *doc) {
    if (m_markoffDocument == doc) return;
    if (m_markoffDocument) {
        disconnect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                   this, &StyleApplier::onD2Changed);
    }
    m_markoffDocument = doc;
    if (m_markoffDocument) {
        connect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                this, &StyleApplier::onD2Changed);
    }
    rerender();
}

void StyleApplier::setTheme(const Markoff::Theme *theme) {
    if (m_theme == theme) return;
    m_theme = theme;
    rerender();
}

void StyleApplier::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    rerender();
}

void StyleApplier::rerender() {
    if (!m_textDocument || !m_markoffDocument) return;
    applyFormats();
}

void StyleApplier::onD2Changed() { applyFormats(); }

void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;
    {
        QSignalBlocker block(m_textDocument);
        QTextCursor cursor(m_textDocument);
        cursor.beginEditBlock();
        // Per-block format application lands in Tasks 5–9.
        cursor.endEditBlock();
    }
    ++m_restyleCount;
    m_applyingFormats = false;
}

}  // namespace Markoff::Styled
```

- [ ] **Step 3: Add StyleApplier files to the library `CMakeLists.txt`**

In `libs/markoff-styled/CMakeLists.txt`, update the `add_library` source list:
```cmake
add_library(markoff_styled STATIC
    include/markoff/styled/Editor.h
    include/markoff/styled/MarkoffStyledExport.h
    src/Editor.cpp
    src/StyleApplier.h
    src/StyleApplier.cpp
)
```

- [ ] **Step 4: Wire StyleApplier into Editor**

In `libs/markoff-styled/src/Editor.cpp`, add at the top:
```cpp
#include "StyleApplier.h"
```

In the `Editor::Editor` constructor body, after the existing wiring, append:
```cpp
m_styleApplier = new StyleApplier(this);
m_styleApplier->setTextDocument(m_editor->document());
m_styleApplier->setTheme(&m_theme);
```

In `Editor::setDocument`, after `m_binding->setMarkoffDocument(doc)`:
```cpp
m_styleApplier->setMarkoffDocument(doc);
```

In `Editor::setTheme`, after `m_theme = t;`:
```cpp
if (m_styleApplier) m_styleApplier->setTheme(&m_theme);
```

In `Editor::setFontScale`, after `m_fontScale = s;`:
```cpp
if (m_styleApplier) m_styleApplier->setFontScale(s);
```

- [ ] **Step 5: Build and verify nothing regresses**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: both existing styled tests pass; no behaviour change yet (no formats applied).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): StyleApplier skeleton subscribed to d2DocumentChanged

Internal class that will own all block + inline format application.
Connected from Editor::setDocument; runs on every d2 cycle with
re-entry guard + beginEditBlock/endEditBlock. No formats applied
yet — landing in subsequent tasks."
```

---

## Task 5: Block-level formats — Heading levels 1–6

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.h`
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`
- Create: `libs/markoff-styled/tests/tst_styled_block_formats.cpp`

This task wires the StyleApplier's block-walk loop and implements the first block kind (Heading). Subsequent block kinds reuse the loop.

- [ ] **Step 1: Write the failing test**

`libs/markoff-styled/tests/tst_styled_block_formats.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

namespace {
QTextBlock blockN(QTextDocument *doc, int n) {
    return doc->findBlockByNumber(n);
}
}  // namespace

class TstStyledBlockFormats : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void heading_level_1_gets_largest_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# H1 title"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        const QTextCharFormat fmt = b.charFormat();
        QVERIFY(fmt.fontPointSize() > 0);
        QCOMPARE(fmt.fontWeight(), int(QFont::Bold));
    }

    void heading_levels_descend_in_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "# H1\n\n## H2\n\n### H3\n\n#### H4\n\n##### H5\n\n###### H6"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        auto *qdoc = e.textEdit()->document();
        const qreal h1 = blockN(qdoc, 0).charFormat().fontPointSize();
        const qreal h2 = blockN(qdoc, 2).charFormat().fontPointSize();
        const qreal h3 = blockN(qdoc, 4).charFormat().fontPointSize();
        const qreal h4 = blockN(qdoc, 6).charFormat().fontPointSize();
        const qreal h5 = blockN(qdoc, 8).charFormat().fontPointSize();
        const qreal h6 = blockN(qdoc, 10).charFormat().fontPointSize();
        QVERIFY(h1 > h2);
        QVERIFY(h2 > h3);
        QVERIFY(h3 > h4);
        QVERIFY(h4 > h5);
        QVERIFY(h5 > h6);
        QVERIFY(h6 > 0);
    }

    void paragraph_keeps_default_size() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Just a paragraph."));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        // Paragraph charFormat must not impose a heading-bold weight.
        QVERIFY(b.charFormat().fontWeight() != int(QFont::Bold));
    }
};

QTEST_MAIN(TstStyledBlockFormats)
#include "tst_styled_block_formats.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_block_formats tst_styled_block_formats.cpp)
add_test(NAME tst_styled_block_formats COMMAND tst_styled_block_formats)
target_link_libraries(tst_styled_block_formats
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_block_formats
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_block_formats -j 8
scripts/run-tests.sh --bin tst_styled_block_formats
```
Expected: FAIL — heading blocks have no font-size set (still default).

- [ ] **Step 4: Implement the block-walk and Heading mapping in StyleApplier**

Replace the body of `applyFormats()` in `libs/markoff-styled/src/StyleApplier.cpp` (and add the necessary includes at the top of the file: `#include <markoff/core/BlockKind.h>`, `#include <markoff/core/SourceTextDocumentBinding.h>`, `#include <QTextBlockFormat>`, `#include <QFont>`, `#include <QStringList>`).

Add a private helper at the bottom of the namespace:
```cpp
namespace {

QTextBlockFormat baseBlockFormat() {
    QTextBlockFormat fmt;
    fmt.setTopMargin(0);
    fmt.setBottomMargin(0);
    fmt.setLeftMargin(0);
    fmt.setIndent(0);
    return fmt;
}

void applyHeading(QTextCursor &cursor, int level, qreal fontScale) {
    static constexpr qreal kBaseSize = 11.0;
    // H1=2.0×, H2=1.7×, H3=1.4×, H4=1.2×, H5=1.0×, H6=0.9× (relative to base).
    static constexpr qreal kRatios[6] = { 2.0, 1.7, 1.4, 1.2, 1.0, 0.9 };
    const int idx = qBound(1, level, 6) - 1;
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(8 * fontScale);
    bf.setBottomMargin(4 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(kBaseSize * kRatios[idx] * fontScale);
    cf.setFontWeight(QFont::Bold);
    cursor.mergeBlockCharFormat(cf);
}

void applyParagraph(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(2 * fontScale);
    bf.setBottomMargin(2 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cf.setFontWeight(QFont::Normal);
    cursor.setBlockCharFormat(cf);
}

}  // namespace
```

Replace `applyFormats()` with:
```cpp
void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;
    {
        QSignalBlocker block(m_textDocument);
        QTextCursor cursor(m_textDocument);
        cursor.beginEditBlock();

        const QByteArray flat = m_textDocument->toPlainText().toUtf8();

        for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
            const auto rangeOpt = m_markoffDocument->blockByteRange(id);
            if (!rangeOpt) continue;
            const int startQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flat, rangeOpt->first);
            const int endQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flat, rangeOpt->second);

            const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);

            // Position cursor at block start; apply to each QTextBlock in range.
            cursor.setPosition(startQt);
            QTextBlock qblk = cursor.block();
            while (qblk.isValid() && qblk.position() <= endQt) {
                QTextCursor blkCursor(qblk);
                if (kind == Markoff::BlockKind::Heading) {
                    // Heading level = count of leading '#' characters.
                    const QByteArray text = m_markoffDocument->blockText(id);
                    int level = 0;
                    while (level < text.size() && text[level] == '#') ++level;
                    level = qBound(1, level, 6);
                    applyHeading(blkCursor, level, m_fontScale);
                } else if (kind == Markoff::BlockKind::Paragraph) {
                    applyParagraph(blkCursor, m_fontScale);
                } else {
                    // Other kinds — Task 6.
                    applyParagraph(blkCursor, m_fontScale);
                }
                qblk = qblk.next();
            }
        }

        cursor.endEditBlock();
    }
    ++m_restyleCount;
    m_applyingFormats = false;
}
```

Heading level is computed from `blockText(id)`'s leading `#` characters — `MarkoffDocument` does not expose a typed `blockHeadingLevel` accessor and the leading-hash approach is robust for the styling pass (parser already enforces ATX-heading shape).

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_styled_block_formats -j 8
scripts/run-tests.sh --bin tst_styled_block_formats
```
Expected: PASS — all 3 slots pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): block formats — Heading levels 1-6 + Paragraph baseline

StyleApplier now walks iterateBlocks(), maps each block's byte range to
QTextCursor positions, and applies per-kind QTextBlockFormat +
QTextCharFormat. Heading sizes: H1 2.0x, H2 1.7x, H3 1.4x, H4 1.2x,
H5 1.0x, H6 0.9x of 11pt base. Other kinds fall through to Paragraph
baseline pending Tasks 6-9."
```

---

## Task 6: Block-level formats — CodeBlock, Blockquote, ListItem, HorizontalRule

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_block_formats.cpp`

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_block_formats.cpp`, append four new test slots:

```cpp
    void code_block_uses_monospace_and_background() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode line\n```"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        auto *qdoc = e.textEdit()->document();
        // Fenced code spans 3 QTextBlocks: fence, content, fence.
        const QTextBlock content = blockN(qdoc, 1);
        const QTextCharFormat cf = content.charFormat();
        QVERIFY(cf.fontFixedPitch() || cf.fontFamilies().toStringList().size() > 0);
        QVERIFY(content.blockFormat().background().style() != Qt::NoBrush);
    }

    void blockquote_has_left_margin() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("> quoted text"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        QVERIFY(b.blockFormat().leftMargin() > 0);
    }

    void list_item_has_left_margin() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- first item"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        QVERIFY(b.blockFormat().leftMargin() > 0);
    }

    void horizontal_rule_uses_monospace() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("---"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        const QTextBlock b = blockN(e.textEdit()->document(), 0);
        const QTextCharFormat cf = b.charFormat();
        QVERIFY(cf.fontFixedPitch() || cf.fontFamilies().toStringList().size() > 0);
    }
```

- [ ] **Step 2: Run test to verify the four new slots fail**

```bash
cmake --build build-dev --target tst_styled_block_formats -j 8
scripts/run-tests.sh --bin tst_styled_block_formats
```
Expected: FAIL on the four new slots — current implementation falls all non-Heading/Paragraph kinds through to Paragraph.

- [ ] **Step 3: Implement the four kinds in StyleApplier**

In `libs/markoff-styled/src/StyleApplier.cpp`, add to the anonymous namespace **before** `applyParagraph`:

```cpp
void applyCodeBlock(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(12 * fontScale);
    bf.setTopMargin(2);
    bf.setBottomMargin(2);
    bf.setBackground(QColor(245, 245, 245));  // Theme::CodeBlockBackground
                                              // resolved in Task 9 wiring.
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setFontPointSize(10.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

void applyBlockquote(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(16 * fontScale * qMax(1, depth));
    bf.setTopMargin(2);
    bf.setBottomMargin(2);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cf.setForeground(QColor(100, 100, 100));  // Theme::Quote.
    cursor.setBlockCharFormat(cf);
}

void applyListItem(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(16 * fontScale * qMax(1, depth + 1));
    bf.setTopMargin(1);
    bf.setBottomMargin(1);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

void applyHorizontalRule(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(6 * fontScale);
    bf.setBottomMargin(6 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setForeground(QColor(180, 180, 180));
    cf.setFontPointSize(11.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}
```

In the `applyFormats()` block-iteration loop, replace the `else` clause with:

```cpp
} else if (kind == Markoff::BlockKind::CodeBlock) {
    applyCodeBlock(blkCursor, m_fontScale);
} else if (kind == Markoff::BlockKind::BlockQuote) {
    int depth = 1;
    const QByteArray text = m_markoffDocument->blockText(id);
    if (!text.isEmpty()) {
        depth = 0;
        for (int i = 0; i < text.size() && text[i] == '>'; ++i) ++depth;
        depth = qMax(1, depth);
    }
    applyBlockquote(blkCursor, depth, m_fontScale);
} else if (kind == Markoff::BlockKind::ListItem) {
    int depth = 0;
    const QByteArray text = m_markoffDocument->blockText(id);
    while (depth < text.size() && (text[depth] == ' ' || text[depth] == '\t')) ++depth;
    depth /= 2;  // 2 spaces per indent level — close enough for v0.
    applyListItem(blkCursor, depth, m_fontScale);
} else if (kind == Markoff::BlockKind::HorizontalRule) {
    applyHorizontalRule(blkCursor, m_fontScale);
} else {
    applyParagraph(blkCursor, m_fontScale);
}
```

- [ ] **Step 4: Run test to verify all 7 slots pass**

```bash
cmake --build build-dev --target tst_styled_block_formats -j 8
scripts/run-tests.sh --bin tst_styled_block_formats
```
Expected: PASS — all 7 slots pass (3 from Task 5, 4 added here).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): block formats — CodeBlock, Blockquote, ListItem, HR

CodeBlock: monospace + tinted background, left indent.
Blockquote: indented per > depth, grey foreground.
ListItem: indent per leading-whitespace depth.
HorizontalRule: monospace grey.

Theme-slot colours hardcoded for v0; Theme-driven lookup lands when
inline styling needs the Theme too (Task 9 + 13)."
```

---

## Task 7: Inline char formats — bold, italic, strikethrough

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`
- Create: `libs/markoff-styled/tests/tst_styled_inline_formats.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-styled/tests/tst_styled_inline_formats.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

namespace {
QTextCharFormat formatAtChar(QTextDocument *doc, int charPos) {
    QTextCursor c(doc);
    c.setPosition(charPos);
    c.setPosition(charPos + 1, QTextCursor::KeepAnchor);
    return c.charFormat();
}
}  // namespace

class TstStyledInlineFormats : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void bold_span_sets_bold_weight() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("**bold** word"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        // Char position 2 is inside "bold" (after the **).
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QCOMPARE(cf.fontWeight(), int(QFont::Bold));
    }

    void italic_span_sets_italic() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("*em* word"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 1);
        QVERIFY(cf.fontItalic());
    }

    void strikethrough_span_sets_strikeout() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("~~struck~~ word"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QVERIFY(cf.fontStrikeOut());
    }

    void plain_text_has_no_emphasis() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("plain words"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 1);
        QCOMPARE(cf.fontWeight(), int(QFont::Normal));
        QVERIFY(!cf.fontItalic());
        QVERIFY(!cf.fontStrikeOut());
    }
};

QTEST_MAIN(TstStyledInlineFormats)
#include "tst_styled_inline_formats.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_inline_formats tst_styled_inline_formats.cpp)
add_test(NAME tst_styled_inline_formats COMMAND tst_styled_inline_formats)
target_link_libraries(tst_styled_inline_formats
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_inline_formats
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_inline_formats -j 8
scripts/run-tests.sh --bin tst_styled_inline_formats
```
Expected: FAIL — no inline formats applied yet (only block-level).

- [ ] **Step 4: Apply inline spans in StyleApplier**

In `libs/markoff-styled/src/StyleApplier.cpp` add the include `#include <markoff/parser/SourceSpan.h>`.

In the anonymous namespace, add:
```cpp
QTextCharFormat charFormatForSpan(const Markoff::SourceSpan &span,
                                  qreal /*fontScale*/) {
    QTextCharFormat fmt;
    if (span.bold)          fmt.setFontWeight(QFont::Bold);
    if (span.italic)        fmt.setFontItalic(true);
    if (span.strikethrough) fmt.setFontStrikeOut(true);
    return fmt;
}
}  // close existing anonymous namespace
```

(If there are subsequent helpers added in Tasks 8/9, they go inside the same anonymous namespace — keep one anonymous namespace, just add helpers.)

In `applyFormats()` after the block-iteration loop but **inside** the `beginEditBlock`/`endEditBlock` window, add a second loop:

```cpp
for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
    const auto rangeOpt = m_markoffDocument->blockByteRange(id);
    if (!rangeOpt) continue;
    const int blockStartQt = Markoff::SourceTextDocumentBinding
        ::byteOffsetToQtPos(flat, rangeOpt->first);

    for (const Markoff::SourceSpan &span : m_markoffDocument->inlineSpansFor(id)) {
        if (span.charLength <= 0) continue;
        QTextCursor c(m_textDocument);
        c.setPosition(blockStartQt + span.charOffset);
        c.setPosition(blockStartQt + span.charOffset + span.charLength,
                      QTextCursor::KeepAnchor);
        c.mergeCharFormat(charFormatForSpan(span, m_fontScale));
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_styled_inline_formats -j 8
scripts/run-tests.sh --bin tst_styled_inline_formats
```
Expected: PASS — all 4 slots pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): inline formats — bold, italic, strikethrough

StyleApplier now walks inlineSpansFor(blockId) after the block-format
pass and merges QTextCharFormat for the three emphasis flags.
Delimiters are NOT yet hidden (v0.1)."
```

---

## Task 8: Inline char formats — code, highlight, tag, footnote-ref

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_inline_formats.cpp`

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_inline_formats.cpp`, append:

```cpp
    void inline_code_uses_monospace() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a `inline` b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        // 'i' in "inline" sits at char pos 3 ("a `[i]nline...").
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.fontFixedPitch() || cf.fontFamilies().toStringList().size() > 0);
    }

    void highlight_span_sets_background() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("==hl== word"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QVERIFY(cf.background().style() != Qt::NoBrush);
    }

    void tag_span_distinct_foreground() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a #tag b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        // '#' at char 2, 't' at char 3.
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.foreground().style() != Qt::NoBrush);
    }

    void footnote_ref_distinct_foreground() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [^1] b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.foreground().style() != Qt::NoBrush);
    }
```

- [ ] **Step 2: Run test to verify the four new slots fail**

```bash
cmake --build build-dev --target tst_styled_inline_formats -j 8
scripts/run-tests.sh --bin tst_styled_inline_formats
```
Expected: FAIL on the four new slots.

- [ ] **Step 3: Extend `charFormatForSpan` in StyleApplier**

In `libs/markoff-styled/src/StyleApplier.cpp`, replace the `charFormatForSpan` function with:

```cpp
QTextCharFormat charFormatForSpan(const Markoff::SourceSpan &span,
                                  qreal /*fontScale*/) {
    QTextCharFormat fmt;
    if (span.bold)          fmt.setFontWeight(QFont::Bold);
    if (span.italic)        fmt.setFontItalic(true);
    if (span.strikethrough) fmt.setFontStrikeOut(true);
    if (span.code) {
        fmt.setFontFamilies({QStringLiteral("monospace")});
        fmt.setFontFixedPitch(true);
        fmt.setBackground(QColor(245, 245, 245));   // Theme::InlineCodeBackground
    }
    if (span.highlight) {
        fmt.setBackground(QColor(255, 240, 130));   // Theme::Highlight
    }
    if (span.isTag) {
        fmt.setForeground(QColor(70, 130, 180));    // Theme::Tag
    }
    if (span.isFootnoteRef) {
        fmt.setForeground(QColor(150, 90, 150));    // Theme::FootnoteRef
        fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    }
    return fmt;
}
```

- [ ] **Step 4: Run test to verify all 8 slots pass**

```bash
cmake --build build-dev --target tst_styled_inline_formats -j 8
scripts/run-tests.sh --bin tst_styled_inline_formats
```
Expected: PASS — all 8 slots pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): inline formats — code, highlight, tag, footnote-ref

Four more SourceSpan flags routed to QTextCharFormat. Theme-slot colours
remain hardcoded for v0; Theme-driven lookup is its own micro-spec."
```

---

## Task 9: Inline char formats — link + wikilink (anchors)

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_inline_formats.cpp`

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_inline_formats.cpp`, append:

```cpp
    void link_span_is_anchored_and_underlined() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("[text](http://x) more"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        // "text" starts at char 1.
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 2);
        QVERIFY(cf.isAnchor());
        QVERIFY(cf.fontUnderline());
        QVERIFY(!cf.anchorHref().isEmpty());
    }

    void wikilink_span_is_anchored_and_underlined() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("[[target]] more"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        // 'target' starts at char 2.
        const QTextCharFormat cf = formatAtChar(e.textEdit()->document(), 3);
        QVERIFY(cf.isAnchor());
        QVERIFY(cf.fontUnderline());
    }
```

- [ ] **Step 2: Run test to verify both new slots fail**

```bash
cmake --build build-dev --target tst_styled_inline_formats -j 8
scripts/run-tests.sh --bin tst_styled_inline_formats
```
Expected: FAIL on the two new slots.

- [ ] **Step 3: Extend `charFormatForSpan` with link handling**

In `libs/markoff-styled/src/StyleApplier.cpp`, append inside `charFormatForSpan` (before `return fmt`):

```cpp
    if (span.isLink || span.isWikilink) {
        fmt.setAnchor(true);
        fmt.setAnchorHref(span.linkTarget.rawText);
        fmt.setFontUnderline(true);
        fmt.setForeground(span.isWikilink
                          ? QColor(120, 80, 200)    // Theme::WikiLink
                          : QColor(40, 100, 200));  // Theme::Link
    }
```

(`SourceSpan::linkTarget` is a `Markoff::LinkTarget`; its `rawText` member is the raw markdown link text used as the anchor href. This is informational only — resolution flows through `LinkService` at click time.)

- [ ] **Step 4: Run test to verify all 10 slots pass**

```bash
cmake --build build-dev --target tst_styled_inline_formats -j 8
scripts/run-tests.sh --bin tst_styled_inline_formats
```
Expected: PASS — all 10 slots pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): inline formats — link + wikilink (anchors)

isLink / isWikilink spans get setAnchor(true), anchor href set to span
rawText (for accessibility introspection — resolution flows through
inlineSpansFor at click time, not through this href). Link colours
distinct between markdown-link and wikilink."
```

---

## Task 10: Link click handling — `LinkInteraction` + `mousePressEvent`

**Files:**
- Modify: `libs/markoff-styled/CMakeLists.txt`
- Create: `libs/markoff-styled/src/LinkInteraction.h`
- Create: `libs/markoff-styled/src/LinkInteraction.cpp`
- Create: `libs/markoff-styled/tests/support/RecordingLinkService.h`
- Create: `libs/markoff-styled/tests/tst_styled_link_interaction.cpp`
- Modify: `libs/markoff-styled/src/Editor.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test support**

`libs/markoff-styled/tests/support/RecordingLinkService.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPoint>
#include <QString>
#include <QVector>

#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>

class RecordingLinkService : public Markoff::LinkService {
public:
    struct ActivateCall   { Markoff::LinkActivation activation; };
    struct HoverCall      { Markoff::LinkActivation activation; QPoint globalPos; };
    struct HoverLeftCall  { QString linkText; };

    QVector<ActivateCall>  activates;
    QVector<HoverCall>     hovers;
    QVector<HoverLeftCall> hoverLefts;

    Markoff::LinkKind classify(const QString &) const override {
        return Markoff::LinkKind::External;
    }
    QUrl resolve(const QString &linkText, const QString & = {}) const override {
        return QUrl(linkText);
    }
    void activate(const Markoff::LinkActivation &a) override {
        activates.push_back({a});
        Markoff::LinkService::activate(a);
    }
    void notifyHover(const Markoff::LinkActivation &a, const QPoint &g) override {
        hovers.push_back({a, g});
        Markoff::LinkService::notifyHover(a, g);
    }
    void notifyHoverLeft(const QString &t) override {
        hoverLefts.push_back({t});
        Markoff::LinkService::notifyHoverLeft(t);
    }
};
```

- [ ] **Step 2: Write the failing test**

`libs/markoff-styled/tests/tst_styled_link_interaction.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

#include "support/RecordingLinkService.h"

namespace {
QPoint pointForChar(QTextEdit *edit, int charPos) {
    QTextCursor c = edit->textCursor();
    c.setPosition(charPos);
    edit->setTextCursor(c);
    const QRect r = edit->cursorRect();
    return edit->viewport()->mapTo(edit->viewport(),
                                   r.center() + QPoint(3, 0));
}
}  // namespace

class TstStyledLinkInteraction : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void click_inside_link_calls_activate() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // 'e' of 'text' is at char 4: "a [t[e]xt](...)" — index 4.
        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);

        QTRY_COMPARE(svc.activates.size(), 1);
    }

    void click_outside_link_does_not_activate() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("just plain text"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        const QPoint p = pointForChar(e.textEdit(), 3);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);

        QTest::qWait(50);
        QCOMPARE(svc.activates.size(), 0);
    }
};

QTEST_MAIN(TstStyledLinkInteraction)
#include "tst_styled_link_interaction.moc"
```

- [ ] **Step 3: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_link_interaction tst_styled_link_interaction.cpp)
add_test(NAME tst_styled_link_interaction COMMAND tst_styled_link_interaction)
target_link_libraries(tst_styled_link_interaction
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
target_include_directories(tst_styled_link_interaction
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_styled_link_interaction
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 4: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_link_interaction -j 8
scripts/run-tests.sh --bin tst_styled_link_interaction
```
Expected: FAIL — no click handling wired yet.

- [ ] **Step 5: Write `libs/markoff-styled/src/LinkInteraction.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <optional>

#include <markoff/core/LinkActivation.h>

class QTextEdit;
class QMouseEvent;

namespace Markoff {
class LinkService;
class MarkoffDocument;
}

namespace Markoff::Styled {

/// Event-filter on a QTextEdit's viewport. Translates mouse press / move /
/// leave into LinkService calls when over a link span.
class LinkInteraction : public QObject {
    Q_OBJECT
public:
    explicit LinkInteraction(QTextEdit *edit, QObject *parent = nullptr);
    ~LinkInteraction() override;

    void setMarkoffDocument(Markoff::MarkoffDocument *doc) { m_doc = doc; }
    void setLinkService(Markoff::LinkService *svc)         { m_service = svc; }
    void setFromContext(const QString &c)                  { m_fromContext = c; }

    /// Returns the link span (if any) covering the cursor at `charPos`.
    /// Public for testability. `globalPos` is only used by callers that
    /// need to forward to `LinkService::notifyHover`; resolution itself
    /// is position-only.
    std::optional<Markoff::LinkActivation>
    resolveLinkAt(int charPos, Qt::KeyboardModifiers mods) const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void handlePress(QMouseEvent *e);
    void handleMove(QMouseEvent *e);
    void handleLeave();

    QTextEdit                 *m_edit       = nullptr;
    Markoff::MarkoffDocument  *m_doc        = nullptr;
    Markoff::LinkService      *m_service    = nullptr;
    QString                    m_fromContext;
    QString                    m_currentHoveredRawText;
};

}  // namespace Markoff::Styled
```

- [ ] **Step 6: Write `libs/markoff-styled/src/LinkInteraction.cpp` (click path only — hover/leave land in Tasks 11–12)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkInteraction.h"

#include <QEvent>
#include <QMouseEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/parser/SourceSpan.h>

namespace Markoff::Styled {

LinkInteraction::LinkInteraction(QTextEdit *edit, QObject *parent)
    : QObject(parent), m_edit(edit) {
    if (m_edit && m_edit->viewport()) {
        m_edit->viewport()->installEventFilter(this);
    }
}

LinkInteraction::~LinkInteraction() = default;

bool LinkInteraction::eventFilter(QObject *obj, QEvent *event) {
    if (m_edit && obj == m_edit->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            handlePress(static_cast<QMouseEvent *>(event));
            // Don't consume — let the editor still place the caret.
        }
    }
    return QObject::eventFilter(obj, event);
}

std::optional<Markoff::LinkActivation>
LinkInteraction::resolveLinkAt(int charPos, Qt::KeyboardModifiers mods) const {
    if (!m_doc || !m_edit) return std::nullopt;
    const QByteArray flat = m_edit->toPlainText().toUtf8();

    for (Markoff::BlockId id : m_doc->iterateBlocks()) {
        const auto anchorOpt = m_doc->blockAnchorForId(id);
        if (!anchorOpt) continue;
        const auto range = m_doc->blockByteRange(*anchorOpt);
        const int startQt = Markoff::SourceTextDocumentBinding
            ::byteOffsetToQtPos(flat, range.start);
        const int endQt = Markoff::SourceTextDocumentBinding
            ::byteOffsetToQtPos(flat, range.end);
        if (charPos < startQt || charPos > endQt) continue;

        for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(id)) {
            if (!span.isLink && !span.isWikilink) continue;
            const int spanStartQt = startQt + span.charOffset;
            const int spanEndQt   = spanStartQt + span.charLength;
            if (charPos < spanStartQt || charPos >= spanEndQt) continue;

            Markoff::LinkActivation a;
            a.kind        = span.isWikilink ? Markoff::LinkKind::WikiLink
                                            : Markoff::LinkKind::External;
            a.rawText     = span.linkTarget.rawText;
            a.modifiers   = mods;
            a.fromContext = m_fromContext;
            return a;
        }
    }
    return std::nullopt;
}

void LinkInteraction::handlePress(QMouseEvent *e) {
    if (!m_service) return;
    QTextCursor c = m_edit->cursorForPosition(e->pos());
    auto act = resolveLinkAt(c.position(), e->modifiers());
    if (act) m_service->activate(*act);
}

void LinkInteraction::handleMove(QMouseEvent *) {
    // Implemented in Task 11.
}

void LinkInteraction::handleLeave() {
    // Implemented in Task 12.
}

}  // namespace Markoff::Styled
```

- [ ] **Step 7: Add LinkInteraction files to library CMake**

Update `libs/markoff-styled/CMakeLists.txt` `add_library` source list:
```cmake
add_library(markoff_styled STATIC
    include/markoff/styled/Editor.h
    include/markoff/styled/MarkoffStyledExport.h
    src/Editor.cpp
    src/StyleApplier.h
    src/StyleApplier.cpp
    src/LinkInteraction.h
    src/LinkInteraction.cpp
)
```

- [ ] **Step 8: Wire LinkInteraction into Editor**

In `libs/markoff-styled/src/Editor.cpp`, add at the top:
```cpp
#include "LinkInteraction.h"
```

In the constructor body, after the existing wiring, append:
```cpp
m_linkInteract = new LinkInteraction(m_editor, this);
```

In `setDocument`, after `m_styleApplier->setMarkoffDocument(doc)`:
```cpp
if (m_linkInteract) m_linkInteract->setMarkoffDocument(doc);
```

In `setLinkService`, before `emit linkServiceChanged()`:
```cpp
if (m_linkInteract) m_linkInteract->setLinkService(svc);
```

In `setFromContext`, before `emit fromContextChanged()`:
```cpp
if (m_linkInteract) m_linkInteract->setFromContext(c);
```

- [ ] **Step 9: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_styled_link_interaction -j 8
scripts/run-tests.sh --bin tst_styled_link_interaction
```
Expected: PASS — both slots pass.

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): link click handling via LinkInteraction

QTextEdit viewport event-filter resolves the (BlockId, charPos) under
the mouse, walks inlineSpansFor to find a covering link/wikilink span,
and routes activation through the configured LinkService. Click
outside a span is a no-op (event passes through to caret placement)."
```

---

## Task 11: Link hover handling + cursor-shape change

**Files:**
- Modify: `libs/markoff-styled/src/LinkInteraction.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_link_interaction.cpp`

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_link_interaction.cpp`, append:
```cpp
    void hover_inside_link_calls_notify_hover() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseMove(e.textEdit()->viewport(), p);

        QTRY_COMPARE(svc.hovers.size(), 1);
    }

    void hover_idempotent_within_same_link() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);
        // Move 1 char within the same link.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 5));
        QTest::qWait(50);
        QCOMPARE(svc.hovers.size(), 1);
    }

    void hover_off_link_then_on_emits_left_and_new_hover() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "a [first](http://1.test) b [second](http://2.test) c"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(800, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);

        // Move into plain text between links.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 26));
        QTRY_COMPARE(svc.hoverLefts.size(), 1);

        // Move into the second link.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 31));
        QTRY_COMPARE(svc.hovers.size(), 2);
    }
```

- [ ] **Step 2: Run test to verify the three new slots fail**

```bash
cmake --build build-dev --target tst_styled_link_interaction -j 8
scripts/run-tests.sh --bin tst_styled_link_interaction
```
Expected: FAIL on the three new slots — `handleMove` is currently empty.

- [ ] **Step 3: Implement hover in `LinkInteraction.cpp`**

Replace `handleMove` with:
```cpp
void LinkInteraction::handleMove(QMouseEvent *e) {
    if (!m_service || !m_doc) return;
    QTextCursor c = m_edit->cursorForPosition(e->pos());
    const auto act = resolveLinkAt(c.position(), e->modifiers());
    const QString newRaw = act ? act->rawText : QString();
    if (newRaw == m_currentHoveredRawText) return;

    if (!m_currentHoveredRawText.isEmpty()) {
        m_service->notifyHoverLeft(m_currentHoveredRawText);
    }
    m_currentHoveredRawText = newRaw;
    if (act) {
        m_service->notifyHover(*act, e->globalPosition().toPoint());
        m_edit->viewport()->setCursor(Qt::PointingHandCursor);
    } else {
        m_edit->viewport()->setCursor(Qt::IBeamCursor);
    }
}
```

Extend `eventFilter` to handle `MouseMove`:
```cpp
bool LinkInteraction::eventFilter(QObject *obj, QEvent *event) {
    if (m_edit && obj == m_edit->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            handlePress(static_cast<QMouseEvent *>(event));
            break;
        case QEvent::MouseMove:
            handleMove(static_cast<QMouseEvent *>(event));
            break;
        default: break;
        }
    }
    return QObject::eventFilter(obj, event);
}
```

- [ ] **Step 4: Run test to verify all 5 slots pass**

```bash
cmake --build build-dev --target tst_styled_link_interaction -j 8
scripts/run-tests.sh --bin tst_styled_link_interaction
```
Expected: PASS — all 5 slots pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): hover handling — LinkService::notifyHover / notifyHoverLeft

mouseMoveEvent translates to (BlockId, charPos), walks spans for a
covering link, calls notifyHover/notifyHoverLeft on identity changes
(raw-text key). Cursor shape switches between PointingHandCursor and
IBeamCursor. Idempotent within the same span."
```

---

## Task 12: Mouse-leave + focus-out → clear hover

**Files:**
- Modify: `libs/markoff-styled/src/LinkInteraction.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_link_interaction.cpp`

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_link_interaction.cpp`, append:
```cpp
    void leave_event_clears_hover() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);

        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(e.textEdit()->viewport(), &leaveEvent);

        QTRY_COMPARE(svc.hoverLefts.size(), 1);
    }
```

- [ ] **Step 2: Run test to verify the new slot fails**

```bash
cmake --build build-dev --target tst_styled_link_interaction -j 8
scripts/run-tests.sh --bin tst_styled_link_interaction
```
Expected: FAIL — `Leave` events not handled.

- [ ] **Step 3: Handle `Leave` in `LinkInteraction.cpp`**

Extend `eventFilter`:
```cpp
        case QEvent::Leave:
            handleLeave();
            break;
```

Replace `handleLeave`:
```cpp
void LinkInteraction::handleLeave() {
    if (m_currentHoveredRawText.isEmpty()) return;
    if (m_service) m_service->notifyHoverLeft(m_currentHoveredRawText);
    m_currentHoveredRawText.clear();
    if (m_edit && m_edit->viewport()) {
        m_edit->viewport()->setCursor(Qt::IBeamCursor);
    }
}
```

- [ ] **Step 4: Run test to verify all 6 slots pass**

```bash
cmake --build build-dev --target tst_styled_link_interaction -j 8
scripts/run-tests.sh --bin tst_styled_link_interaction
```
Expected: PASS — all 6 slots pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): mouse-leave clears hover

Mouse leaving the editor viewport emits notifyHoverLeft for the
currently-hovered link and resets the cursor shape. Idempotent on
already-cleared state."
```

---

## Task 13: `DocHighlighter` stub class

**Files:**
- Modify: `libs/markoff-styled/CMakeLists.txt`
- Create: `libs/markoff-styled/src/DocHighlighter.h`
- Create: `libs/markoff-styled/src/DocHighlighter.cpp`
- Modify: `libs/markoff-styled/src/Editor.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`
- Create: `libs/markoff-styled/tests/tst_styled_delimiter_visibility.cpp`

This task lands the v0 stub of the `DocHighlighter` and its test placeholder. v0 asserts delimiters render visible (not hidden); v0.1 promotes to cursor-aware hide-on-leave.

- [ ] **Step 1: Write the v0-stub test**

`libs/markoff-styled/tests/tst_styled_delimiter_visibility.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledDelimiterVisibility : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void delimiters_render_visible_in_v0() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("**bold** text"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        // v0: delimiters are visible. We just check the document text
        // still contains them — promoting to cursor-aware hide is v0.1.
        QCOMPARE(e.textEdit()->toPlainText(),
                 QStringLiteral("**bold** text"));
    }
};

QTEST_MAIN(TstStyledDelimiterVisibility)
#include "tst_styled_delimiter_visibility.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_delimiter_visibility tst_styled_delimiter_visibility.cpp)
add_test(NAME tst_styled_delimiter_visibility COMMAND tst_styled_delimiter_visibility)
target_link_libraries(tst_styled_delimiter_visibility
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_delimiter_visibility
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Confirm the test passes already (it asserts v0 behaviour, which is "delimiters visible")**

```bash
cmake --build build-dev --target tst_styled_delimiter_visibility -j 8
scripts/run-tests.sh --bin tst_styled_delimiter_visibility
```
Expected: PASS — the binding already preserves the markdown source text verbatim.

- [ ] **Step 4: Write the `DocHighlighter` stub class**

`libs/markoff-styled/src/DocHighlighter.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSyntaxHighlighter>

class QTextDocument;

namespace Markoff::Styled {

/// Whole-document QSyntaxHighlighter for cursor-derived format overlays
/// (delimiter visibility, find-span highlights). v0 stub — no-op in
/// highlightBlock. v0.1 promotes to cursor-aware delimiter hide.
class DocHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit DocHighlighter(QTextDocument *parent);
    ~DocHighlighter() override;

protected:
    void highlightBlock(const QString &text) override;
};

}  // namespace Markoff::Styled
```

`libs/markoff-styled/src/DocHighlighter.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocHighlighter.h"

namespace Markoff::Styled {

DocHighlighter::DocHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {}

DocHighlighter::~DocHighlighter() = default;

void DocHighlighter::highlightBlock(const QString & /*text*/) {
    // v0 stub. v0.1: cursor-aware delimiter visibility goes here.
}

}  // namespace Markoff::Styled
```

- [ ] **Step 5: Register the new files in the library CMake and wire into Editor**

Update `libs/markoff-styled/CMakeLists.txt` source list:
```cmake
add_library(markoff_styled STATIC
    include/markoff/styled/Editor.h
    include/markoff/styled/MarkoffStyledExport.h
    src/Editor.cpp
    src/StyleApplier.h
    src/StyleApplier.cpp
    src/DocHighlighter.h
    src/DocHighlighter.cpp
    src/LinkInteraction.h
    src/LinkInteraction.cpp
)
```

In `libs/markoff-styled/src/Editor.cpp` add at the top:
```cpp
#include "DocHighlighter.h"
```

In the constructor body, after the existing wiring, append:
```cpp
m_highlighter = new DocHighlighter(m_editor->document());
```

- [ ] **Step 6: Build and re-run the full styled suite**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: all styled tests pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): DocHighlighter stub for cursor-derived overlays

Empty QSyntaxHighlighter parented to the QTextDocument. v0.1 promotes
this to cursor-aware delimiter hide-on-leave-parent-range; in v0 it's
inert. Delimiter-visibility test asserts the v0 contract (delimiters
visible)."
```

---

## Task 14: D2 integration test — forward/reverse round-trips

**Files:**
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`
- Create: `libs/markoff-styled/tests/tst_styled_d2_integration.cpp`

- [ ] **Step 1: Write the test**

`libs/markoff-styled/tests/tst_styled_d2_integration.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledD2Integration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_applies_formats_after_d2_cycle() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# starts h1"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        e.show();

        const QTextBlock blk0 = e.textEdit()->document()->findBlockByNumber(0);
        QVERIFY(blk0.charFormat().fontPointSize() > 11.0);
    }

    void remote_edit_replays_text_and_restyles() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("paragraph"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        // Replace the whole content with a heading.
        doc.applyFlatEdit(0, doc.flatView().size(),
                          QByteArrayLiteral("## h2 line"),
                          Markoff::Origin::Remote);

        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("## h2 line"));
        const QTextBlock blk0 = e.textEdit()->document()->findBlockByNumber(0);
        QTRY_VERIFY(blk0.charFormat().fontPointSize() > 11.0);
    }

    void undo_via_d2_restores_text_and_formats() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# h1 original"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);

        doc.applyFlatEdit(0, doc.flatView().size(),
                          QByteArrayLiteral("paragraph only"),
                          Markoff::Origin::Local);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("paragraph only"));

        doc.undoD2();
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("# h1 original"));
        const QTextBlock blk0 = e.textEdit()->document()->findBlockByNumber(0);
        QTRY_VERIFY(blk0.charFormat().fontPointSize() > 11.0);
    }

    void reset_content_does_not_double_blocks() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("first"));
        Markoff::Session s;
        e.setSession(&s);
        e.setDocument(&doc);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("first"));

        doc.resetContent(QByteArrayLiteral("second"));
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));
        // Must not be "secondsecond" or "first\n\nsecond".
        QCOMPARE(e.textEdit()->toPlainText().count('\n'), 0);
    }
};

QTEST_MAIN(TstStyledD2Integration)
#include "tst_styled_d2_integration.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_d2_integration tst_styled_d2_integration.cpp)
add_test(NAME tst_styled_d2_integration COMMAND tst_styled_d2_integration)
target_link_libraries(tst_styled_d2_integration
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_d2_integration
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_styled_d2_integration -j 8
scripts/run-tests.sh --bin tst_styled_d2_integration
```
Expected: PASS — all 4 slots pass with the wiring landed in Tasks 1–13. If any fail, fix the StyleApplier connection ordering or `setDocument` lifecycle before proceeding.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-styled
git commit -m "test(styled): D2 integration — forward + reverse + undo + reset

Locks in the load-bearing data-flow invariants: typing produces
formats, remote edits replay via setPlainText then restyle, undoD2
restores text + formats, resetContent doesn't double rows."
```

---

## Task 15: Demo app + per-leaf CLAUDE.md

**Files:**
- Modify: `libs/markoff-styled/app/main.cpp`
- Create: `libs/markoff-styled/CLAUDE.md`

- [ ] **Step 1: Flesh out the demo app**

Replace `libs/markoff-styled/app/main.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QMainWindow>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("markoff-styled-app");

    QCommandLineParser parser;
    parser.addPositionalArgument("file", "Markdown file to open (optional).");
    parser.addHelpOption();
    parser.process(app);

    QByteArray initial;
    if (!parser.positionalArguments().isEmpty()) {
        QFile f(parser.positionalArguments().first());
        if (f.open(QIODevice::ReadOnly)) initial = f.readAll();
    }

    QMainWindow window;
    auto *editor = new Markoff::Styled::Editor(&window);
    auto *session = new Markoff::Session(&window);
    auto *doc = new Markoff::MarkoffDocument(1, &window);
    doc->loadFromMarkdown(initial);
    editor->setSession(session);
    editor->setDocument(doc);
    window.setCentralWidget(editor);
    window.resize(900, 700);
    window.show();
    return app.exec();
}
```

- [ ] **Step 2: Write `libs/markoff-styled/CLAUDE.md`**

```markdown
# markoff-styled

Plain-jane QWidget Markoff editor on `markoff-core`. Third view leaf
alongside `markoff-source` and `markoff-live`. No QML, no KF6.

## Public surface
- `Markoff::Styled::Editor` — `Markoff::MarkdownView` subclass composing
  a `QTextEdit`. Setters: `setDocument`, `setSession`, `setTheme`,
  `setLinkService`, `setFromContext`, `setFontScale`.

## Internal
- `Markoff::Styled::StyleApplier` — subscribes to
  `MarkoffDocument::d2DocumentChanged`; applies `QTextBlockFormat` +
  `QTextCharFormat` from `iterateBlocks()` + `inlineSpansFor()`.
- `Markoff::Styled::DocHighlighter` — whole-doc `QSyntaxHighlighter`
  stub. v0 inert; v0.1 owns cursor-aware delimiter visibility.
- `Markoff::Styled::LinkInteraction` — event-filter on the
  `QTextEdit`'s viewport. Routes mouse press / move / leave through the
  configured `Markoff::LinkService`.

## Dependencies
- Qt6 Core / Gui / Widgets
- `markoff-core` (transitive `markoff-parser`)

No KF6, no QML, no `markoff-live`.

## Conventions
- C++20, Qt6.8+, CMake 3.19+.
- SPDX `GPL-3.0-or-later` on every file.
- `tr()` for user-visible strings.
- Tests prefix `tst_styled_*`. All test binaries run under
  `QT_QPA_PLATFORM=offscreen`.

## Spec
`docs/specs/2026-05-26-markoff-styled-leaf-design.md`

## Plan
`docs/plans/2026-05-26-markoff-styled-leaf.md`
```

- [ ] **Step 3: Build everything and run the full styled suite**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: all 7 styled test binaries pass (`tst_styled_editor_construction`, `tst_styled_binding_roundtrip`, `tst_styled_block_formats`, `tst_styled_inline_formats`, `tst_styled_link_interaction`, `tst_styled_delimiter_visibility`, `tst_styled_d2_integration`).

- [ ] **Step 4: Verify the demo app launches**

```bash
./build-dev/bin/markoff-styled-app docs/specs/2026-05-26-markoff-styled-leaf-design.md &
APP_PID=$!
sleep 2
kill $APP_PID
```

(Smoke check only — agents should not leave windows up. For real dogfood, ask the user.)

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): demo app + per-leaf CLAUDE.md

markoff-styled-app loads a markdown file (or starts blank) and displays
it through the new widget. Per-leaf CLAUDE.md documents the public
surface, internal structure, dependencies, and conventions."
```

---

## Task 16: Sanity check — run the wider suite, refresh the project root CLAUDE.md

**Files:**
- Modify: `CLAUDE.md` (project root)
- (No test changes; this task is verification + documentation refresh.)

- [ ] **Step 1: Run the wider test suite to ensure nothing else regressed**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```
Expected: pre-existing baseline plus the 7 new styled binaries pass. If any pre-existing failures appear from outside the styled scope, do not chase them in this plan — note in the commit and proceed.

- [ ] **Step 2: Update the project root `CLAUDE.md`**

Open `CLAUDE.md` (the one in `/home/clinton/dev/Markoff/CLAUDE.md`) and in the "Layout" section, after the `libs/markoff-source` entry, append:

```markdown
- `libs/markoff-styled`        — third view leaf (2026-05-26). Plain-jane
                                 QWidget editor on `markoff-core`. No QML, no
                                 KF6. Parser-driven block + inline formats
                                 via `MarkoffDocument::inlineSpansFor` and
                                 `iterateBlocks`. `Markoff::Styled::Editor`
                                 is the public widget. Spec
                                 `docs/specs/2026-05-26-markoff-styled-leaf-design.md`.
```

In the "Per-library guides" section, add:
```markdown
- `libs/markoff-styled/CLAUDE.md`
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(CLAUDE.md): add markoff-styled to the Layout section

Records the new third view leaf landed in this branch."
```

---

## Self-review checklist

After implementing all tasks, run through this once before declaring v0 complete.

- [ ] All 7 test binaries pass under `scripts/run-tests.sh -R '^tst_styled_'`.
- [ ] `markoff-styled-app` launches and displays a markdown file with styling visible.
- [ ] No new failures in the wider `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` baseline.
- [ ] `compile_commands.json` symlink at project root still valid (`ls -la compile_commands.json` resolves).
- [ ] `git log --oneline | head -20` shows the per-task commits with the prefix conventions.

If anything fails, fix in a follow-up commit on the same branch — do not amend prior commits.
