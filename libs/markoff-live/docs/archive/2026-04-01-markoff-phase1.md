# Markoff Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Markoff, a standalone Qt6/C++ markdown library with a forked text editor widget, MD4C-based parsing, reading view renderer, test application, and Corbomite adapter.

**Architecture:** Markoff is an independent library inside Corbomite's monorepo. It forks Qt's `QPlainTextEdit` + `QWidgetTextControl` for the editor widget, uses MD4C for parsing, and renders to `QTextDocument` for display. A thin adapter in Corbomite bridges Markoff's API to the existing `MarkdownRenderEngine` interface.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets), KDE Frameworks 6 (SyntaxHighlighting, I18n), MD4C (system library), JKQTMathText, CMake.

**Spec:** `libs/markoff/docs/specs/2026-04-01-markoff-phase1-design.md`

---

## File Structure

### New Files (Markoff library)

| File | Responsibility |
|------|---------------|
| `libs/markoff/CMakeLists.txt` | Library build, test enable, app subdirectory |
| `libs/markoff/include/markoff/Document.h` | Parsed markdown document (opaque AST) |
| `libs/markoff/include/markoff/RenderSettings.h` | Rendering configuration struct |
| `libs/markoff/include/markoff/Renderer.h` | AST → QTextDocument rendering |
| `libs/markoff/include/markoff/ReadingView.h` | Reading mode widget |
| `libs/markoff/include/markoff/Editor.h` | Forked text editor widget (public API) |
| `libs/markoff/src/Document.cpp` | Document implementation |
| `libs/markoff/src/DocumentBuilder.cpp` | MD4C SAX callbacks → AST |
| `libs/markoff/src/DocumentBuilder_p.h` | Internal builder header |
| `libs/markoff/src/Renderer.cpp` | Renderer implementation |
| `libs/markoff/src/ReadingView.cpp` | ReadingView implementation |
| `libs/markoff/src/Editor.cpp` | Forked from `qplaintextedit.cpp` |
| `libs/markoff/src/Editor_p.h` | Forked from `qplaintextedit_p.h` |
| `libs/markoff/src/TextControl.h` | Forked from `qwidgettextcontrol_p.h` |
| `libs/markoff/src/TextControl_p.h` | Forked from `qwidgettextcontrol_p_p.h` |
| `libs/markoff/src/TextControl.cpp` | Forked from `qwidgettextcontrol.cpp` |
| `libs/markoff/tests/CMakeLists.txt` | Test targets |
| `libs/markoff/tests/tst_document.cpp` | Document parsing tests |
| `libs/markoff/tests/tst_renderer.cpp` | Renderer output tests |
| `libs/markoff/app/CMakeLists.txt` | Test app build |
| `libs/markoff/app/main.cpp` | Test app entry point |
| `libs/markoff/app/MainWindow.h` | Test app window header |
| `libs/markoff/app/MainWindow.cpp` | Test app window implementation |

### New Files (Corbomite adapter)

| File | Responsibility |
|------|---------------|
| `libs/core/include/corbomite/core/MarkoffRenderEngine.h` | Adapter header |
| `libs/core/src/MarkoffRenderEngine.cpp` | Adapter implementation |

### Modified Files

| File | Change |
|------|--------|
| `CMakeLists.txt` (root) | Add `add_subdirectory(libs/markoff)` |
| `libs/core/CMakeLists.txt` | Add `MarkoffRenderEngine.cpp`, link to `markoff` |

---

## Task 1: CMake Skeleton and Empty Library

**Files:**
- Create: `libs/markoff/CMakeLists.txt`
- Create: `libs/markoff/include/markoff/RenderSettings.h`
- Modify: `CMakeLists.txt` (root)

This task establishes that the library builds. RenderSettings is a pure struct with no dependencies — the simplest header to start with.

- [ ] **Step 1: Create the library CMakeLists.txt**

```cmake
# libs/markoff/CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(markoff VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C REQUIRED IMPORTED_TARGET md4c)

add_library(markoff STATIC
    src/Document.cpp
)
set_target_properties(markoff PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(markoff
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(markoff
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets
    PRIVATE PkgConfig::MD4C KF6::SyntaxHighlighting jkqtmathtext
)

add_subdirectory(tests)
add_subdirectory(app)
```

- [ ] **Step 2: Create RenderSettings.h**

```cpp
// libs/markoff/include/markoff/RenderSettings.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_RENDERSETTINGS_H
#define MARKOFF_RENDERSETTINGS_H

namespace Markoff {

struct RenderSettings {
    int baseFontSizePt = 14;
    int maxWidthPx = 0;        // 0 = fill container
    int marginPx = 16;
    bool showFrontmatter = false;
    bool renderImages = true;
    bool renderCodeHighlighting = true;
};

} // namespace Markoff

#endif // MARKOFF_RENDERSETTINGS_H
```

- [ ] **Step 3: Create a minimal Document.h and Document.cpp stub**

```cpp
// libs/markoff/include/markoff/Document.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_DOCUMENT_H
#define MARKOFF_DOCUMENT_H

#include <QString>
#include <memory>

namespace Markoff {

class Document {
public:
    ~Document();

    static std::unique_ptr<Document> fromMarkdown(const QString &markdown);

    QString sourceText() const;
    bool isEmpty() const;
    QString extractSubpath(const QString &subpath) const;

private:
    Document();
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_DOCUMENT_H
```

```cpp
// libs/markoff/src/Document.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/Document.h"

namespace Markoff {

struct Document::Private {
    QString source;
};

Document::Document() : d(std::make_unique<Private>()) {}
Document::~Document() = default;

std::unique_ptr<Document> Document::fromMarkdown(const QString &markdown)
{
    auto doc = std::unique_ptr<Document>(new Document());
    doc->d->source = markdown;
    return doc;
}

QString Document::sourceText() const { return d->source; }
bool Document::isEmpty() const { return d->source.isEmpty(); }
QString Document::extractSubpath(const QString &) const { return {}; }

} // namespace Markoff
```

- [ ] **Step 4: Create empty test and app CMakeLists.txt stubs**

```cmake
# libs/markoff/tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
```

```cmake
# libs/markoff/app/CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
```

- [ ] **Step 5: Add markoff to root CMakeLists.txt**

In `CMakeLists.txt` (root), add after the `add_subdirectory(libs/mmdr)` line:

```cmake
add_subdirectory(libs/markoff)
```

- [ ] **Step 6: Build and verify**

Run: `cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON && cmake --build build --target markoff`

Expected: Compiles successfully. The `markoff` static library target exists.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/CMakeLists.txt libs/markoff/include/markoff/RenderSettings.h \
  libs/markoff/include/markoff/Document.h libs/markoff/src/Document.cpp \
  libs/markoff/tests/CMakeLists.txt libs/markoff/app/CMakeLists.txt CMakeLists.txt
git commit -m "feat(markoff): add CMake skeleton and stub Document class"
```

---

## Task 2: Document Parsing with MD4C

**Files:**
- Modify: `libs/markoff/src/Document.cpp`
- Create: `libs/markoff/src/DocumentBuilder_p.h`
- Create: `libs/markoff/src/DocumentBuilder.cpp`
- Modify: `libs/markoff/CMakeLists.txt` (add DocumentBuilder.cpp)
- Create: `libs/markoff/tests/tst_document.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt`

- [ ] **Step 1: Write the document test**

```cpp
// libs/markoff/tests/tst_document.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "markoff/Document.h"

class TestDocument : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testEmptyDocument()
    {
        auto doc = Markoff::Document::fromMarkdown(QString());
        QVERIFY(doc);
        QVERIFY(doc->isEmpty());
    }

    void testNonEmptyDocument()
    {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("# Hello"));
        QVERIFY(doc);
        QVERIFY(!doc->isEmpty());
        QCOMPARE(doc->sourceText(), QStringLiteral("# Hello"));
    }

    void testExtractSubpathHeading()
    {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("# Intro\nFirst paragraph.\n## Details\nSecond paragraph.\n## Other\nThird."));
        QString section = doc->extractSubpath(QStringLiteral("#Details"));
        QVERIFY(section.contains(QStringLiteral("Second paragraph")));
        QVERIFY(!section.contains(QStringLiteral("Third")));
    }

    void testExtractSubpathBlockId()
    {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("First line.\n\nTarget paragraph. ^myblock\n\nAfter."));
        QString section = doc->extractSubpath(QStringLiteral("#^myblock"));
        QVERIFY(section.contains(QStringLiteral("Target paragraph")));
        QVERIFY(!section.contains(QStringLiteral("After")));
    }

    void testExtractSubpathNotFound()
    {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("# Hello\nWorld"));
        QVERIFY(doc->extractSubpath(QStringLiteral("#Nonexistent")).isEmpty());
    }
};

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
```

- [ ] **Step 2: Add test to CMakeLists.txt**

Update `libs/markoff/tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_executable(tst_markoff_document tst_document.cpp)
add_test(NAME tst_markoff_document COMMAND tst_markoff_document)
target_link_libraries(tst_markoff_document PRIVATE Qt6::Test markoff)
set_tests_properties(tst_markoff_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run tests to verify they fail (subpath tests should fail)**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target tst_markoff_document && ctest -R tst_markoff_document --output-on-failure`

Expected: `testEmptyDocument` and `testNonEmptyDocument` PASS (stub works). `testExtractSubpathHeading`, `testExtractSubpathBlockId` FAIL (extractSubpath returns empty).

- [ ] **Step 4: Create DocumentBuilder**

```cpp
// libs/markoff/src/DocumentBuilder_p.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_DOCUMENTBUILDER_P_H
#define MARKOFF_DOCUMENTBUILDER_P_H

#include <QString>
#include <QList>
#include <md4c.h>

namespace Markoff {

struct InlineRun {
    QString text;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    bool math = false;
    bool mathDisplay = false;
    QString linkHref;
    QString wikiTarget;
};

struct Block {
    MD_BLOCKTYPE type = MD_BLOCK_P;
    int headingLevel = 0;
    QList<InlineRun> inlines;
    QString codeInfo;           // language for code blocks
    MD_ALIGN tableAlign = MD_ALIGN_DEFAULT;
    bool isTaskItem = false;
    MD_CHAR taskMark = ' ';
    int listStart = 1;          // for ordered lists
    bool isTightList = false;
    QList<Block> children;      // for nested structures (lists, tables)
};

class DocumentBuilder {
public:
    DocumentBuilder();

    bool parse(const QString &markdown);
    QList<Block> takeBlocks();

private:
    // MD4C callbacks
    static int onEnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata);
    static int onLeaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata);
    static int onEnterSpan(MD_SPANTYPE type, void *detail, void *userdata);
    static int onLeaveSpan(MD_SPANTYPE type, void *detail, void *userdata);
    static int onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata);

    // Instance methods called by static callbacks
    int enterBlock(MD_BLOCKTYPE type, void *detail);
    int leaveBlock(MD_BLOCKTYPE type, void *detail);
    int enterSpan(MD_SPANTYPE type, void *detail);
    int leaveSpan(MD_SPANTYPE type, void *detail);
    int text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size);

    QList<Block> m_blocks;
    QList<Block *> m_blockStack;   // stack of open blocks
    // Inline formatting state
    bool m_bold = false;
    bool m_italic = false;
    bool m_strikethrough = false;
    bool m_code = false;
    bool m_math = false;
    bool m_mathDisplay = false;
    QString m_linkHref;
    QString m_wikiTarget;
};

} // namespace Markoff

#endif // MARKOFF_DOCUMENTBUILDER_P_H
```

- [ ] **Step 5: Implement DocumentBuilder**

```cpp
// libs/markoff/src/DocumentBuilder.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocumentBuilder_p.h"

namespace Markoff {

DocumentBuilder::DocumentBuilder() = default;

bool DocumentBuilder::parse(const QString &markdown)
{
    m_blocks.clear();
    m_blockStack.clear();
    m_bold = m_italic = m_strikethrough = m_code = m_math = m_mathDisplay = false;
    m_linkHref.clear();
    m_wikiTarget.clear();

    QByteArray utf8 = markdown.toUtf8();

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB
                 | MD_FLAG_WIKILINKS
                 | MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = &DocumentBuilder::onEnterBlock;
    parser.leave_block = &DocumentBuilder::onLeaveBlock;
    parser.enter_span = &DocumentBuilder::onEnterSpan;
    parser.leave_span = &DocumentBuilder::onLeaveSpan;
    parser.text = &DocumentBuilder::onText;

    int result = md_parse(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), &parser, this);
    return result == 0;
}

QList<Block> DocumentBuilder::takeBlocks()
{
    return std::move(m_blocks);
}

// Static callback trampolines
int DocumentBuilder::onEnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{ return static_cast<DocumentBuilder *>(userdata)->enterBlock(type, detail); }

int DocumentBuilder::onLeaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{ return static_cast<DocumentBuilder *>(userdata)->leaveBlock(type, detail); }

int DocumentBuilder::onEnterSpan(MD_SPANTYPE type, void *detail, void *userdata)
{ return static_cast<DocumentBuilder *>(userdata)->enterSpan(type, detail); }

int DocumentBuilder::onLeaveSpan(MD_SPANTYPE type, void *detail, void *userdata)
{ return static_cast<DocumentBuilder *>(userdata)->leaveSpan(type, detail); }

int DocumentBuilder::onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{ return static_cast<DocumentBuilder *>(userdata)->text(type, text, size); }

static QString attrToString(const MD_ATTRIBUTE &attr)
{
    if (!attr.text || attr.size == 0)
        return {};
    return QString::fromUtf8(attr.text, attr.size);
}

int DocumentBuilder::enterBlock(MD_BLOCKTYPE type, void *detail)
{
    Block block;
    block.type = type;

    switch (type) {
    case MD_BLOCK_H:
        block.headingLevel = static_cast<MD_BLOCK_H_DETAIL *>(detail)->level;
        break;
    case MD_BLOCK_CODE:
        block.codeInfo = attrToString(static_cast<MD_BLOCK_CODE_DETAIL *>(detail)->lang);
        break;
    case MD_BLOCK_LI:
        if (auto *li = static_cast<MD_BLOCK_LI_DETAIL *>(detail); li->is_task) {
            block.isTaskItem = true;
            block.taskMark = li->task_mark;
        }
        break;
    case MD_BLOCK_OL:
        block.listStart = static_cast<MD_BLOCK_OL_DETAIL *>(detail)->start;
        block.isTightList = static_cast<MD_BLOCK_OL_DETAIL *>(detail)->is_tight;
        break;
    case MD_BLOCK_UL:
        block.isTightList = static_cast<MD_BLOCK_UL_DETAIL *>(detail)->is_tight;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        block.tableAlign = static_cast<MD_BLOCK_TD_DETAIL *>(detail)->align;
        break;
    default:
        break;
    }

    if (m_blockStack.isEmpty()) {
        m_blocks.append(std::move(block));
        m_blockStack.append(&m_blocks.last());
    } else {
        m_blockStack.last()->children.append(std::move(block));
        m_blockStack.append(&m_blockStack.last()->children.last());
    }

    return 0;
}

int DocumentBuilder::leaveBlock(MD_BLOCKTYPE, void *)
{
    if (!m_blockStack.isEmpty())
        m_blockStack.removeLast();
    return 0;
}

int DocumentBuilder::enterSpan(MD_SPANTYPE type, void *detail)
{
    switch (type) {
    case MD_SPAN_EM:       m_italic = true; break;
    case MD_SPAN_STRONG:   m_bold = true; break;
    case MD_SPAN_DEL:      m_strikethrough = true; break;
    case MD_SPAN_CODE:     m_code = true; break;
    case MD_SPAN_LATEXMATH: m_math = true; break;
    case MD_SPAN_LATEXMATH_DISPLAY: m_mathDisplay = true; break;
    case MD_SPAN_A:
        m_linkHref = attrToString(static_cast<MD_SPAN_A_DETAIL *>(detail)->href);
        break;
    case MD_SPAN_WIKILINK:
        m_wikiTarget = attrToString(static_cast<MD_SPAN_WIKILINK_DETAIL *>(detail)->target);
        break;
    default: break;
    }
    return 0;
}

int DocumentBuilder::leaveSpan(MD_SPANTYPE type, void *)
{
    switch (type) {
    case MD_SPAN_EM:       m_italic = false; break;
    case MD_SPAN_STRONG:   m_bold = false; break;
    case MD_SPAN_DEL:      m_strikethrough = false; break;
    case MD_SPAN_CODE:     m_code = false; break;
    case MD_SPAN_LATEXMATH: m_math = false; break;
    case MD_SPAN_LATEXMATH_DISPLAY: m_mathDisplay = false; break;
    case MD_SPAN_A:        m_linkHref.clear(); break;
    case MD_SPAN_WIKILINK: m_wikiTarget.clear(); break;
    default: break;
    }
    return 0;
}

int DocumentBuilder::text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size)
{
    if (m_blockStack.isEmpty())
        return 0;

    Block *current = m_blockStack.last();

    QString str;
    switch (type) {
    case MD_TEXT_BR:
        str = QStringLiteral("\n");
        break;
    case MD_TEXT_SOFTBR:
        str = QStringLiteral(" ");
        break;
    case MD_TEXT_NULLCHAR:
        str = QChar(0xFFFD);
        break;
    default:
        str = QString::fromUtf8(text, size);
        break;
    }

    InlineRun run;
    run.text = str;
    run.bold = m_bold;
    run.italic = m_italic;
    run.strikethrough = m_strikethrough;
    run.code = m_code;
    run.math = m_math;
    run.mathDisplay = m_mathDisplay;
    run.linkHref = m_linkHref;
    run.wikiTarget = m_wikiTarget;

    current->inlines.append(std::move(run));
    return 0;
}

} // namespace Markoff
```

- [ ] **Step 6: Update Document.cpp to use DocumentBuilder and implement extractSubpath**

Replace the contents of `libs/markoff/src/Document.cpp`:

```cpp
// libs/markoff/src/Document.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/Document.h"
#include "DocumentBuilder_p.h"

namespace Markoff {

struct Document::Private {
    QString source;
    QList<Block> blocks;
};

Document::Document() : d(std::make_unique<Private>()) {}
Document::~Document() = default;

std::unique_ptr<Document> Document::fromMarkdown(const QString &markdown)
{
    auto doc = std::unique_ptr<Document>(new Document());
    doc->d->source = markdown;

    if (!markdown.isEmpty()) {
        DocumentBuilder builder;
        if (builder.parse(markdown)) {
            doc->d->blocks = builder.takeBlocks();
        }
    }

    return doc;
}

QString Document::sourceText() const { return d->source; }
bool Document::isEmpty() const { return d->source.isEmpty(); }

QString Document::extractSubpath(const QString &subpath) const
{
    if (subpath.isEmpty() || d->source.isEmpty())
        return {};

    const QStringList lines = d->source.split(QLatin1Char('\n'));

    // Block ID: "#^block-id"
    if (subpath.startsWith(QStringLiteral("#^"))) {
        const QString blockId = subpath.mid(2);
        const QString marker = QStringLiteral("^") + blockId;

        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].contains(marker)) {
                // Expand to contiguous non-empty lines (paragraph)
                int start = i;
                while (start > 0 && !lines[start - 1].trimmed().isEmpty())
                    --start;
                int end = i;
                while (end + 1 < lines.size() && !lines[end + 1].trimmed().isEmpty())
                    ++end;

                QStringList result;
                for (int j = start; j <= end; ++j) {
                    QString line = lines[j];
                    // Strip the block ID marker from output
                    int markerPos = line.indexOf(marker);
                    if (markerPos >= 0)
                        line = line.left(markerPos).trimmed();
                    result.append(line);
                }
                return result.join(QLatin1Char('\n'));
            }
        }
        return {};
    }

    // Heading: "#heading-text"
    if (subpath.startsWith(QLatin1Char('#'))) {
        const QString target = subpath.mid(1).replace(QLatin1Char('-'), QLatin1Char(' '));
        int headingLevel = 0;
        int startLine = -1;

        for (int i = 0; i < lines.size(); ++i) {
            const QString &line = lines[i];
            if (!line.startsWith(QLatin1Char('#')))
                continue;

            // Count heading level
            int level = 0;
            while (level < line.size() && line[level] == QLatin1Char('#'))
                ++level;
            if (level == 0 || level > 6)
                continue;
            if (level < line.size() && line[level] != QLatin1Char(' '))
                continue;

            QString headingText = line.mid(level).trimmed();

            if (startLine < 0) {
                // Looking for the target heading
                if (headingText.compare(target, Qt::CaseInsensitive) == 0) {
                    startLine = i;
                    headingLevel = level;
                }
            } else {
                // Found start, looking for end (same or higher level heading)
                if (level <= headingLevel) {
                    QStringList result = lines.mid(startLine, i - startLine);
                    while (!result.isEmpty() && result.last().trimmed().isEmpty())
                        result.removeLast();
                    return result.join(QLatin1Char('\n'));
                }
            }
        }

        // Heading found but extends to EOF
        if (startLine >= 0) {
            QStringList result = lines.mid(startLine);
            while (!result.isEmpty() && result.last().trimmed().isEmpty())
                result.removeLast();
            return result.join(QLatin1Char('\n'));
        }
    }

    return {};
}

} // namespace Markoff
```

- [ ] **Step 7: Add DocumentBuilder.cpp to CMakeLists.txt**

In `libs/markoff/CMakeLists.txt`, update the `add_library` sources:

```cmake
add_library(markoff STATIC
    src/Document.cpp
    src/DocumentBuilder.cpp
)
```

- [ ] **Step 8: Build and run tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target tst_markoff_document && ctest -R tst_markoff_document --output-on-failure`

Expected: All 5 tests PASS.

- [ ] **Step 9: Commit**

```bash
git add libs/markoff/src/DocumentBuilder_p.h libs/markoff/src/DocumentBuilder.cpp \
  libs/markoff/src/Document.cpp libs/markoff/CMakeLists.txt \
  libs/markoff/tests/tst_document.cpp libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): add MD4C-based Document parsing with subpath extraction"
```

---

## Task 3: Renderer — AST to QTextDocument

**Files:**
- Create: `libs/markoff/include/markoff/Renderer.h`
- Create: `libs/markoff/src/Renderer.cpp`
- Create: `libs/markoff/tests/tst_renderer.cpp`
- Modify: `libs/markoff/CMakeLists.txt`
- Modify: `libs/markoff/tests/CMakeLists.txt`

- [ ] **Step 1: Write the renderer test**

```cpp
// libs/markoff/tests/tst_renderer.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include "markoff/Document.h"
#include "markoff/Renderer.h"
#include "markoff/RenderSettings.h"

class TestRenderer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testRenderReturnsDocument()
    {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Hello world"));
        Markoff::Renderer renderer;
        auto result = renderer.renderToTextDocument(*doc);
        QVERIFY(result);
        QVERIFY(!result->isEmpty());
    }

    void testRenderHeading()
    {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("# Title"));
        Markoff::Renderer renderer;
        auto result = renderer.renderToTextDocument(*doc);
        QString html = result->toHtml();
        QVERIFY(html.contains(QStringLiteral("Title")));
    }

    void testRenderBold()
    {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("**bold text**"));
        Markoff::Renderer renderer;
        auto result = renderer.renderToTextDocument(*doc);
        QString html = result->toHtml();
        QVERIFY(html.contains(QStringLiteral("bold text")));
        QVERIFY(html.contains(QStringLiteral("font-weight")));
    }

    void testRenderCodeBlock()
    {
        auto doc = Markoff::Document::fromMarkdown(
            QStringLiteral("```cpp\nint x = 42;\n```"));
        Markoff::Renderer renderer;
        auto result = renderer.renderToTextDocument(*doc);
        QString html = result->toHtml();
        QVERIFY(html.contains(QStringLiteral("int x = 42")));
    }

    void testRenderSettingsAffectOutput()
    {
        auto doc = Markoff::Document::fromMarkdown(QStringLiteral("Hello"));
        Markoff::Renderer renderer;

        Markoff::RenderSettings settings;
        settings.baseFontSizePt = 20;
        renderer.setSettings(settings);

        auto result = renderer.renderToTextDocument(*doc);
        QString html = result->toHtml();
        QVERIFY(html.contains(QStringLiteral("20")));
    }

    void testRenderEmptyDocument()
    {
        auto doc = Markoff::Document::fromMarkdown(QString());
        Markoff::Renderer renderer;
        auto result = renderer.renderToTextDocument(*doc);
        QVERIFY(result);
        QVERIFY(result->isEmpty());
    }
};

QTEST_MAIN(TestRenderer)
#include "tst_renderer.moc"
```

- [ ] **Step 2: Create Renderer.h**

```cpp
// libs/markoff/include/markoff/Renderer.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_RENDERER_H
#define MARKOFF_RENDERER_H

#include <memory>

class QTextDocument;

namespace Markoff {

class Document;
struct RenderSettings;

class Renderer {
public:
    Renderer();
    ~Renderer();

    void setSettings(const RenderSettings &settings);
    RenderSettings settings() const;

    std::unique_ptr<QTextDocument> renderToTextDocument(const Document &doc) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_RENDERER_H
```

- [ ] **Step 3: Implement Renderer.cpp**

```cpp
// libs/markoff/src/Renderer.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/Renderer.h"
#include "markoff/Document.h"
#include "markoff/RenderSettings.h"
#include "DocumentBuilder_p.h"

#include <QTextDocument>
#include <QString>

namespace Markoff {

struct Renderer::Private {
    RenderSettings settings;
};

Renderer::Renderer() : d(std::make_unique<Private>()) {}
Renderer::~Renderer() = default;

void Renderer::setSettings(const RenderSettings &settings)
{
    d->settings = settings;
}

RenderSettings Renderer::settings() const
{
    return d->settings;
}

// Forward declaration — Document needs to expose blocks for rendering
// We use a friend or accessor pattern. For now, we re-parse the source.
// This is acceptable for Phase 1; later the Document will expose its AST.

std::unique_ptr<QTextDocument> Renderer::renderToTextDocument(const Document &doc) const
{
    auto textDoc = std::make_unique<QTextDocument>();

    if (doc.isEmpty())
        return textDoc;

    // Re-parse to get the block structure
    // (Phase 1 shortcut — later, Document exposes its AST directly)
    DocumentBuilder builder;
    if (!builder.parse(doc.sourceText()))
        return textDoc;

    const auto blocks = builder.takeBlocks();
    const auto &s = d->settings;

    // Build HTML from the AST
    QString html;
    html += QStringLiteral("<html><head><style>");
    html += QStringLiteral("body { font-size: %1pt; margin: %2px; ")
                .arg(s.baseFontSizePt).arg(s.marginPx);
    if (s.maxWidthPx > 0)
        html += QStringLiteral("max-width: %1px; ").arg(s.maxWidthPx);
    html += QStringLiteral("}");
    html += QStringLiteral(" code { background-color: #f0f0f0; padding: 2px 4px; font-family: monospace; }");
    html += QStringLiteral(" pre { background-color: #f0f0f0; padding: 8px; font-family: monospace; }");
    html += QStringLiteral(" blockquote { border-left: 3px solid #ccc; margin-left: 0; padding-left: 12px; color: #666; }");
    html += QStringLiteral(" hr { border: none; border-top: 1px solid #ccc; }");
    html += QStringLiteral("</style></head><body>");

    std::function<void(const QList<Block> &)> renderBlocks;
    renderBlocks = [&](const QList<Block> &blockList) {
        for (const auto &block : blockList) {
            switch (block.type) {
            case MD_BLOCK_H:
                html += QStringLiteral("<h%1>").arg(block.headingLevel);
                break;
            case MD_BLOCK_P:
                html += QStringLiteral("<p>");
                break;
            case MD_BLOCK_CODE: {
                html += QStringLiteral("<pre><code>");
                break;
            }
            case MD_BLOCK_QUOTE:
                html += QStringLiteral("<blockquote>");
                break;
            case MD_BLOCK_UL:
                html += QStringLiteral("<ul>");
                break;
            case MD_BLOCK_OL:
                html += QStringLiteral("<ol start=\"%1\">").arg(block.listStart);
                break;
            case MD_BLOCK_LI:
                html += QStringLiteral("<li>");
                if (block.isTaskItem) {
                    if (block.taskMark == 'x' || block.taskMark == 'X')
                        html += QStringLiteral("[x] ");
                    else
                        html += QStringLiteral("[ ] ");
                }
                break;
            case MD_BLOCK_HR:
                html += QStringLiteral("<hr>");
                break;
            case MD_BLOCK_TABLE:
                html += QStringLiteral("<table border=\"1\" cellpadding=\"4\" cellspacing=\"0\">");
                break;
            case MD_BLOCK_THEAD:
                html += QStringLiteral("<thead>");
                break;
            case MD_BLOCK_TBODY:
                html += QStringLiteral("<tbody>");
                break;
            case MD_BLOCK_TR:
                html += QStringLiteral("<tr>");
                break;
            case MD_BLOCK_TH:
                html += QStringLiteral("<th>");
                break;
            case MD_BLOCK_TD:
                html += QStringLiteral("<td>");
                break;
            default:
                break;
            }

            // Render inline content
            for (const auto &run : block.inlines) {
                QString text = run.text.toHtmlEscaped();

                if (run.code || block.type == MD_BLOCK_CODE) {
                    html += text;
                    continue;
                }
                if (run.bold) text = QStringLiteral("<b>") + text + QStringLiteral("</b>");
                if (run.italic) text = QStringLiteral("<i>") + text + QStringLiteral("</i>");
                if (run.strikethrough) text = QStringLiteral("<s>") + text + QStringLiteral("</s>");
                if (run.math || run.mathDisplay)
                    text = QStringLiteral("<code>") + text + QStringLiteral("</code>");
                if (!run.linkHref.isEmpty())
                    text = QStringLiteral("<a href=\"%1\">%2</a>").arg(run.linkHref.toHtmlEscaped(), text);
                if (!run.wikiTarget.isEmpty())
                    text = QStringLiteral("<a href=\"wikilink:%1\">%2</a>")
                               .arg(run.wikiTarget.toHtmlEscaped(), text);

                html += text;
            }

            // Render children (nested blocks)
            if (!block.children.isEmpty())
                renderBlocks(block.children);

            // Close tags
            switch (block.type) {
            case MD_BLOCK_H:
                html += QStringLiteral("</h%1>").arg(block.headingLevel);
                break;
            case MD_BLOCK_P:  html += QStringLiteral("</p>"); break;
            case MD_BLOCK_CODE: html += QStringLiteral("</code></pre>"); break;
            case MD_BLOCK_QUOTE: html += QStringLiteral("</blockquote>"); break;
            case MD_BLOCK_UL: html += QStringLiteral("</ul>"); break;
            case MD_BLOCK_OL: html += QStringLiteral("</ol>"); break;
            case MD_BLOCK_LI: html += QStringLiteral("</li>"); break;
            case MD_BLOCK_TABLE: html += QStringLiteral("</table>"); break;
            case MD_BLOCK_THEAD: html += QStringLiteral("</thead>"); break;
            case MD_BLOCK_TBODY: html += QStringLiteral("</tbody>"); break;
            case MD_BLOCK_TR: html += QStringLiteral("</tr>"); break;
            case MD_BLOCK_TH: html += QStringLiteral("</th>"); break;
            case MD_BLOCK_TD: html += QStringLiteral("</td>"); break;
            default: break;
            }
        }
    };

    renderBlocks(blocks);
    html += QStringLiteral("</body></html>");

    textDoc->setHtml(html);
    return textDoc;
}

} // namespace Markoff
```

- [ ] **Step 4: Add Renderer.cpp to CMakeLists.txt**

In `libs/markoff/CMakeLists.txt`, update sources:

```cmake
add_library(markoff STATIC
    src/Document.cpp
    src/DocumentBuilder.cpp
    src/Renderer.cpp
)
```

- [ ] **Step 5: Add renderer test to tests/CMakeLists.txt**

Append to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_renderer tst_renderer.cpp)
add_test(NAME tst_markoff_renderer COMMAND tst_markoff_renderer)
target_link_libraries(tst_markoff_renderer PRIVATE Qt6::Test markoff)
set_tests_properties(tst_markoff_renderer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 6: Build and run tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . && ctest -R tst_markoff --output-on-failure`

Expected: All document tests and renderer tests PASS.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/include/markoff/Renderer.h libs/markoff/src/Renderer.cpp \
  libs/markoff/tests/tst_renderer.cpp libs/markoff/CMakeLists.txt \
  libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): add Renderer producing QTextDocument from parsed AST"
```

---

## Task 4: Qt Widget Fork — Editor

**Files:**
- Create: `libs/markoff/include/markoff/Editor.h`
- Create: `libs/markoff/src/Editor.cpp`
- Create: `libs/markoff/src/Editor_p.h`
- Create: `libs/markoff/src/TextControl.h`
- Create: `libs/markoff/src/TextControl_p.h`
- Create: `libs/markoff/src/TextControl.cpp`
- Modify: `libs/markoff/CMakeLists.txt`

This is the highest-risk task. The process is mechanical but requires careful attention.

- [ ] **Step 1: Copy Qt source files into the markoff tree**

```bash
cp ~/src/qtbase/src/widgets/widgets/qplaintextedit.h libs/markoff/src/Editor_qt_orig.h
cp ~/src/qtbase/src/widgets/widgets/qplaintextedit.cpp libs/markoff/src/Editor_qt_orig.cpp
cp ~/src/qtbase/src/widgets/widgets/qplaintextedit_p.h libs/markoff/src/Editor_p_qt_orig.h
cp ~/src/qtbase/src/widgets/widgets/qwidgettextcontrol_p.h libs/markoff/src/TextControl_qt_orig.h
cp ~/src/qtbase/src/widgets/widgets/qwidgettextcontrol_p_p.h libs/markoff/src/TextControl_p_qt_orig.h
cp ~/src/qtbase/src/widgets/widgets/qwidgettextcontrol.cpp libs/markoff/src/TextControl_qt_orig.cpp
```

These `_qt_orig` files are reference copies. We create our own files from them.

- [ ] **Step 2: Create Editor.h (public API)**

Create `libs/markoff/include/markoff/Editor.h` — this is our narrow public API, NOT a copy of the Qt header. The implementation delegates to the forked internals.

```cpp
// libs/markoff/include/markoff/Editor.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_EDITOR_H
#define MARKOFF_EDITOR_H

#include <QAbstractScrollArea>
#include <memory>

class QTextDocument;

namespace Markoff {

class Editor : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    void setPlainText(const QString &text);
    QString toPlainText() const;

    QTextDocument *document() const;

Q_SIGNALS:
    void textChanged();

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void scrollContentsBy(int dx, int dy) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    bool event(QEvent *e) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_EDITOR_H
```

- [ ] **Step 3: Create the forked internals**

This step is the core work. Create `Editor_p.h`, `TextControl.h`, `TextControl_p.h`, `TextControl.cpp`, and `Editor.cpp` by adapting the Qt originals.

**The adaptation process for each file:**

1. Replace all `QPlainTextEdit` → `Markoff::Editor`, `QWidgetTextControl` → `Markoff::TextControl`
2. Replace Qt private header includes (`private/qabstractscrollarea_p.h`, `private/qobject_p.h`) with our own structures
3. Replace `QPlainTextEditPrivate : QAbstractScrollAreaPrivate` with `Editor::Private` as a plain struct with a back-pointer `Editor *q`
4. Replace `QWidgetTextControlPrivate : QObjectPrivate` with `TextControl::Private` as a plain struct with a back-pointer `TextControl *q`
5. Replace `Q_D()` / `Q_Q()` macros with direct member access via `d->` and `d->q->`
6. Remove `Q_DECLARE_PRIVATE` / `Q_DECLARE_PUBLIC`
7. Remove `QT_REQUIRE_CONFIG` guards
8. Remove `QT_BEGIN_NAMESPACE` / `QT_END_NAMESPACE` (we use `namespace Markoff`)
9. Strip rich text mode, `setAcceptRichText()`, auto-bullet creation
10. Strip placeholder text rendering
11. Keep all keyboard, mouse, selection, clipboard, undo/redo, scroll, IME handling
12. Update the license header to note the fork origin and GPL-3.0-or-later

**This is a large, mechanical transformation.** The exact resulting code depends on how deep the private API coupling is and what must be inlined vs. reimplemented. The engineer should:
- Work file by file, starting with `TextControl_p.h` (state), then `TextControl.h` (interface), then `TextControl.cpp` (implementation), then `Editor_p.h`, then `Editor.cpp`
- Compile after each file to catch issues incrementally
- When a private API call cannot be replaced with public API, check if the needed functionality can be inlined (copy the 5-10 lines) or if a shim is needed

**Expected outcome:** 4-6 files totaling ~5,000-6,000 lines (reduced from ~7,800 by stripping rich text mode and unused features).

- [ ] **Step 4: Add Editor and TextControl to CMakeLists.txt**

```cmake
add_library(markoff STATIC
    src/Document.cpp
    src/DocumentBuilder.cpp
    src/Renderer.cpp
    src/Editor.cpp
    src/TextControl.cpp
)
```

- [ ] **Step 5: Build the editor**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target markoff`

Expected: Compiles. This may take multiple iterations to resolve private API dependencies. Each compilation error is a decoupling point to address.

- [ ] **Step 6: Smoke test — create a minimal test program**

Create a temporary test (not committed) to verify the editor works:

```cpp
// Quick manual test — run and type in the window
#include <QApplication>
#include "markoff/Editor.h"
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    Markoff::Editor editor;
    editor.setPlainText("# Hello Markoff\n\nType here...");
    editor.resize(600, 400);
    editor.show();
    return app.exec();
}
```

Verify: typing, cursor movement, selection, copy/paste, undo/redo, scrolling all work.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp \
  libs/markoff/src/Editor_p.h libs/markoff/src/TextControl.h \
  libs/markoff/src/TextControl_p.h libs/markoff/src/TextControl.cpp \
  libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): fork QPlainTextEdit + QWidgetTextControl into Markoff::Editor"
```

---

## Task 5: ReadingView Widget

**Files:**
- Create: `libs/markoff/include/markoff/ReadingView.h`
- Create: `libs/markoff/src/ReadingView.cpp`
- Modify: `libs/markoff/CMakeLists.txt`

- [ ] **Step 1: Create ReadingView.h**

```cpp
// libs/markoff/include/markoff/ReadingView.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_READINGVIEW_H
#define MARKOFF_READINGVIEW_H

#include <QWidget>
#include <memory>

namespace Markoff {

class Document;
struct RenderSettings;

class ReadingView : public QWidget {
    Q_OBJECT
public:
    explicit ReadingView(QWidget *parent = nullptr);
    ~ReadingView() override;

    void setDocument(const Document &doc);
    void setSettings(const RenderSettings &settings);

Q_SIGNALS:
    void linkClicked(const QString &target);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff

#endif // MARKOFF_READINGVIEW_H
```

- [ ] **Step 2: Implement ReadingView.cpp**

```cpp
// libs/markoff/src/ReadingView.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/ReadingView.h"
#include "markoff/Document.h"
#include "markoff/Renderer.h"
#include "markoff/RenderSettings.h"

#include <QTextBrowser>
#include <QVBoxLayout>
#include <QUrl>

namespace Markoff {

struct ReadingView::Private {
    QTextBrowser *browser = nullptr;
    Renderer renderer;
    const Document *currentDoc = nullptr;

    void render(const Document &doc)
    {
        auto textDoc = renderer.renderToTextDocument(doc);
        browser->setHtml(textDoc->toHtml());
    }
};

ReadingView::ReadingView(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->browser = new QTextBrowser(this);
    d->browser->setOpenLinks(false);
    d->browser->setOpenExternalLinks(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d->browser);

    connect(d->browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        Q_EMIT linkClicked(url.toString());
    });
}

ReadingView::~ReadingView() = default;

void ReadingView::setDocument(const Document &doc)
{
    d->render(doc);
}

void ReadingView::setSettings(const RenderSettings &settings)
{
    d->renderer.setSettings(settings);
}

} // namespace Markoff
```

- [ ] **Step 3: Add ReadingView.cpp to CMakeLists.txt**

```cmake
add_library(markoff STATIC
    src/Document.cpp
    src/DocumentBuilder.cpp
    src/Renderer.cpp
    src/Editor.cpp
    src/TextControl.cpp
    src/ReadingView.cpp
)
```

- [ ] **Step 4: Build**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target markoff`

Expected: Compiles.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/include/markoff/ReadingView.h libs/markoff/src/ReadingView.cpp \
  libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add ReadingView widget with QTextBrowser rendering"
```

---

## Task 6: Test Application

**Files:**
- Create: `libs/markoff/app/main.cpp`
- Create: `libs/markoff/app/MainWindow.h`
- Create: `libs/markoff/app/MainWindow.cpp`
- Modify: `libs/markoff/app/CMakeLists.txt`

- [ ] **Step 1: Update app/CMakeLists.txt**

```cmake
# libs/markoff/app/CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

add_executable(markoff-testapp
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(markoff-testapp PRIVATE Qt6::Widgets markoff)
```

- [ ] **Step 2: Create main.cpp**

```cpp
// libs/markoff/app/main.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Markoff Test"));

    MainWindow window;

    if (argc > 1)
        window.openFile(QString::fromLocal8Bit(argv[1]));

    window.show();
    return app.exec();
}
```

- [ ] **Step 3: Create MainWindow.h**

```cpp
// libs/markoff/app/MainWindow.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TESTAPP_MAINWINDOW_H
#define MARKOFF_TESTAPP_MAINWINDOW_H

#include <QMainWindow>
#include <memory>

namespace Markoff { class Editor; class ReadingView; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString &path);

private Q_SLOTS:
    void onOpen();
    void onSave();
    void onTextChanged();

private:
    void updateTitle();

    Markoff::Editor *m_editor = nullptr;
    Markoff::ReadingView *m_readingView = nullptr;
    QString m_filePath;
};

#endif // MARKOFF_TESTAPP_MAINWINDOW_H
```

- [ ] **Step 4: Create MainWindow.cpp**

```cpp
// libs/markoff/app/MainWindow.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "markoff/Editor.h"
#include "markoff/ReadingView.h"
#include "markoff/Document.h"
#include "markoff/RenderSettings.h"

#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QSpinBox>
#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Widgets
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_editor = new Markoff::Editor(splitter);
    m_readingView = new Markoff::ReadingView(splitter);
    splitter->addWidget(m_editor);
    splitter->addWidget(m_readingView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    // Toolbar
    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);

    auto *openAction = toolbar->addAction(tr("Open"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

    auto *saveAction = toolbar->addAction(tr("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSave);

    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(tr("Font size:")));
    auto *fontSpin = new QSpinBox(toolbar);
    fontSpin->setRange(8, 32);
    fontSpin->setValue(14);
    toolbar->addWidget(fontSpin);

    connect(fontSpin, &QSpinBox::valueChanged, this, [this](int size) {
        Markoff::RenderSettings settings;
        settings.baseFontSizePt = size;
        m_readingView->setSettings(settings);
        onTextChanged(); // re-render
    });

    // Connections
    connect(m_editor, &Markoff::Editor::textChanged, this, &MainWindow::onTextChanged);

    // Window
    resize(1200, 700);
    updateTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    m_editor->setPlainText(stream.readAll());
    m_filePath = path;
    updateTitle();
}

void MainWindow::onOpen()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open Markdown"), QString(),
        tr("Markdown files (*.md);;All files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onSave()
{
    if (m_filePath.isEmpty()) {
        m_filePath = QFileDialog::getSaveFileName(
            this, tr("Save Markdown"), QString(),
            tr("Markdown files (*.md);;All files (*)"));
        if (m_filePath.isEmpty())
            return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream << m_editor->toPlainText();
    updateTitle();
}

void MainWindow::onTextChanged()
{
    auto doc = Markoff::Document::fromMarkdown(m_editor->toPlainText());
    m_readingView->setDocument(*doc);
}

void MainWindow::updateTitle()
{
    if (m_filePath.isEmpty())
        setWindowTitle(QStringLiteral("Markoff \u2014 [untitled]"));
    else
        setWindowTitle(QStringLiteral("Markoff \u2014 %1").arg(m_filePath));
}
```

- [ ] **Step 5: Build and run**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target markoff-testapp && ./libs/markoff/app/markoff-testapp`

Expected: Window opens with split view. Type markdown on the left, see rendered output on the right. Open/Save work.

- [ ] **Step 6: Test with a real markdown file**

Run: `./libs/markoff/app/markoff-testapp ~/dev/Corbomite/libs/markoff/docs/01-problem-definition.md`

Expected: The file opens. Left pane shows raw markdown. Right pane shows rendered output with headings, paragraphs, lists, tables.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/app/main.cpp libs/markoff/app/MainWindow.h \
  libs/markoff/app/MainWindow.cpp libs/markoff/app/CMakeLists.txt
git commit -m "feat(markoff): add test application with split editor/reading view"
```

---

## Task 7: Corbomite Adapter

**Files:**
- Create: `libs/core/include/corbomite/core/MarkoffRenderEngine.h`
- Create: `libs/core/src/MarkoffRenderEngine.cpp`
- Modify: `libs/core/CMakeLists.txt`

- [ ] **Step 1: Create MarkoffRenderEngine.h**

```cpp
// libs/core/include/corbomite/core/MarkoffRenderEngine.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CORBOMITE_MARKOFFRENDERENGINE_H
#define CORBOMITE_MARKOFFRENDERENGINE_H

#include "MarkdownRenderEngine.h"

namespace Corbomite {

class MarkoffRenderEngine : public MarkdownRenderEngine {
public:
    std::unique_ptr<RenderedDocument> render(
        const QString &markdown,
        const RenderOptions &options = {}) const override;
};

} // namespace Corbomite

#endif // CORBOMITE_MARKOFFRENDERENGINE_H
```

- [ ] **Step 2: Implement MarkoffRenderEngine.cpp**

```cpp
// libs/core/src/MarkoffRenderEngine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "corbomite/core/MarkoffRenderEngine.h"
#include "corbomite/core/RenderedDocument.h"
#include "corbomite/core/RenderProfile.h"
#include "corbomite/core/RenderOptions.h"

#include <markoff/Document.h>
#include <markoff/Renderer.h>
#include <markoff/RenderSettings.h>

namespace Corbomite {

std::unique_ptr<RenderedDocument> MarkoffRenderEngine::render(
    const QString &markdown,
    const RenderOptions &options) const
{
    // Subpath extraction
    QString md = markdown;
    if (!options.subpath.isEmpty())
        md = extractSubpath(markdown, options.subpath);

    // Parse
    auto doc = Markoff::Document::fromMarkdown(md);

    // Translate settings
    Markoff::RenderSettings settings;
    settings.baseFontSizePt = options.baseFontSizePt.value_or(m_profile.baseFontSizePt);
    settings.maxWidthPx = options.maxWidthPx.value_or(m_profile.maxWidthPx);
    settings.marginPx = options.marginPx.value_or(m_profile.marginPx);
    settings.showFrontmatter = m_profile.showFrontmatter;
    settings.renderImages = m_profile.renderImages;
    settings.renderCodeHighlighting = m_profile.renderCodeHighlighting;

    // Render
    Markoff::Renderer renderer;
    renderer.setSettings(settings);
    auto textDoc = renderer.renderToTextDocument(*doc);

    return RenderedDocument::fromQTextDocument(std::move(textDoc));
}

} // namespace Corbomite
```

- [ ] **Step 3: Update libs/core/CMakeLists.txt**

Add `MarkoffRenderEngine.cpp` to sources and link to `markoff`:

In the `add_library` call, add:
```
    src/MarkoffRenderEngine.cpp
```

In `target_link_libraries`, add `markoff` to the link list:
```cmake
target_link_libraries(corbomite-core PUBLIC Qt6::Core Qt6::Gui Qt6::Svg Qt6::Widgets KF6::SyntaxHighlighting jkqtmathtext mmdr markoff)
```

- [ ] **Step 4: Build**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build .`

Expected: Full Corbomite project compiles including the new adapter.

- [ ] **Step 5: Run existing tests**

Run: `cd /home/clinton/dev/Corbomite/build && ctest --output-on-failure`

Expected: All existing Corbomite tests pass. The new adapter doesn't break anything — it's not wired in yet.

- [ ] **Step 6: Commit**

```bash
git add libs/core/include/corbomite/core/MarkoffRenderEngine.h \
  libs/core/src/MarkoffRenderEngine.cpp libs/core/CMakeLists.txt
git commit -m "feat(core): add MarkoffRenderEngine adapter bridging Markoff to Corbomite"
```

---

## Task 8: Final Integration Test

**Files:** None new — this is a verification task.

- [ ] **Step 1: Run all tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . && ctest --output-on-failure`

Expected: All tests pass — existing Corbomite tests, new Markoff document tests, new Markoff renderer tests.

- [ ] **Step 2: Run the test app with sample files**

```bash
./libs/markoff/app/markoff-testapp ~/dev/Corbomite/libs/markoff/docs/01-problem-definition.md
```

Verify: Headings render. Bold/italic render. Lists render. Code blocks render. Tables render.

- [ ] **Step 3: Run Corbomite itself**

Run: `cd /home/clinton/dev/Corbomite && ./build/Corbomite`

Verify: Corbomite launches normally. Existing render engine works. The new adapter exists but is not active yet (it would need to be injected at the application wiring site to replace `RegexRenderEngine`).

- [ ] **Step 4: Commit any remaining fixes**

If any test or verification revealed issues, fix and commit.

```bash
git commit -m "fix(markoff): address integration test findings"
```
