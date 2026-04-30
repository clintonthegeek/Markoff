> **Status: completed.** Walking skeleton landed in 13 commits ending `f172095` + dogfood-fix `b03b0d0`. All five v0 block delegates + LiveListModelBinding + LiveSelectionModel are in tree. Do not execute.

# Live Render walking skeleton — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the read-only Live render walking skeleton — `LiveView.qml` plus a delegate-per-AST-block ListView + cross-block selection + a Widget-bridged context menu — into `markoff-view-qml`, so the Phase-2 seam in `MarkoffEditor.qml` becomes a real switchable mode alongside the existing Source editor.

**Architecture:** v0 renders five block kinds (paragraph, heading, horizontal rule, image, code block) as separate QML delegates fed by a stable-identity `LiveBlockModel` whose rows come from a Myers-diff over block sequences computed on every `parseUpdatedAt`. Selection is canonical in a C++ `LiveSelectionModel`; a top-level `MouseArea` drives it; per-delegate `Connections` apply slices via `TextEdit.select`. Right-click invokes a `QMenu` through the KDAB Widget-window bridge. `MarkoffDocument` black-boxes the CRDT — no `CollabText::Crdt::*` types appear in this layer.

**Tech Stack:** C++20, Qt 6.8+ (Quick, QuickControls2, Qml, Widgets), KF6::SyntaxHighlighting, CMake 3.19+. Test framework Qt Test for C++ + QML smoke. Build with `-j 8` (no bare `-j`, which freezes the user's machine).

**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration/`).

**Related docs:**
- Spec: [`docs/specs/2026-04-29-live-render-design.md`](../specs/2026-04-29-live-render-design.md)
- Spike findings (load-bearing for the selection layer): [`docs/specs/2026-04-29-cross-block-selection-spike-findings.md`](../specs/2026-04-29-cross-block-selection-spike-findings.md)
- Spike code (runnable reference): `.spike/cross-block-selection/`
- Library guide: [`libs/markoff-view-qml/CLAUDE.md`](../../libs/markoff-view-qml/CLAUDE.md)

**Out of scope:** editing (write path), math/mermaid/table/list/blockquote/callout/frontmatter/link/wikilink delegates, plugin loader, theme loader, cross-mode selection sync, auto-scroll, touch gestures. See spec §8.

---

## File Structure

### Files created

```
libs/markoff-view-qml/
  include/markoff/view/qml/
    BlockKind.h                — string-keyed kind constants
    BlockRecord.h              — model row data
    LiveSelectionModel.h
    LiveBlockModel.h
    LiveListModelBinding.h
    LiveContextMenuHandler.h
  src/
    BlockKind.cpp              — kind-string definitions
    BlockWalker.h              — pure: source markdown → block list
    BlockWalker.cpp
    AstBlockDiff.h             — pure: two block lists → edit script
    AstBlockDiff.cpp
    LiveSelectionModel.cpp
    LiveBlockModel.cpp
    LiveListModelBinding.cpp
    LiveContextMenuHandler.cpp
  qml/
    LiveView.qml
    delegates/
      ParagraphDelegate.qml
      HeadingDelegate.qml
      HorizontalRuleDelegate.qml
      ImageDelegate.qml
      CodeBlockDelegate.qml
  tests/
    tst_view_qml_block_walker.cpp
    tst_view_qml_ast_block_diff.cpp
    tst_view_qml_live_selection_model.cpp
    tst_view_qml_live_block_model.cpp
    tst_view_qml_live_list_model_binding.cpp
    tst_view_qml_live_view_smoke.cpp
```

### Files modified

```
libs/markoff-view-qml/CMakeLists.txt
libs/markoff-view-qml/qml/MarkoffEditor.qml
libs/markoff-view-qml/CLAUDE.md
libs/markoff-view-qml/app/main.cpp
libs/markoff-view-qml/app/CMakeLists.txt
libs/markoff-view-qml/tests/CMakeLists.txt
```

### File responsibilities (one-liners)

- **BlockKind.h/.cpp** — constants for the v0 kind strings: `"paragraph"`, `"heading"`, `"hr"`, `"image"`, `"code_block"`. String-keyed (not enum) per spec invariant for plugin extensibility.
- **BlockRecord.h** — value type carrying a kind + raw markdown source + kind-specific fields (heading level, image src/alt, code language/body).
- **BlockWalker** — pure function `(QString source) -> QList<BlockRecord>`. Walks source markdown by blank-line + fenced-code-block regions, classifies each block by leading-pattern.
- **AstBlockDiff** — pure function `(prev: QList<BlockKey>, next: QList<BlockKey>) -> QList<DiffOp>`. Myers-LCS-based.
- **LiveBlockModel** — `QAbstractListModel`. Applies `DiffOp` sequences as Qt model signals.
- **LiveListModelBinding** — subscribes to `EditorBackend::parseUpdatedAt`, runs `BlockWalker` + `AstBlockDiff`, applies to `LiveBlockModel`, clears `LiveSelectionModel` if any touched block disappears.
- **LiveSelectionModel** — cross-block selection state + clipboard. Structurally identical to the spike's `SelectionModel`.
- **LiveContextMenuHandler** — KDAB Widget-bridge. Owns a `QMenu` with Copy + Select All actions.
- **LiveView.qml** — top-level `ListView` + `DelegateChooser` + `MouseArea` running the spike's `hit()` function. Right-click invokes context-menu handler.
- **delegates/*.qml** — one per kind. Inline content via `TextEdit.MarkdownText` for text-bearing kinds; `Image`, `Rectangle`, etc. for the others.

---

## Task 1: Add `Qt6::Widgets` + switch app to `QApplication`

Foundation for the Widget-window bridge. No feature change yet; verify the existing 6 view-qml + 25 foundation tests still pass.

**Files:**
- Modify: `libs/markoff-view-qml/CMakeLists.txt`
- Modify: `libs/markoff-view-qml/app/CMakeLists.txt`
- Modify: `libs/markoff-view-qml/app/main.cpp`

- [ ] **Step 1.1: Add Widgets to library deps**

In `libs/markoff-view-qml/CMakeLists.txt`, find the `find_package(Qt6 ...)` line and ensure `Widgets` is in the COMPONENTS list. Find the `target_link_libraries(... PUBLIC Qt6::Quick ...)` line and add `Qt6::Widgets`. Show the relevant unified diff context as a guide:

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Qml Test Widgets)
...
target_link_libraries(markoff_view_qml
    PUBLIC
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Widgets
        ...)
```

- [ ] **Step 1.2: Add Widgets to app deps**

In `libs/markoff-view-qml/app/CMakeLists.txt`, ensure `Qt6::Widgets` is linked to the `markoff-view-qml-app` target.

- [ ] **Step 1.3: Switch `main.cpp` to `QApplication`**

Replace the `QGuiApplication` include + construction in `libs/markoff-view-qml/app/main.cpp` with `QApplication`:

```cpp
// Was: #include <QGuiApplication>
#include <QApplication>
...
// Was: QGuiApplication app(argc, argv);
QApplication app(argc, argv);
```

- [ ] **Step 1.4: Build and run tests**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_|markoff_|anchor_json|selection|fold_ref)' --output-on-failure -j 8
```
Expected: all 6 view-qml + 25 foundation tests pass. The 2 pre-existing failures (`tst_markoff_undo_grouping`, `tst_markoff_table_operations`) outside our perimeter are unchanged.

- [ ] **Step 1.5: Commit**

```bash
git add libs/markoff-view-qml/CMakeLists.txt libs/markoff-view-qml/app/CMakeLists.txt libs/markoff-view-qml/app/main.cpp
git commit -m "feat(view-qml): adopt QApplication + Qt6::Widgets for KDAB bridge prep"
```

---

## Task 2: `BlockKind` constants + `BlockRecord` + `BlockKey` value types

Pure C++ value types. No tests yet — exercised by Task 3's BlockWalker tests.

**Files:**
- Create: `libs/markoff-view-qml/include/markoff/view/qml/BlockKind.h`
- Create: `libs/markoff-view-qml/src/BlockKind.cpp`
- Create: `libs/markoff-view-qml/include/markoff/view/qml/BlockRecord.h`

- [ ] **Step 2.1: Write `BlockKind.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::View::Qml {

/// Block-kind constants. String-keyed (not a closed enum) so that plugin-
/// registered kinds in future phases don't require recompiling this library.
namespace BlockKind {
    extern const QString Paragraph;
    extern const QString Heading;
    extern const QString HorizontalRule;
    extern const QString Image;
    extern const QString CodeBlock;
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 2.2: Write `BlockKind.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/BlockKind.h>

namespace Markoff::View::Qml::BlockKind {
    const QString Paragraph      = QStringLiteral("paragraph");
    const QString Heading        = QStringLiteral("heading");
    const QString HorizontalRule = QStringLiteral("hr");
    const QString Image          = QStringLiteral("image");
    const QString CodeBlock      = QStringLiteral("code_block");
}
```

- [ ] **Step 2.3: Write `BlockRecord.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::View::Qml {

/// Kind-tagged record for a single AST block, carrying both identity-bearing
/// data (kind + raw source) and rendering data (kind-specific fields). The
/// `source` field is the raw markdown text for this block exactly as it
/// appears in the document; `text` holds the renderable string (may equal
/// source, or strip frontmatter markers, etc., per kind).
struct BlockRecord {
    QString kind;          ///< One of BlockKind::*
    QString source;        ///< Raw markdown source for this block (identity)
    QString text;          ///< Renderable text (used by paragraph/heading delegates)

    int     headingLevel = 0;   ///< 1..6 if kind==Heading; else 0
    QString imageSrc;           ///< URL/path if kind==Image
    QString imageAlt;           ///< Alt text if kind==Image
    QString imageTitle;         ///< Optional title if kind==Image
    QString codeLanguage;       ///< Fence info-string if kind==CodeBlock
    QString codeText;           ///< Body without fences if kind==CodeBlock

    bool operator==(const BlockRecord &o) const noexcept {
        return kind == o.kind && source == o.source && text == o.text
            && headingLevel == o.headingLevel
            && imageSrc == o.imageSrc && imageAlt == o.imageAlt && imageTitle == o.imageTitle
            && codeLanguage == o.codeLanguage && codeText == o.codeText;
    }
    bool operator!=(const BlockRecord &o) const noexcept { return !(*this == o); }
};

/// Minimal identity key used by AstBlockDiff. Two blocks with the same kind
/// and source bytes are "the same block" from the diff's perspective.
struct BlockKey {
    QString kind;
    QString source;
    bool operator==(const BlockKey &o) const noexcept {
        return kind == o.kind && source == o.source;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 2.4: Add to library CMakeLists.txt**

In `libs/markoff-view-qml/CMakeLists.txt`, add `src/BlockKind.cpp` to the library's source list and `include/markoff/view/qml/BlockKind.h`, `include/markoff/view/qml/BlockRecord.h` to the public headers list.

- [ ] **Step 2.5: Build**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```
Expected: clean build. No test changes.

- [ ] **Step 2.6: Commit**

```bash
git add libs/markoff-view-qml/include/markoff/view/qml/BlockKind.h \
        libs/markoff-view-qml/include/markoff/view/qml/BlockRecord.h \
        libs/markoff-view-qml/src/BlockKind.cpp \
        libs/markoff-view-qml/CMakeLists.txt
git commit -m "feat(view-qml): BlockKind + BlockRecord + BlockKey value types"
```

---

## Task 3: `BlockWalker` — source markdown → `QList<BlockRecord>`

Pure function. Test-driven. Walks source by blank lines + fenced code regions; classifies each block.

**Files:**
- Create: `libs/markoff-view-qml/src/BlockWalker.h`
- Create: `libs/markoff-view-qml/src/BlockWalker.cpp`
- Test: `libs/markoff-view-qml/tests/tst_view_qml_block_walker.cpp`

- [ ] **Step 3.1: Write the failing test file**

Create `libs/markoff-view-qml/tests/tst_view_qml_block_walker.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/BlockWalker.h"
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;

class TstBlockWalker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_input_returns_empty_list() {
        QCOMPARE(BlockWalker::walk(QString()).size(), 0);
    }

    void single_paragraph_one_block() {
        const QString src = QStringLiteral("Hello world.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[0].text.trimmed(), QStringLiteral("Hello world."));
    }

    void two_paragraphs_separated_by_blank_line() {
        const QString src = QStringLiteral("First.\n\nSecond.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[1].kind, BlockKind::Paragraph);
    }

    void heading_level_one() {
        const QString src = QStringLiteral("# Title\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
        QCOMPARE(blocks[0].headingLevel, 1);
        QCOMPARE(blocks[0].text, QStringLiteral("Title"));
    }

    void heading_level_three() {
        const QString src = QStringLiteral("### Sub\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Heading);
        QCOMPARE(blocks[0].headingLevel, 3);
    }

    void horizontal_rule_dashes() {
        const QString src = QStringLiteral("---\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::HorizontalRule);
    }

    void horizontal_rule_asterisks() {
        const QString src = QStringLiteral("***\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::HorizontalRule);
    }

    void image_block() {
        const QString src = QStringLiteral("![Alt text](http://example.com/img.png)\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Image);
        QCOMPARE(blocks[0].imageAlt, QStringLiteral("Alt text"));
        QCOMPARE(blocks[0].imageSrc, QStringLiteral("http://example.com/img.png"));
    }

    void image_with_title() {
        const QString src = QStringLiteral("![A](u \"title here\")\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Image);
        QCOMPARE(blocks[0].imageTitle, QStringLiteral("title here"));
    }

    void fenced_code_block_with_language() {
        const QString src = QStringLiteral("```python\nprint('hi')\n```\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeLanguage, QStringLiteral("python"));
        QCOMPARE(blocks[0].codeText, QStringLiteral("print('hi')\n"));
    }

    void fenced_code_block_no_language() {
        const QString src = QStringLiteral("```\nplain\n```\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeLanguage, QString());
        QCOMPARE(blocks[0].codeText, QStringLiteral("plain\n"));
    }

    void code_block_inside_does_not_split_on_blank_lines() {
        const QString src = QStringLiteral("```\nline1\n\nline2\n```\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[0].codeText, QStringLiteral("line1\n\nline2\n"));
    }

    void mixed_doc_paragraph_heading_hr_image_codeblock_paragraph() {
        const QString src = QStringLiteral(
            "First paragraph.\n\n"
            "# Heading\n\n"
            "---\n\n"
            "![alt](u.png)\n\n"
            "```rust\nfn main() {}\n```\n\n"
            "Last paragraph.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 6);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(blocks[1].kind, BlockKind::Heading);
        QCOMPARE(blocks[2].kind, BlockKind::HorizontalRule);
        QCOMPARE(blocks[3].kind, BlockKind::Image);
        QCOMPARE(blocks[4].kind, BlockKind::CodeBlock);
        QCOMPARE(blocks[5].kind, BlockKind::Paragraph);
    }

    void unterminated_code_block_treated_as_codeblock_to_eof() {
        const QString src = QStringLiteral("```python\nprint(1)\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::CodeBlock);
    }

    void leading_blank_lines_ignored() {
        const QString src = QStringLiteral("\n\n\nHello.\n");
        const auto blocks = BlockWalker::walk(src);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, BlockKind::Paragraph);
    }
};

QTEST_APPLESS_MAIN(TstBlockWalker)
#include "tst_view_qml_block_walker.moc"
```

- [ ] **Step 3.2: Run the test, confirm it fails**

Add the test to `libs/markoff-view-qml/tests/CMakeLists.txt`:

```cmake
qt_add_executable(tst_view_qml_block_walker tst_view_qml_block_walker.cpp)
target_link_libraries(tst_view_qml_block_walker PRIVATE markoff_view_qml Qt6::Test)
add_test(NAME tst_view_qml_block_walker COMMAND tst_view_qml_block_walker)
```

```bash
cmake --build build-dev --target tst_view_qml_block_walker -j 8
```
Expected: build fails because `BlockWalker.h` doesn't exist yet.

- [ ] **Step 3.3: Write `BlockWalker.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <markoff/view/qml/BlockRecord.h>

namespace Markoff::View::Qml {

/// Walks a markdown source string and returns a list of top-level block
/// records. Pure function; no Qt-app or QML dependencies. The classification
/// is intentionally simple (line-based + fence-state); edge cases that
/// require full tree-sitter analysis are deferred to a later phase.
class BlockWalker {
public:
    static QList<BlockRecord> walk(const QString &source);
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 3.4: Write `BlockWalker.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockWalker.h"

#include <markoff/view/qml/BlockKind.h>

#include <QRegularExpression>
#include <QStringList>

namespace Markoff::View::Qml {

namespace {

bool isFenceOpen(const QString &line, QString &lang)
{
    static const QRegularExpression re(QStringLiteral("^```\\s*(\\S*)\\s*$"));
    const auto m = re.match(line);
    if (!m.hasMatch()) return false;
    lang = m.captured(1);
    return true;
}

bool isFenceClose(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^```\\s*$"));
    return re.match(line).hasMatch();
}

bool isHorizontalRule(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^[ \\t]*([-*_])(\\s*\\1){2,}[ \\t]*$"));
    return re.match(line).hasMatch();
}

bool isImageOnly(const QString &block, BlockRecord &rec)
{
    static const QRegularExpression re(
        QStringLiteral("^!\\[(.*?)\\]\\((\\S+?)(?:\\s+\"(.*?)\")?\\)\\s*$"));
    const auto m = re.match(block.trimmed());
    if (!m.hasMatch()) return false;
    rec.kind = BlockKind::Image;
    rec.imageAlt = m.captured(1);
    rec.imageSrc = m.captured(2);
    rec.imageTitle = m.captured(3);
    rec.text = block;
    rec.source = block;
    return true;
}

int headingLevel(const QString &line, QString &headingText)
{
    static const QRegularExpression re(QStringLiteral("^(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
    const auto m = re.match(line);
    if (!m.hasMatch()) return 0;
    headingText = m.captured(2);
    return m.captured(1).length();
}

}  // namespace

QList<BlockRecord> BlockWalker::walk(const QString &source)
{
    QList<BlockRecord> result;
    if (source.isEmpty()) return result;

    const QStringList lines = source.split(QChar('\n'));
    int i = 0;
    const int n = lines.size();

    while (i < n) {
        // Skip blank lines.
        while (i < n && lines[i].trimmed().isEmpty()) ++i;
        if (i >= n) break;

        BlockRecord rec;

        QString fenceLang;
        if (isFenceOpen(lines[i], fenceLang)) {
            // Fenced code block. Capture body until fence-close or EOF.
            const int startIdx = i;
            ++i;
            QStringList bodyLines;
            while (i < n && !isFenceClose(lines[i])) {
                bodyLines << lines[i];
                ++i;
            }
            const bool closed = (i < n && isFenceClose(lines[i]));
            const int endIdx = closed ? i : (n - 1);
            if (closed) ++i;  // consume closing fence

            rec.kind = BlockKind::CodeBlock;
            rec.codeLanguage = fenceLang;
            rec.codeText = bodyLines.join(QChar('\n'));
            if (!bodyLines.isEmpty() || closed) rec.codeText += QChar('\n');

            QStringList allLines;
            for (int k = startIdx; k <= endIdx && k < n; ++k) allLines << lines[k];
            rec.source = allLines.join(QChar('\n'));
            rec.text = rec.source;
            result.append(rec);
            continue;
        }

        // Non-fence: collect lines until next blank line.
        const int startIdx = i;
        QStringList blockLines;
        while (i < n && !lines[i].trimmed().isEmpty()) {
            blockLines << lines[i];
            ++i;
        }
        const QString blockSource = blockLines.join(QChar('\n'));

        // Classify.
        if (blockLines.size() == 1) {
            const QString single = blockLines.first();
            QString headingText;
            const int level = headingLevel(single, headingText);
            if (level > 0) {
                rec.kind = BlockKind::Heading;
                rec.headingLevel = level;
                rec.text = headingText;
                rec.source = blockSource;
                result.append(rec);
                continue;
            }
            if (isHorizontalRule(single)) {
                rec.kind = BlockKind::HorizontalRule;
                rec.source = blockSource;
                rec.text = blockSource;
                result.append(rec);
                continue;
            }
            BlockRecord img;
            if (isImageOnly(single, img)) {
                result.append(img);
                continue;
            }
        }

        // Default: paragraph.
        rec.kind = BlockKind::Paragraph;
        rec.source = blockSource;
        rec.text = blockSource;
        result.append(rec);

        Q_UNUSED(startIdx);
    }

    return result;
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 3.5: Add to library CMakeLists.txt**

In `libs/markoff-view-qml/CMakeLists.txt`, add `src/BlockWalker.cpp` to the library's source list.

- [ ] **Step 3.6: Build + run tests**

```bash
cmake --build build-dev --target tst_view_qml_block_walker -j 8
./build-dev/bin/tst_view_qml_block_walker -platform offscreen
```
Expected: all 14 test cases PASS.

- [ ] **Step 3.7: Commit**

```bash
git add libs/markoff-view-qml/src/BlockWalker.h \
        libs/markoff-view-qml/src/BlockWalker.cpp \
        libs/markoff-view-qml/tests/tst_view_qml_block_walker.cpp \
        libs/markoff-view-qml/CMakeLists.txt \
        libs/markoff-view-qml/tests/CMakeLists.txt
git commit -m "feat(view-qml): BlockWalker — source markdown → BlockRecord list"
```

---

## Task 4: `AstBlockDiff` — Myers/LCS over BlockKey lists

Pure function. TDD with 12 cases.

**Files:**
- Create: `libs/markoff-view-qml/src/AstBlockDiff.h`
- Create: `libs/markoff-view-qml/src/AstBlockDiff.cpp`
- Test: `libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp`

- [ ] **Step 4.1: Write the failing test**

Create `libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/AstBlockDiff.h"
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;

namespace {

BlockKey k(const QString &kind, const QString &source) {
    return BlockKey { kind, source };
}

QString opName(AstBlockDiff::OpKind k) {
    switch (k) {
        case AstBlockDiff::OpKind::Equal:  return QStringLiteral("Equal");
        case AstBlockDiff::OpKind::Insert: return QStringLiteral("Insert");
        case AstBlockDiff::OpKind::Delete: return QStringLiteral("Delete");
    }
    return QString();
}

QString opsToString(const QList<AstBlockDiff::Op> &ops) {
    QStringList parts;
    for (const auto &op : ops) {
        parts << QStringLiteral("%1(prev=%2,next=%3)").arg(opName(op.kind))
                                                      .arg(op.prevIndex)
                                                      .arg(op.nextIndex);
    }
    return parts.join(QStringLiteral(", "));
}

}  // namespace

class TstAstBlockDiff : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_to_empty_no_ops() {
        const auto ops = AstBlockDiff::diff({}, {});
        QCOMPARE(ops.size(), 0);
    }

    void empty_to_one_block_emits_one_insert() {
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("hi")) };
        const auto ops = AstBlockDiff::diff({}, next);
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[0].nextIndex, 0);
    }

    void one_block_to_empty_emits_one_delete() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("hi")) };
        const auto ops = AstBlockDiff::diff(prev, {});
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[0].prevIndex, 0);
    }

    void identity_three_blocks_three_equals() {
        QList<BlockKey> seq {
            k(BlockKind::Paragraph, QStringLiteral("a")),
            k(BlockKind::Paragraph, QStringLiteral("b")),
            k(BlockKind::Paragraph, QStringLiteral("c"))
        };
        const auto ops = AstBlockDiff::diff(seq, seq);
        QCOMPARE(ops.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(ops[i].kind, AstBlockDiff::OpKind::Equal);
            QCOMPARE(ops[i].prevIndex, i);
            QCOMPARE(ops[i].nextIndex, i);
        }
    }

    void single_insert_at_start() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        // Expect: Insert(next=0), Equal(0->1), Equal(1->2)
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[0].nextIndex, 0);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_insert_at_end() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[1].nextIndex, 1);
    }

    void single_insert_middle() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[1].nextIndex, 1);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_delete_at_start() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("b")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[0].prevIndex, 0);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_delete_at_end() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[1].prevIndex, 1);
    }

    void replace_kind_change_emits_delete_then_insert() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")) };
        QList<BlockKey> next { k(BlockKind::Heading,   QStringLiteral("a")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        // The two are not equal (different kind); expect Delete then Insert.
        QCOMPARE(ops.size(), 2);
        const bool first_is_delete = ops[0].kind == AstBlockDiff::OpKind::Delete;
        const bool first_is_insert = ops[0].kind == AstBlockDiff::OpKind::Insert;
        QVERIFY2(first_is_delete || first_is_insert, qPrintable(opsToString(ops)));
    }

    void content_edit_in_middle_of_three_blocks() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("B!")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        // Block 0 and block 2 are unchanged; block 1 differs (delete + insert).
        // Expect: Equal, Delete, Insert, Equal (4 ops).
        QCOMPARE(ops.size(), 4);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[3].kind, AstBlockDiff::OpKind::Equal);
    }

    void large_identical_short_circuits_to_all_equals() {
        QList<BlockKey> seq;
        for (int i = 0; i < 50; ++i)
            seq.append(k(BlockKind::Paragraph, QString::number(i)));
        const auto ops = AstBlockDiff::diff(seq, seq);
        QCOMPARE(ops.size(), 50);
        for (const auto &op : ops) {
            QCOMPARE(op.kind, AstBlockDiff::OpKind::Equal);
        }
    }
};

QTEST_APPLESS_MAIN(TstAstBlockDiff)
#include "tst_view_qml_ast_block_diff.moc"
```

- [ ] **Step 4.2: Add to test CMakeLists.txt and confirm build fails**

In `libs/markoff-view-qml/tests/CMakeLists.txt`, add:

```cmake
qt_add_executable(tst_view_qml_ast_block_diff tst_view_qml_ast_block_diff.cpp)
target_link_libraries(tst_view_qml_ast_block_diff PRIVATE markoff_view_qml Qt6::Test)
add_test(NAME tst_view_qml_ast_block_diff COMMAND tst_view_qml_ast_block_diff)
```

```bash
cmake --build build-dev --target tst_view_qml_ast_block_diff -j 8
```
Expected: build fails (`AstBlockDiff.h` doesn't exist).

- [ ] **Step 4.3: Write `AstBlockDiff.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <markoff/view/qml/BlockRecord.h>

namespace Markoff::View::Qml {

/// Pure C++ Myers/LCS diff over BlockKey sequences. Output is a list of
/// edit operations referencing indices in `prev` and `next`. Used by
/// LiveBlockModel to emit the minimal Qt model signal sequence so ListView
/// preserves delegates whose AST block still exists.
class AstBlockDiff {
public:
    enum class OpKind {
        Equal,    ///< prev[prevIndex] == next[nextIndex]; delegate persists
        Insert,   ///< next[nextIndex] is new (no prev counterpart)
        Delete    ///< prev[prevIndex] is gone (no next counterpart)
    };

    struct Op {
        OpKind kind;
        int    prevIndex = -1;   ///< -1 for Insert
        int    nextIndex = -1;   ///< -1 for Delete
    };

    static QList<Op> diff(const QList<BlockKey> &prev,
                          const QList<BlockKey> &next);
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 4.4: Write `AstBlockDiff.cpp`**

A standard Myers/LCS implementation. Build the LCS table; backtrack to produce ops in original order.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AstBlockDiff.h"

#include <vector>

namespace Markoff::View::Qml {

QList<AstBlockDiff::Op> AstBlockDiff::diff(const QList<BlockKey> &prev,
                                           const QList<BlockKey> &next)
{
    const int m = prev.size();
    const int n = next.size();

    // Fast path: identity (avoids O(m*n) on hot path of identical reparses).
    if (m == n) {
        bool same = true;
        for (int i = 0; i < m; ++i) {
            if (prev[i] != next[i]) { same = false; break; }
        }
        if (same) {
            QList<Op> ops;
            ops.reserve(m);
            for (int i = 0; i < m; ++i) {
                ops.append(Op{ OpKind::Equal, i, i });
            }
            return ops;
        }
    }

    // LCS table.
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (prev[i - 1] == next[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    // Backtrack to build ops. Walk from (m, n) back to (0, 0); prepend ops
    // so the result is in forward order.
    QList<Op> ops;
    int i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && prev[i - 1] == next[j - 1]) {
            ops.prepend(Op{ OpKind::Equal, i - 1, j - 1 });
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            ops.prepend(Op{ OpKind::Insert, -1, j - 1 });
            --j;
        } else {
            ops.prepend(Op{ OpKind::Delete, i - 1, -1 });
            --i;
        }
    }
    return ops;
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 4.5: Add to library CMakeLists.txt**

Add `src/AstBlockDiff.cpp` to the library's source list.

- [ ] **Step 4.6: Build + run tests**

```bash
cmake --build build-dev --target tst_view_qml_ast_block_diff -j 8
./build-dev/bin/tst_view_qml_ast_block_diff -platform offscreen
```
Expected: 12 PASS.

- [ ] **Step 4.7: Commit**

```bash
git add libs/markoff-view-qml/src/AstBlockDiff.h \
        libs/markoff-view-qml/src/AstBlockDiff.cpp \
        libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp \
        libs/markoff-view-qml/CMakeLists.txt \
        libs/markoff-view-qml/tests/CMakeLists.txt
git commit -m "feat(view-qml): AstBlockDiff — Myers/LCS over BlockKey sequences"
```

---

## Task 5: `LiveSelectionModel` — cross-block selection state

Mirrors the spike's `SelectionModel`. C++ class, registered as `QML_ELEMENT`, exposed on the QML side.

**Files:**
- Create: `libs/markoff-view-qml/include/markoff/view/qml/LiveSelectionModel.h`
- Create: `libs/markoff-view-qml/src/LiveSelectionModel.cpp`
- Test: `libs/markoff-view-qml/tests/tst_view_qml_live_selection_model.cpp`

- [ ] **Step 5.1: Write the failing test**

Create `libs/markoff-view-qml/tests/tst_view_qml_live_selection_model.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/LiveSelectionModel.h>

using namespace Markoff::View::Qml;

class TstLiveSelectionModel : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initially_no_selection() {
        LiveSelectionModel m;
        QVERIFY(!m.hasSelection());
        QCOMPARE(m.anchorBlock(), -1);
        QCOMPARE(m.activeBlock(), -1);
    }

    void begin_then_extend_in_same_block() {
        LiveSelectionModel m;
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.begin(0, 5);
        QVERIFY(!m.hasSelection());  // zero-length
        QCOMPARE(spy.count(), 1);
        m.extend(0, 12);
        QVERIFY(m.hasSelection());
        QCOMPARE(spy.count(), 2);
        const QPoint r = m.rangeForBlock(0);
        QCOMPARE(r.x(), 5);
        QCOMPARE(r.y(), 12);
    }

    void rangeForBlock_returns_minus_one_for_unselected_blocks() {
        LiveSelectionModel m;
        m.begin(2, 0);
        m.extend(2, 5);
        QCOMPARE(m.rangeForBlock(0), QPoint(-1, -1));
        QCOMPARE(m.rangeForBlock(1), QPoint(-1, -1));
        QCOMPARE(m.rangeForBlock(3), QPoint(-1, -1));
    }

    void multi_block_forward_selection() {
        LiveSelectionModel m;
        m.begin(1, 3);
        m.extend(3, 7);
        QCOMPARE(m.rangeForBlock(1).x(), 3);
        QCOMPARE(m.rangeForBlock(1).y(), INT32_MAX);
        QCOMPARE(m.rangeForBlock(2).x(), 0);
        QCOMPARE(m.rangeForBlock(2).y(), INT32_MAX);
        QCOMPARE(m.rangeForBlock(3).x(), 0);
        QCOMPARE(m.rangeForBlock(3).y(), 7);
    }

    void multi_block_backward_selection_normalizes() {
        LiveSelectionModel m;
        m.begin(3, 7);
        m.extend(1, 3);
        // Should produce same range as forward direction.
        QCOMPARE(m.rangeForBlock(1).x(), 3);
        QCOMPARE(m.rangeForBlock(1).y(), INT32_MAX);
        QCOMPARE(m.rangeForBlock(3).x(), 0);
        QCOMPARE(m.rangeForBlock(3).y(), 7);
    }

    void zero_length_selection_returns_no_range() {
        LiveSelectionModel m;
        m.begin(0, 5);
        // active == anchor; zero-length.
        QCOMPARE(m.rangeForBlock(0), QPoint(-1, -1));
    }

    void clear_resets_state() {
        LiveSelectionModel m;
        m.begin(0, 0);
        m.extend(2, 4);
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.clear();
        QVERIFY(!m.hasSelection());
        QCOMPARE(m.anchorBlock(), -1);
        QCOMPARE(m.activeBlock(), -1);
        QCOMPARE(spy.count(), 1);
    }

    void clear_when_already_empty_does_not_emit() {
        LiveSelectionModel m;
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.clear();
        QCOMPARE(spy.count(), 0);
    }

    void collectSelectedText_single_block() {
        LiveSelectionModel m;
        m.begin(1, 0);
        m.extend(1, 5);
        QStringList blocks = { "abc", "Hello world", "xyz" };
        QCOMPARE(m.collectSelectedText(blocks), QStringLiteral("Hello"));
    }

    void collectSelectedText_multi_block() {
        LiveSelectionModel m;
        m.begin(0, 6);
        m.extend(2, 3);
        QStringList blocks = { "Hello world", "middle", "abcdef" };
        QCOMPARE(m.collectSelectedText(blocks),
                 QStringLiteral("world\nmiddle\nabc"));
    }

    void extend_without_begin_treats_as_begin() {
        LiveSelectionModel m;
        m.extend(2, 4);
        QCOMPARE(m.anchorBlock(), 2);
        QCOMPARE(m.anchorOffset(), 4);
        QCOMPARE(m.activeBlock(), 2);
        QCOMPARE(m.activeOffset(), 4);
    }

    void no_emit_when_extend_repeats_same_position() {
        LiveSelectionModel m;
        m.begin(0, 5);
        QSignalSpy spy(&m, &LiveSelectionModel::selectionChanged);
        m.extend(0, 5);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_APPLESS_MAIN(TstLiveSelectionModel)
#include "tst_view_qml_live_selection_model.moc"
```

- [ ] **Step 5.2: Add to test CMakeLists.txt + confirm build fails**

```cmake
qt_add_executable(tst_view_qml_live_selection_model tst_view_qml_live_selection_model.cpp)
target_link_libraries(tst_view_qml_live_selection_model PRIVATE markoff_view_qml Qt6::Test)
add_test(NAME tst_view_qml_live_selection_model COMMAND tst_view_qml_live_selection_model)
```

```bash
cmake --build build-dev --target tst_view_qml_live_selection_model -j 8
```
Expected: build fails (`LiveSelectionModel.h` doesn't exist).

- [ ] **Step 5.3: Write `LiveSelectionModel.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <qqmlintegration.h>

namespace Markoff::View::Qml {

/// Cross-block selection state. Mirrors the spike's SelectionModel verbatim
/// (see docs/specs/2026-04-29-cross-block-selection-spike-findings.md §4).
///
/// Contract for QML consumers:
///   - rangeForBlock returns QPoint(-1,-1) for unselected blocks.
///   - For selected blocks the y component MAY equal INT32_MAX as a
///     "to end of block" sentinel. CONSUMERS MUST CLAMP via
///     min(y, textEdit.length) before calling TextEdit.select(start, end),
///     because TextEdit.select silently no-ops if end > text.length.
class LiveSelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int  anchorBlock  READ anchorBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int  anchorOffset READ anchorOffset NOTIFY selectionChanged)
    Q_PROPERTY(int  activeBlock  READ activeBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int  activeOffset READ activeOffset NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit LiveSelectionModel(QObject *parent = nullptr);

    int anchorBlock()  const { return m_anchorBlock; }
    int anchorOffset() const { return m_anchorOffset; }
    int activeBlock()  const { return m_activeBlock; }
    int activeOffset() const { return m_activeOffset; }
    bool hasSelection() const;

    Q_INVOKABLE void begin(int block, int offset);
    Q_INVOKABLE void extend(int block, int offset);
    Q_INVOKABLE void clear();

    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;
    Q_INVOKABLE QString collectSelectedText(const QStringList &blockTexts) const;
    Q_INVOKABLE void copySelectionToClipboard(const QStringList &blockTexts) const;

Q_SIGNALS:
    void selectionChanged();

private:
    void normalized(int &fb, int &fo, int &lb, int &lo) const;

    int m_anchorBlock  = -1;
    int m_anchorOffset = -1;
    int m_activeBlock  = -1;
    int m_activeOffset = -1;
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 5.4: Write `LiveSelectionModel.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveSelectionModel.h>

#include <QClipboard>
#include <QGuiApplication>

namespace Markoff::View::Qml {

LiveSelectionModel::LiveSelectionModel(QObject *parent) : QObject(parent) {}

bool LiveSelectionModel::hasSelection() const
{
    if (m_anchorBlock < 0) return false;
    if (m_anchorBlock != m_activeBlock) return true;
    return m_anchorOffset != m_activeOffset;
}

void LiveSelectionModel::begin(int block, int offset)
{
    m_anchorBlock = block;
    m_anchorOffset = offset;
    m_activeBlock = block;
    m_activeOffset = offset;
    Q_EMIT selectionChanged();
}

void LiveSelectionModel::extend(int block, int offset)
{
    if (m_anchorBlock < 0) {
        begin(block, offset);
        return;
    }
    if (m_activeBlock == block && m_activeOffset == offset) return;
    m_activeBlock = block;
    m_activeOffset = offset;
    Q_EMIT selectionChanged();
}

void LiveSelectionModel::clear()
{
    if (m_anchorBlock < 0) return;
    m_anchorBlock = m_anchorOffset = m_activeBlock = m_activeOffset = -1;
    Q_EMIT selectionChanged();
}

void LiveSelectionModel::normalized(int &fb, int &fo, int &lb, int &lo) const
{
    if (m_anchorBlock < m_activeBlock ||
        (m_anchorBlock == m_activeBlock && m_anchorOffset <= m_activeOffset)) {
        fb = m_anchorBlock; fo = m_anchorOffset;
        lb = m_activeBlock; lo = m_activeOffset;
    } else {
        fb = m_activeBlock; fo = m_activeOffset;
        lb = m_anchorBlock; lo = m_anchorOffset;
    }
}

QPoint LiveSelectionModel::rangeForBlock(int blockIndex) const
{
    if (!hasSelection()) return QPoint(-1, -1);
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    if (blockIndex < fb || blockIndex > lb) return QPoint(-1, -1);
    if (fb == lb) return QPoint(fo, lo);
    if (blockIndex == fb) return QPoint(fo, INT32_MAX);
    if (blockIndex == lb) return QPoint(0, lo);
    return QPoint(0, INT32_MAX);
}

QString LiveSelectionModel::collectSelectedText(const QStringList &blockTexts) const
{
    if (!hasSelection()) return {};
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    if (fb < 0 || lb >= blockTexts.size()) return {};
    if (fb == lb) {
        return blockTexts.at(fb).mid(fo, lo - fo);
    }
    QString out;
    out += blockTexts.at(fb).mid(fo);
    out += QChar('\n');
    for (int i = fb + 1; i < lb; ++i) {
        out += blockTexts.at(i);
        out += QChar('\n');
    }
    out += blockTexts.at(lb).left(lo);
    return out;
}

void LiveSelectionModel::copySelectionToClipboard(const QStringList &blockTexts) const
{
    const QString txt = collectSelectedText(blockTexts);
    if (txt.isEmpty()) return;
    QGuiApplication::clipboard()->setText(txt);
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 5.5: Add to library CMakeLists.txt + QML module**

Add `src/LiveSelectionModel.cpp` to the library's source list. Ensure `LiveSelectionModel.h` is registered with the QML module (the existing `qt_add_qml_module` call should pick up `QML_ELEMENT` automatically via the `SOURCES` list).

- [ ] **Step 5.6: Build + run tests**

```bash
cmake --build build-dev --target tst_view_qml_live_selection_model -j 8
./build-dev/bin/tst_view_qml_live_selection_model -platform offscreen
```
Expected: 12 PASS.

- [ ] **Step 5.7: Commit**

```bash
git add libs/markoff-view-qml/include/markoff/view/qml/LiveSelectionModel.h \
        libs/markoff-view-qml/src/LiveSelectionModel.cpp \
        libs/markoff-view-qml/tests/tst_view_qml_live_selection_model.cpp \
        libs/markoff-view-qml/CMakeLists.txt \
        libs/markoff-view-qml/tests/CMakeLists.txt
git commit -m "feat(view-qml): LiveSelectionModel — cross-block selection (per spike)"
```

---

## Task 6: `LiveBlockModel` — `QAbstractListModel` over BlockRecord rows

Applies `AstBlockDiff::Op` lists as Qt model signals.

**Files:**
- Create: `libs/markoff-view-qml/include/markoff/view/qml/LiveBlockModel.h`
- Create: `libs/markoff-view-qml/src/LiveBlockModel.cpp`
- Test: `libs/markoff-view-qml/tests/tst_view_qml_live_block_model.cpp`

- [ ] **Step 6.1: Write the failing test**

Create `libs/markoff-view-qml/tests/tst_view_qml_live_block_model.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;

namespace {

BlockRecord para(const QString &t) {
    BlockRecord r;
    r.kind = BlockKind::Paragraph;
    r.source = t;
    r.text = t;
    return r;
}
BlockRecord heading(int level, const QString &t) {
    BlockRecord r;
    r.kind = BlockKind::Heading;
    r.headingLevel = level;
    r.text = t;
    r.source = QString(level, QChar('#')) + QChar(' ') + t;
    return r;
}

}  // namespace

class TstLiveBlockModel : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_initially() {
        LiveBlockModel m;
        QCOMPARE(m.rowCount(), 0);
    }

    void setRecords_populates_rows() {
        LiveBlockModel m;
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.setRecords({ para("a"), para("b"), heading(1, "T") });
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(reset.count(), 1);
    }

    void roleNames_includes_kind_and_text() {
        LiveBlockModel m;
        const auto names = m.roleNames();
        const QList<QByteArray> values = names.values();
        QVERIFY(values.contains("kind"));
        QVERIFY(values.contains("text"));
        QVERIFY(values.contains("headingLevel"));
        QVERIFY(values.contains("imageSrc"));
        QVERIFY(values.contains("imageAlt"));
        QVERIFY(values.contains("imageTitle"));
        QVERIFY(values.contains("codeLanguage"));
        QVERIFY(values.contains("codeText"));
    }

    void data_returns_kind_and_text() {
        LiveBlockModel m;
        m.setRecords({ heading(2, "Hello") });
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("kind")).toString(), BlockKind::Heading);
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("text")).toString(), QStringLiteral("Hello"));
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("headingLevel")).toInt(), 2);
    }

    void applyOps_pure_inserts_emits_correct_signals() {
        LiveBlockModel m;
        QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
        m.setRecords({});  // start empty
        m.applyOps(
            { { AstBlockDiff::OpKind::Insert, -1, 0 },
              { AstBlockDiff::OpKind::Insert, -1, 1 } },
            { para("a"), para("b") });
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(ins.count(), 2);  // two consecutive inserts
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("text")).toString(), QStringLiteral("a"));
        QCOMPARE(m.data(m.index(1, 0), m.roleForName("text")).toString(), QStringLiteral("b"));
    }

    void applyOps_pure_deletes_emits_correct_signals() {
        LiveBlockModel m;
        m.setRecords({ para("a"), para("b") });
        QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
        m.applyOps(
            { { AstBlockDiff::OpKind::Delete, 0, -1 },
              { AstBlockDiff::OpKind::Delete, 1, -1 } },
            {});
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(rem.count(), 2);
    }

    void applyOps_equal_ops_keep_rows_unchanged_no_signals() {
        LiveBlockModel m;
        m.setRecords({ para("a"), para("b") });
        QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
        QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
        m.applyOps(
            { { AstBlockDiff::OpKind::Equal, 0, 0 },
              { AstBlockDiff::OpKind::Equal, 1, 1 } },
            { para("a"), para("b") });
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(ins.count(), 0);
        QCOMPARE(rem.count(), 0);
        QCOMPARE(chg.count(), 0);
    }

    void applyOps_replace_in_middle_emits_minimal_diff() {
        LiveBlockModel m;
        m.setRecords({ para("a"), para("b"), para("c") });
        QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
        m.applyOps(
            { { AstBlockDiff::OpKind::Equal,  0, 0 },
              { AstBlockDiff::OpKind::Delete, 1, -1 },
              { AstBlockDiff::OpKind::Insert, -1, 1 },
              { AstBlockDiff::OpKind::Equal,  2, 2 } },
            { para("a"), para("B!"), para("c") });
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(rem.count(), 1);
        QCOMPARE(ins.count(), 1);
        QCOMPARE(m.data(m.index(1, 0), m.roleForName("text")).toString(), QStringLiteral("B!"));
    }

    void applyOps_image_record_carries_role_data() {
        LiveBlockModel m;
        BlockRecord img;
        img.kind = BlockKind::Image;
        img.imageSrc = "u.png";
        img.imageAlt = "alt";
        img.source = "![alt](u.png)";
        m.setRecords({ img });
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("imageSrc")).toString(), QStringLiteral("u.png"));
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("imageAlt")).toString(), QStringLiteral("alt"));
    }

    void applyOps_codeblock_record_carries_role_data() {
        LiveBlockModel m;
        BlockRecord cb;
        cb.kind = BlockKind::CodeBlock;
        cb.codeLanguage = "rust";
        cb.codeText = "fn main(){}";
        cb.source = "```rust\nfn main(){}\n```";
        m.setRecords({ cb });
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("codeLanguage")).toString(), QStringLiteral("rust"));
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("codeText")).toString(), QStringLiteral("fn main(){}"));
    }
};

QTEST_APPLESS_MAIN(TstLiveBlockModel)
#include "tst_view_qml_live_block_model.moc"
```

- [ ] **Step 6.2: Add test target + confirm build fails**

In `libs/markoff-view-qml/tests/CMakeLists.txt`:

```cmake
qt_add_executable(tst_view_qml_live_block_model tst_view_qml_live_block_model.cpp)
target_link_libraries(tst_view_qml_live_block_model PRIVATE markoff_view_qml Qt6::Test)
add_test(NAME tst_view_qml_live_block_model COMMAND tst_view_qml_live_block_model)
```

```bash
cmake --build build-dev --target tst_view_qml_live_block_model -j 8
```
Expected: build fails.

- [ ] **Step 6.3: Write `LiveBlockModel.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

#include <markoff/view/qml/BlockRecord.h>
#include "../../../src/AstBlockDiff.h"  // private header — internal cross-include OK
// Note: in the actual file, prefer including a forward-decl friendly alias.
// We use a relative include for now since AstBlockDiff is an internal helper.

namespace Markoff::View::Qml {

class LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        TextRole,
        SourceRole,
        HeadingLevelRole,
        ImageSrcRole,
        ImageAltRole,
        ImageTitleRole,
        CodeLanguageRole,
        CodeTextRole
    };

    explicit LiveBlockModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Convenience for tests / consumers.
    Q_INVOKABLE int roleForName(const QByteArray &name) const;

    /// Replace all rows. Emits modelReset.
    void setRecords(const QList<BlockRecord> &records);

    /// Apply a diff op sequence relative to `nextRecords`. Op semantics:
    ///   Equal(prev,next):  no model signal; row stays. Verifies role data
    ///                      matches `nextRecords[next]`; if not, a
    ///                      `dataChanged` is emitted. (This handles the
    ///                      case where two BlockKeys are equal but the
    ///                      kind-specific role data differs — rare, but
    ///                      defensive.)
    ///   Insert(_, next):   beginInsertRows + insert nextRecords[next] + end.
    ///   Delete(prev, _):   beginRemoveRows + remove + end.
    /// Caller passes `nextRecords` so we can populate inserted rows.
    void applyOps(const QList<AstBlockDiff::Op> &ops,
                  const QList<BlockRecord> &nextRecords);

    /// Read-only access to a row's record (for tests + selection logic).
    const BlockRecord &recordAt(int row) const { return m_rows.at(row); }

private:
    QList<BlockRecord> m_rows;
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 6.4: Write `LiveBlockModel.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveBlockModel.h>

namespace Markoff::View::Qml {

LiveBlockModel::LiveBlockModel(QObject *parent) : QAbstractListModel(parent) {}

int LiveBlockModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_rows.size();
}

QHash<int, QByteArray> LiveBlockModel::roleNames() const
{
    return {
        { KindRole,         "kind" },
        { TextRole,         "text" },
        { SourceRole,       "source" },
        { HeadingLevelRole, "headingLevel" },
        { ImageSrcRole,     "imageSrc" },
        { ImageAltRole,     "imageAlt" },
        { ImageTitleRole,   "imageTitle" },
        { CodeLanguageRole, "codeLanguage" },
        { CodeTextRole,     "codeText" },
    };
}

int LiveBlockModel::roleForName(const QByteArray &name) const
{
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == name) return it.key();
    }
    return -1;
}

QVariant LiveBlockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const BlockRecord &r = m_rows[index.row()];
    switch (role) {
        case KindRole:         return r.kind;
        case TextRole:         return r.text;
        case SourceRole:       return r.source;
        case HeadingLevelRole: return r.headingLevel;
        case ImageSrcRole:     return r.imageSrc;
        case ImageAltRole:     return r.imageAlt;
        case ImageTitleRole:   return r.imageTitle;
        case CodeLanguageRole: return r.codeLanguage;
        case CodeTextRole:     return r.codeText;
        default:               return {};
    }
}

void LiveBlockModel::setRecords(const QList<BlockRecord> &records)
{
    beginResetModel();
    m_rows = records;
    endResetModel();
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords)
{
    // Walk ops in order. Track current row position in m_rows as we mutate.
    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                // Defensive role-data sync.
                if (row < m_rows.size() && op.nextIndex < nextRecords.size()) {
                    if (m_rows[row] != nextRecords[op.nextIndex]) {
                        m_rows[row] = nextRecords[op.nextIndex];
                        const QModelIndex idx = index(row, 0);
                        Q_EMIT dataChanged(idx, idx);
                    }
                }
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Insert: {
                beginInsertRows(QModelIndex(), row, row);
                m_rows.insert(row, nextRecords[op.nextIndex]);
                endInsertRows();
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Delete: {
                beginRemoveRows(QModelIndex(), row, row);
                m_rows.removeAt(row);
                endRemoveRows();
                // do NOT increment row — next op references the now-shifted index
                break;
            }
        }
    }
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 6.5: Add to library CMakeLists.txt + QML module**

Add `src/LiveBlockModel.cpp` to the library's source list. The QML element registration follows from `QML_ELEMENT` + `QML_UNCREATABLE` macros.

- [ ] **Step 6.6: Build + run tests**

```bash
cmake --build build-dev --target tst_view_qml_live_block_model -j 8
./build-dev/bin/tst_view_qml_live_block_model -platform offscreen
```
Expected: 10 PASS.

- [ ] **Step 6.7: Commit**

```bash
git add libs/markoff-view-qml/include/markoff/view/qml/LiveBlockModel.h \
        libs/markoff-view-qml/src/LiveBlockModel.cpp \
        libs/markoff-view-qml/tests/tst_view_qml_live_block_model.cpp \
        libs/markoff-view-qml/CMakeLists.txt \
        libs/markoff-view-qml/tests/CMakeLists.txt
git commit -m "feat(view-qml): LiveBlockModel — QAbstractListModel applying AstBlockDiff ops"
```

---

## Task 7: `LiveListModelBinding` — wire EditorBackend → BlockWalker → diff → model

The integration glue. Subscribes to `EditorBackend::parseUpdatedAt`. Owns `LiveBlockModel` + `LiveSelectionModel`. Clears selection if any selected block disappears.

**Files:**
- Create: `libs/markoff-view-qml/include/markoff/view/qml/LiveListModelBinding.h`
- Create: `libs/markoff-view-qml/src/LiveListModelBinding.cpp`
- Test: `libs/markoff-view-qml/tests/tst_view_qml_live_list_model_binding.cpp`

- [ ] **Step 7.1: Write the failing test**

Create `libs/markoff-view-qml/tests/tst_view_qml_live_list_model_binding.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSelectionModel.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff::View::Qml;

class TstLiveListModelBinding : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void model_populates_after_initial_parse() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("# Title\n\nFirst paragraph.\n");
        doc.applyLocalEdit({ ed });

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveBlockModel *model = binding.model();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->data(model->index(0, 0), model->roleForName("kind")).toString(),
                 QStringLiteral("heading"));
        QCOMPARE(model->data(model->index(1, 0), model->roleForName("kind")).toString(),
                 QStringLiteral("paragraph"));
    }

    void single_block_edit_emits_dataChanged_only() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("# Title\n\npara\n");
        doc.applyLocalEdit({ ed });
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveBlockModel *model = binding.model();
        QSignalSpy ins(model, &QAbstractItemModel::rowsInserted);
        QSignalSpy rem(model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy chg(model, &QAbstractItemModel::dataChanged);

        // Edit the paragraph: insert "!" at end of "para".
        // Resulting source:  "# Title\n\npara!\n"
        Markoff::MarkoffEdit edit;
        edit.oldStart = 13;  // byte offset after "para"
        edit.oldEnd   = 13;
        edit.newText  = QByteArrayLiteral("!");
        doc.applyLocalEdit({ edit });

        // Wait for the next parse.
        parseSpy.wait(2000);

        // The diff replaces the second paragraph with the edited one — one
        // delete + one insert at row 1, equivalent to a dataChanged result.
        // Either pattern is acceptable: 1 dataChanged OR 1 remove + 1 insert.
        const int touched = chg.count() + ins.count() + rem.count();
        QVERIFY2(touched >= 1 && touched <= 2,
                 qPrintable(QString("expected 1-2 model touches, got %1").arg(touched)));
        QCOMPARE(model->rowCount(), 2);
    }

    void selection_clears_when_touched_block_removed() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("a\n\nb\n\nc\n");
        doc.applyLocalEdit({ ed });
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveSelectionModel *sel = binding.selectionModel();
        QVERIFY(sel != nullptr);
        sel->begin(1, 0);  // select start of second block
        sel->extend(1, 1);
        QVERIFY(sel->hasSelection());

        // Remove the second paragraph entirely.
        Markoff::MarkoffEdit edit;
        edit.oldStart = 3;   // bytes "b\n\n" start
        edit.oldEnd   = 6;
        edit.newText  = QByteArray();
        doc.applyLocalEdit({ edit });
        parseSpy.wait(2000);

        QVERIFY(!sel->hasSelection());
    }

    void selection_persists_when_unrelated_block_changes() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral("a\n\nb\n\nc\n");
        doc.applyLocalEdit({ ed });
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveSelectionModel *sel = binding.selectionModel();
        sel->begin(2, 0);  // select start of third block
        sel->extend(2, 1);

        // Edit the FIRST paragraph (block 0), unrelated to selection.
        Markoff::MarkoffEdit edit;
        edit.oldStart = 1;  // after "a"
        edit.oldEnd   = 1;
        edit.newText  = QByteArrayLiteral("!");
        doc.applyLocalEdit({ edit });
        parseSpy.wait(2000);

        QVERIFY(sel->hasSelection());
    }
};

QTEST_MAIN(TstLiveListModelBinding)
#include "tst_view_qml_live_list_model_binding.moc"
```

(Note: this test uses `QTEST_MAIN` rather than `QTEST_APPLESS_MAIN` because `EditorBackend` + the parse pool need a real `QGuiApplication` event loop.)

- [ ] **Step 7.2: Add test target + confirm build fails**

```cmake
qt_add_executable(tst_view_qml_live_list_model_binding tst_view_qml_live_list_model_binding.cpp)
target_link_libraries(tst_view_qml_live_list_model_binding PRIVATE markoff_view_qml markoff_foundation Qt6::Test)
add_test(NAME tst_view_qml_live_list_model_binding COMMAND tst_view_qml_live_list_model_binding)
```

```bash
cmake --build build-dev --target tst_view_qml_live_list_model_binding -j 8
```
Expected: build fails (`LiveListModelBinding.h` missing).

- [ ] **Step 7.3: Write `LiveListModelBinding.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include <markoff/view/qml/EditorBackend.h>

namespace Markoff { class Document; }  // markoff-parser

namespace Markoff::View::Qml {

class LiveBlockModel;
class LiveSelectionModel;

/// Drives a LiveBlockModel from EditorBackend::parseUpdatedAt. On each parse,
/// runs BlockWalker on the doc's source markdown, diffs the new block list
/// against the current model rows, applies the resulting ops, and clears
/// LiveSelectionModel if any selected block disappears.
class LiveListModelBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorBackend *editorBackend
               READ editorBackend WRITE setEditorBackend NOTIFY editorBackendChanged)
    Q_PROPERTY(LiveBlockModel *model
               READ model CONSTANT)
    Q_PROPERTY(LiveSelectionModel *selectionModel
               READ selectionModel CONSTANT)

public:
    explicit LiveListModelBinding(QObject *parent = nullptr);
    ~LiveListModelBinding() override;

    EditorBackend *editorBackend() const;
    void setEditorBackend(EditorBackend *eb);

    LiveBlockModel *model() const;
    LiveSelectionModel *selectionModel() const;

Q_SIGNALS:
    void editorBackendChanged();

private:
    void onParseUpdatedAt(const Markoff::Document *parsed, quint64 atVersion);

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 7.4: Write `LiveListModelBinding.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveListModelBinding.h>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSelectionModel.h>
#include "BlockWalker.h"
#include "AstBlockDiff.h"

#include <markoff-parser/Document.h>
#include <markoff-foundation/MarkoffDocument.h>

#include <QSet>

namespace Markoff::View::Qml {

struct LiveListModelBinding::Private {
    EditorBackend       *editorBackend = nullptr;
    LiveBlockModel      *model         = nullptr;
    LiveSelectionModel  *selection     = nullptr;
    QList<BlockKey>      lastKeys;     ///< previous block-key list (for diff)
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model = new LiveBlockModel(this);
    d->selection = new LiveSelectionModel(this);
}

LiveListModelBinding::~LiveListModelBinding() = default;

EditorBackend *LiveListModelBinding::editorBackend() const { return d->editorBackend; }
LiveBlockModel *LiveListModelBinding::model() const { return d->model; }
LiveSelectionModel *LiveListModelBinding::selectionModel() const { return d->selection; }

void LiveListModelBinding::setEditorBackend(EditorBackend *eb)
{
    if (d->editorBackend == eb) return;
    if (d->editorBackend) {
        QObject::disconnect(d->editorBackend, nullptr, this, nullptr);
    }
    d->editorBackend = eb;
    if (d->editorBackend) {
        QObject::connect(d->editorBackend, &EditorBackend::parseUpdatedAt,
                         this, [this](QVariant parsedVar, quint64 atVersion) {
            const Markoff::Document *parsed =
                parsedVar.value<const Markoff::Document *>();
            this->onParseUpdatedAt(parsed, atVersion);
        });
    }
    Q_EMIT editorBackendChanged();
}

void LiveListModelBinding::onParseUpdatedAt(const Markoff::Document *parsed,
                                            quint64 /*atVersion*/)
{
    if (!parsed) return;
    const QString source = parsed->sourceText();
    const QList<BlockRecord> nextRecords = BlockWalker::walk(source);

    QList<BlockKey> nextKeys;
    nextKeys.reserve(nextRecords.size());
    for (const auto &r : nextRecords) {
        nextKeys.append(BlockKey { r.kind, r.source });
    }

    const QList<AstBlockDiff::Op> ops = AstBlockDiff::diff(d->lastKeys, nextKeys);

    // If the selection's anchor or active block is removed by this diff,
    // clear the selection. Build a set of prev indices that are touched
    // by Delete ops — if either selection block index is in there, clear.
    if (d->selection->hasSelection()) {
        QSet<int> deletedPrevIndices;
        for (const auto &op : ops) {
            if (op.kind == AstBlockDiff::OpKind::Delete) {
                deletedPrevIndices.insert(op.prevIndex);
            }
        }
        const int aB = d->selection->anchorBlock();
        const int actB = d->selection->activeBlock();
        if (deletedPrevIndices.contains(aB) || deletedPrevIndices.contains(actB)) {
            d->selection->clear();
        }
    }

    d->model->applyOps(ops, nextRecords);
    d->lastKeys = nextKeys;
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 7.5: Add to library CMakeLists.txt + QML module**

Add `src/LiveListModelBinding.cpp` to the library's source list.

- [ ] **Step 7.6: Build + run tests**

```bash
cmake --build build-dev --target tst_view_qml_live_list_model_binding -j 8
./build-dev/bin/tst_view_qml_live_list_model_binding -platform offscreen
```
Expected: 4 PASS.

- [ ] **Step 7.7: Commit**

```bash
git add libs/markoff-view-qml/include/markoff/view/qml/LiveListModelBinding.h \
        libs/markoff-view-qml/src/LiveListModelBinding.cpp \
        libs/markoff-view-qml/tests/tst_view_qml_live_list_model_binding.cpp \
        libs/markoff-view-qml/CMakeLists.txt \
        libs/markoff-view-qml/tests/CMakeLists.txt
git commit -m "feat(view-qml): LiveListModelBinding — parseUpdatedAt → walker → diff → model"
```

---

## Task 8: `LiveContextMenuHandler` — KDAB Widget bridge

A `QObject` exposed to QML that owns a `QMenu` (a `QWidget`) shown as a top-level OS window on right-click. v0 actions: Copy, Select All.

**Files:**
- Create: `libs/markoff-view-qml/include/markoff/view/qml/LiveContextMenuHandler.h`
- Create: `libs/markoff-view-qml/src/LiveContextMenuHandler.cpp`

No standalone test file — exercised by the smoke test in Task 12.

- [ ] **Step 8.1: Write `LiveContextMenuHandler.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <memory>
#include <qqmlintegration.h>

class QMenu;

namespace Markoff::View::Qml {

class LiveSelectionModel;

/// KDAB-pattern Widget-window bridge for the right-click context menu.
/// Owns a QMenu that shows as a top-level OS window when popup() is invoked.
/// v0 actions: Copy, Select All. The menu is created with no parent so it
/// shows as a real native menu independent of the QQuickWindow.
class LiveContextMenuHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(LiveSelectionModel *selectionModel
               READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    Q_PROPERTY(QStringList blockTexts
               READ blockTexts WRITE setBlockTexts NOTIFY blockTextsChanged)
    Q_PROPERTY(int blockCount
               READ blockCount WRITE setBlockCount NOTIFY blockCountChanged)

public:
    explicit LiveContextMenuHandler(QObject *parent = nullptr);
    ~LiveContextMenuHandler() override;

    LiveSelectionModel *selectionModel() const { return m_selection; }
    void setSelectionModel(LiveSelectionModel *m);

    QStringList blockTexts() const { return m_blockTexts; }
    void setBlockTexts(const QStringList &t);

    int blockCount() const { return m_blockCount; }
    void setBlockCount(int n);

    /// Pop the menu at the given GLOBAL screen coordinates.
    Q_INVOKABLE void popup(QPoint globalPos);

Q_SIGNALS:
    void selectionModelChanged();
    void blockTextsChanged();
    void blockCountChanged();

private:
    LiveSelectionModel  *m_selection = nullptr;
    QStringList          m_blockTexts;
    int                  m_blockCount = 0;
    std::unique_ptr<QMenu> m_menu;
};

}  // namespace Markoff::View::Qml
```

- [ ] **Step 8.2: Write `LiveContextMenuHandler.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveContextMenuHandler.h>

#include <markoff/view/qml/LiveSelectionModel.h>

#include <QAction>
#include <QMenu>

namespace Markoff::View::Qml {

LiveContextMenuHandler::LiveContextMenuHandler(QObject *parent)
    : QObject(parent)
    , m_menu(std::make_unique<QMenu>())  // no parent — top-level OS window
{
    QAction *copyAction = m_menu->addAction(tr("Copy"));
    QObject::connect(copyAction, &QAction::triggered, this, [this]() {
        if (m_selection) m_selection->copySelectionToClipboard(m_blockTexts);
    });

    QAction *selectAllAction = m_menu->addAction(tr("Select All"));
    QObject::connect(selectAllAction, &QAction::triggered, this, [this]() {
        if (!m_selection || m_blockCount <= 0) return;
        m_selection->begin(0, 0);
        const int lastIdx = m_blockCount - 1;
        const int lastLen = (lastIdx < m_blockTexts.size())
            ? m_blockTexts.at(lastIdx).size() : 0;
        m_selection->extend(lastIdx, lastLen);
    });
}

LiveContextMenuHandler::~LiveContextMenuHandler() = default;

void LiveContextMenuHandler::setSelectionModel(LiveSelectionModel *m)
{
    if (m_selection == m) return;
    m_selection = m;
    Q_EMIT selectionModelChanged();
}

void LiveContextMenuHandler::setBlockTexts(const QStringList &t)
{
    if (m_blockTexts == t) return;
    m_blockTexts = t;
    Q_EMIT blockTextsChanged();
}

void LiveContextMenuHandler::setBlockCount(int n)
{
    if (m_blockCount == n) return;
    m_blockCount = n;
    Q_EMIT blockCountChanged();
}

void LiveContextMenuHandler::popup(QPoint globalPos)
{
    m_menu->popup(globalPos);
}

}  // namespace Markoff::View::Qml
```

- [ ] **Step 8.3: Add to library CMakeLists.txt + QML module + build**

Add `src/LiveContextMenuHandler.cpp` to the source list.

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```
Expected: clean build.

- [ ] **Step 8.4: Commit**

```bash
git add libs/markoff-view-qml/include/markoff/view/qml/LiveContextMenuHandler.h \
        libs/markoff-view-qml/src/LiveContextMenuHandler.cpp \
        libs/markoff-view-qml/CMakeLists.txt
git commit -m "feat(view-qml): LiveContextMenuHandler — KDAB Widget-bridged QMenu"
```

---

## Task 9: QML delegates (5 files)

Each delegate is small; combined into one task because they're structurally similar and their tests are integration-only via the smoke test in Task 12.

**Files:**
- Create: `libs/markoff-view-qml/qml/delegates/ParagraphDelegate.qml`
- Create: `libs/markoff-view-qml/qml/delegates/HeadingDelegate.qml`
- Create: `libs/markoff-view-qml/qml/delegates/HorizontalRuleDelegate.qml`
- Create: `libs/markoff-view-qml/qml/delegates/ImageDelegate.qml`
- Create: `libs/markoff-view-qml/qml/delegates/CodeBlockDelegate.qml`

- [ ] **Step 9.1: Write `ParagraphDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Paragraph block delegate. Renders inline formatting via
/// Qt's MarkdownText. Selection is driven externally by LiveSelectionModel
/// — selectByMouse is OFF; per-Connections binding applies the slice.
TextEdit {
    id: textEdit

    required property int    blockIndex
    required property string text
    required property var    selectionModel  // LiveSelectionModel *

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12

    text: parent ? parent.text : ""
    textFormat: TextEdit.MarkdownText
    readOnly: true
    selectByMouse: false
    wrapMode: TextEdit.Wrap
    font.pixelSize: 16

    Connections {
        target: textEdit.selectionModel
        function onSelectionChanged() {
            const r = textEdit.selectionModel.rangeForBlock(textEdit.blockIndex)
            if (r.x === -1) {
                textEdit.deselect()
            } else {
                const end = Math.min(r.y, textEdit.length)
                textEdit.select(r.x, end)
            }
        }
    }
}
```

(Note: the binding `text: parent ? parent.text : ""` is wrong because `parent.text` doesn't exist on a delegate's parent. Replace with the literal role name. Corrected: since `text` is a required property on this Item AND the property name conflicts with TextEdit's own `text` property, we either rename the required prop to `blockText` OR omit the explicit binding and rely on TextEdit's `text` being driven by the model role's mapping. **Use `blockText` for the required prop and bind `text: blockText`.**)

Corrected:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

TextEdit {
    id: textEdit

    required property int    blockIndex
    required property string blockText
    required property var    selectionModel  // LiveSelectionModel *

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12

    text: textEdit.blockText
    textFormat: TextEdit.MarkdownText
    readOnly: true
    selectByMouse: false
    wrapMode: TextEdit.Wrap
    font.pixelSize: 16

    Connections {
        target: textEdit.selectionModel
        function onSelectionChanged() {
            const r = textEdit.selectionModel.rangeForBlock(textEdit.blockIndex)
            if (r.x === -1) {
                textEdit.deselect()
            } else {
                const end = Math.min(r.y, textEdit.length)
                textEdit.select(r.x, end)
            }
        }
    }
}
```

- [ ] **Step 9.2: Write `HeadingDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

TextEdit {
    id: textEdit

    required property int    blockIndex
    required property string blockText
    required property int    headingLevel
    required property var    selectionModel

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12

    text: textEdit.blockText
    textFormat: TextEdit.PlainText  // plain — we control sizing via font
    readOnly: true
    selectByMouse: false
    wrapMode: TextEdit.Wrap
    font.pixelSize: {
        switch (textEdit.headingLevel) {
            case 1: return 28
            case 2: return 24
            case 3: return 20
            case 4: return 18
            case 5: return 16
            case 6: return 14
            default: return 16
        }
    }
    font.bold: true

    Connections {
        target: textEdit.selectionModel
        function onSelectionChanged() {
            const r = textEdit.selectionModel.rangeForBlock(textEdit.blockIndex)
            if (r.x === -1) {
                textEdit.deselect()
            } else {
                const end = Math.min(r.y, textEdit.length)
                textEdit.select(r.x, end)
            }
        }
    }
}
```

- [ ] **Step 9.3: Write `HorizontalRuleDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    height: 16

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: "#888"
    }
}
```

- [ ] **Step 9.4: Write `ImageDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root

    required property string imageSrc
    required property string imageAlt
    required property string imageTitle

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
        visible: image.status !== Image.Ready
        anchors.fill: parent
        color: "#222"
        Text {
            anchors.centerIn: parent
            color: "#ccc"
            text: root.imageAlt.length > 0
                ? "[image: " + root.imageAlt + "]"
                : "[image: " + root.imageSrc + "]"
            font.italic: true
        }
    }
}
```

- [ ] **Step 9.5: Write `CodeBlockDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.kde.syntaxhighlighting

Rectangle {
    id: root

    required property int    blockIndex
    required property string codeLanguage
    required property string codeText
    required property var    selectionModel

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    color: "#1e1e1e"
    radius: 4
    implicitHeight: textEdit.implicitHeight + 16

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
        color: "#dcdcdc"

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

- [ ] **Step 9.6: Add all five QML files to `qt_add_qml_module` in library CMakeLists.txt**

In `libs/markoff-view-qml/CMakeLists.txt`, find the existing `qt_add_qml_module` call and add the five delegate files to its `QML_FILES` list.

- [ ] **Step 9.7: Build**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```
Expected: clean build, no QML syntax errors at compile time.

- [ ] **Step 9.8: Commit**

```bash
git add libs/markoff-view-qml/qml/delegates/ libs/markoff-view-qml/CMakeLists.txt
git commit -m "feat(view-qml): five v0 block delegates (paragraph, heading, hr, image, code)"
```

---

## Task 10: `LiveView.qml` — top-level component

The integration point. ListView + DelegateChooser + the spike's `hit()` MouseArea + right-click → context-menu handler.

**Files:**
- Create: `libs/markoff-view-qml/qml/LiveView.qml`

- [ ] **Step 10.1: Write `LiveView.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Qt.labs.qmlmodels

import org.markoff.view.qml

import "delegates"

/// Read-only Live render of a markdown document. Sibling of SourceEditor.qml
/// inside MarkoffEditor.qml.
///
/// External property contract:
///   - editorBackend : EditorBackend *  (required; same one as Source mode)
Item {
    id: root

    property var editorBackend  // EditorBackend *

    LiveListModelBinding {
        id: binding
        editorBackend: root.editorBackend
    }

    LiveContextMenuHandler {
        id: ctxMenu
        selectionModel: binding.selectionModel
        blockTexts: {
            // Build a fresh array of block texts whenever the model count changes.
            const out = []
            const count = binding.model ? binding.model.rowCount() : 0
            for (let i = 0; i < count; ++i) {
                const idx = binding.model.index(i, 0)
                out.push(binding.model.data(idx, binding.model.roleForName("text")))
            }
            return out
        }
        blockCount: binding.model ? binding.model.rowCount() : 0
    }

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        spacing: 12

        model: binding.model

        delegate: DelegateChooser {
            role: "kind"

            DelegateChoice {
                roleValue: "paragraph"
                ParagraphDelegate {
                    blockIndex: model.index
                    blockText: model.text
                    selectionModel: binding.selectionModel
                }
            }
            DelegateChoice {
                roleValue: "heading"
                HeadingDelegate {
                    blockIndex: model.index
                    blockText: model.text
                    headingLevel: model.headingLevel
                    selectionModel: binding.selectionModel
                }
            }
            DelegateChoice {
                roleValue: "hr"
                HorizontalRuleDelegate {}
            }
            DelegateChoice {
                roleValue: "image"
                ImageDelegate {
                    imageSrc: model.imageSrc
                    imageAlt: model.imageAlt
                    imageTitle: model.imageTitle
                }
            }
            DelegateChoice {
                roleValue: "code_block"
                CodeBlockDelegate {
                    blockIndex: model.index
                    codeLanguage: model.codeLanguage
                    codeText: model.codeText
                    selectionModel: binding.selectionModel
                }
            }
        }

        // ---- selection input layer ----
        // The full hit() pipeline from the cross-block-selection spike.
        // Documentation: docs/specs/2026-04-29-cross-block-selection-spike-findings.md §3.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.IBeamCursor
            hoverEnabled: false
            preventStealing: true   // stop ListView's flickable from stealing drags

            function clampedLocalX(item, contentX) {
                return Math.max(0, Math.min(contentX - item.x, item.width - 1))
            }

            function blockTextOf(idx) {
                if (!binding.model) return ""
                if (idx < 0 || idx >= binding.model.rowCount()) return ""
                return binding.model.data(binding.model.index(idx, 0),
                                          binding.model.roleForName("text"))
            }

            function hit(mouseX, mouseY) {
                const lastIdx = listView.count - 1

                const clampedX = Math.max(0, Math.min(mouseX, width - 1))
                const clampedY = Math.max(0, Math.min(mouseY, height - 1))
                const cx = clampedX + listView.contentX
                const cy = clampedY + listView.contentY
                const probeX = listView.width / 2

                if (cy >= listView.contentHeight) {
                    const probe = listView.itemAt(probeX, listView.contentHeight - 1)
                    if (probe) {
                        const localY = Math.max(0, probe.height - 1)
                        return { block: probe.index ?? lastIdx,
                                 offset: probe.positionAt
                                     ? probe.positionAt(clampedLocalX(probe, cx), localY)
                                     : blockTextOf(lastIdx).length }
                    }
                    return { block: lastIdx, offset: blockTextOf(lastIdx).length }
                }
                if (cy < 0) {
                    return { block: 0, offset: 0 }
                }

                const item = listView.itemAt(probeX, cy)
                if (item) {
                    const localY = cy - item.y
                    const offset = item.positionAt
                        ? item.positionAt(clampedLocalX(item, cx), localY)
                        : 0
                    return { block: item.index ?? 0, offset: offset }
                }

                let aboveItem = null, aboveDy = 0
                let belowItem = null, belowDy = 0
                for (let dy = 4; dy < 64; dy += 4) {
                    if (!aboveItem) {
                        const a = listView.itemAt(probeX, Math.max(0, cy - dy))
                        if (a) { aboveItem = a; aboveDy = dy }
                    }
                    if (!belowItem) {
                        const b = listView.itemAt(probeX, Math.min(listView.contentHeight - 1, cy + dy))
                        if (b) { belowItem = b; belowDy = dy }
                    }
                    if (aboveItem && belowItem) break
                }
                if (aboveItem && (!belowItem || aboveDy <= belowDy)) {
                    const localY = Math.max(0, aboveItem.height - 1)
                    const offset = aboveItem.positionAt
                        ? aboveItem.positionAt(clampedLocalX(aboveItem, cx), localY)
                        : blockTextOf(aboveItem.index).length
                    return { block: aboveItem.index ?? 0, offset: offset }
                }
                if (belowItem) {
                    const offset = belowItem.positionAt
                        ? belowItem.positionAt(clampedLocalX(belowItem, cx), 0)
                        : 0
                    return { block: belowItem.index ?? 0, offset: offset }
                }
                return null
            }

            onPressed: (m) => {
                if (m.button === Qt.RightButton) {
                    const globalPos = mapToGlobal(m.x, m.y)
                    ctxMenu.popup(Qt.point(globalPos.x, globalPos.y))
                    return
                }
                const h = hit(m.x, m.y)
                if (h) {
                    binding.selectionModel.begin(h.block, h.offset)
                } else {
                    binding.selectionModel.clear()
                }
            }
            onPositionChanged: (m) => {
                if (!pressed || (m.buttons & Qt.LeftButton) === 0) return
                const h = hit(m.x, m.y)
                if (h) binding.selectionModel.extend(h.block, h.offset)
            }
        }
    }

    // Ctrl+C copies the current selection.
    Shortcut {
        sequence: StandardKey.Copy
        onActivated: {
            if (!binding.selectionModel.hasSelection) return
            const out = []
            const count = binding.model.rowCount()
            for (let i = 0; i < count; ++i) {
                const idx = binding.model.index(i, 0)
                out.push(binding.model.data(idx, binding.model.roleForName("text")))
            }
            binding.selectionModel.copySelectionToClipboard(out)
        }
    }
}
```

- [ ] **Step 10.2: Add to library QML module**

In `libs/markoff-view-qml/CMakeLists.txt`'s `qt_add_qml_module(... QML_FILES ...)` call, add `qml/LiveView.qml`.

- [ ] **Step 10.3: Build**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```
Expected: clean build.

- [ ] **Step 10.4: Commit**

```bash
git add libs/markoff-view-qml/qml/LiveView.qml libs/markoff-view-qml/CMakeLists.txt
git commit -m "feat(view-qml): LiveView.qml — ListView + DelegateChooser + hit() drag layer"
```

---

## Task 11: Update `MarkoffEditor.qml` — add `mode` property + Source/Live swap

**Files:**
- Modify: `libs/markoff-view-qml/qml/MarkoffEditor.qml`

- [ ] **Step 11.1: Read existing `MarkoffEditor.qml`** to understand its current structure.

- [ ] **Step 11.2: Add a `mode` property and replace the PHASE-2 SEAM with conditional children**

Insert near the top of the existing root Item:

```qml
/// Either "source" or "live". Default: "source" (unchanged from Phase 1).
property string mode: "source"
```

Find the `// PHASE-2 SEAM` comment. Replace it with:

```qml
SourceEditor {
    visible: root.mode === "source"
    enabled: root.mode === "source"
    document: root.document
    theme: root.theme
}
LiveView {
    visible: root.mode === "live"
    enabled: root.mode === "live"
    editorBackend: backend  // existing EditorBackend id
}
```

(Note: the exact id of the existing `EditorBackend` may be different — read the existing file. Wire `LiveView.editorBackend` to whatever `EditorBackend` instance the existing `SourceEditor` consumes.)

- [ ] **Step 11.3: Build**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```
Expected: clean build.

- [ ] **Step 11.4: Commit**

```bash
git add libs/markoff-view-qml/qml/MarkoffEditor.qml
git commit -m "feat(view-qml): MarkoffEditor.qml — mode property + Source/Live swap at PHASE-2 seam"
```

---

## Task 12: Test app `--live` flag + smoke test

**Files:**
- Modify: `libs/markoff-view-qml/app/main.cpp`
- Create: `libs/markoff-view-qml/tests/tst_view_qml_live_view_smoke.cpp`

- [ ] **Step 12.1: Add `--live` CLI handling in `main.cpp`**

In `libs/markoff-view-qml/app/main.cpp`, after `QApplication app(argc, argv);`, parse arguments:

```cpp
const QStringList args = QCoreApplication::arguments();
bool startInLiveMode = args.contains(QStringLiteral("--live"));
```

Then expose this to QML via the engine's root context:

```cpp
engine.rootContext()->setContextProperty(QStringLiteral("startInLiveMode"),
                                         QVariant(startInLiveMode));
```

In the existing root QML object that loads `MarkoffEditor`, set its `mode` property:

```qml
MarkoffEditor {
    // ... existing bindings ...
    mode: typeof startInLiveMode !== "undefined" && startInLiveMode ? "live" : "source"
}
```

(The exact location depends on the existing app QML file — read it before editing.)

- [ ] **Step 12.2: Write the smoke test**

Create `libs/markoff-view-qml/tests/tst_view_qml_live_view_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QSignalSpy>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveListModelBinding.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff::View::Qml;

class TstLiveViewSmoke : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void live_view_renders_five_block_kinds() {
        Markoff::MarkoffDocument doc(1);
        EditorBackend be;
        be.setDocument(&doc);
        LiveListModelBinding binding;
        binding.setEditorBackend(&be);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0;
        ed.newText = QByteArrayLiteral(
            "# Heading\n\n"
            "Para text.\n\n"
            "---\n\n"
            "![alt](http://example.com/img.png)\n\n"
            "```python\nx = 1\n```\n");
        doc.applyLocalEdit({ ed });

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        LiveBlockModel *model = binding.model();
        QCOMPARE(model->rowCount(), 5);

        QStringList kinds;
        for (int i = 0; i < model->rowCount(); ++i) {
            kinds << model->data(model->index(i, 0),
                                 model->roleForName("kind")).toString();
        }
        QCOMPARE(kinds, (QStringList{
            QStringLiteral("heading"),
            QStringLiteral("paragraph"),
            QStringLiteral("hr"),
            QStringLiteral("image"),
            QStringLiteral("code_block")
        }));
    }
};

QTEST_MAIN(TstLiveViewSmoke)
#include "tst_view_qml_live_view_smoke.moc"
```

- [ ] **Step 12.3: Add test target**

In `libs/markoff-view-qml/tests/CMakeLists.txt`:

```cmake
qt_add_executable(tst_view_qml_live_view_smoke tst_view_qml_live_view_smoke.cpp)
target_link_libraries(tst_view_qml_live_view_smoke PRIVATE markoff_view_qml markoff_foundation Qt6::Test)
add_test(NAME tst_view_qml_live_view_smoke COMMAND tst_view_qml_live_view_smoke)
```

- [ ] **Step 12.4: Build app + run smoke test**

```bash
cmake --build build-dev --target markoff-view-qml-app tst_view_qml_live_view_smoke -j 8
./build-dev/bin/tst_view_qml_live_view_smoke -platform offscreen
```
Expected: smoke test PASS. Test app builds clean.

- [ ] **Step 12.5: Run full view-qml + foundation test suite**

```bash
ctest --test-dir build-dev -R '^tst_(view_qml_|foundation_|markoff_|anchor_json|selection|fold_ref)' --output-on-failure -j 8
```
Expected: all originally-passing tests still pass + 6 new tests (block_walker, ast_block_diff, live_selection_model, live_block_model, live_list_model_binding, live_view_smoke) all pass.

- [ ] **Step 12.6: Commit**

```bash
git add libs/markoff-view-qml/app/main.cpp libs/markoff-view-qml/tests/tst_view_qml_live_view_smoke.cpp libs/markoff-view-qml/tests/CMakeLists.txt
# also any modified app QML files
git commit -m "feat(view-qml): test app --live flag + smoke test for LiveView walking skeleton"
```

---

## Task 13: Update `libs/markoff-view-qml/CLAUDE.md`

Document the new Phase-2 surface, the v0 invariants, and the spike-findings reference.

**Files:**
- Modify: `libs/markoff-view-qml/CLAUDE.md`

- [ ] **Step 13.1: Update CLAUDE.md**

Replace the existing "## Phase-2 plan (not in this code — architecture is positioned for it)" section with a "## Phase-2 v0 walking skeleton (in code)" section listing:

- The five v0 block kinds
- LiveView / LiveListModelBinding / LiveBlockModel / LiveSelectionModel / LiveContextMenuHandler / BlockWalker / AstBlockDiff exports
- The cross-block-selection invariants (selectByMouse off, top-level MouseArea owns input, INT32_MAX clamping required by consumers)
- The Widget-bridge invariant (KDAB pattern; QApplication required)
- Reference to `docs/specs/2026-04-29-live-render-design.md` and `docs/specs/2026-04-29-cross-block-selection-spike-findings.md`

Add a new "## Architectural invariants (v0 Phase-2)" subsection mirroring spec §4 invariants 1-8.

- [ ] **Step 13.2: Commit**

```bash
git add libs/markoff-view-qml/CLAUDE.md
git commit -m "docs(view-qml): update CLAUDE.md for Phase-2 v0 walking skeleton"
```

---

## Self-review

**Spec coverage check (against `docs/specs/2026-04-29-live-render-design.md`):**
- §1 Architecture — covered by Tasks 1, 7, 10, 11 ✓
- §2 File structure — Tasks 2-12 ✓
- §3 Data flow — exercised by Task 7's integration tests + Task 12 smoke ✓
- §4 Invariants — Task 13 documents them; Tasks 5/7 enforce them in code ✓
- §5 Testing — Tasks 3, 4, 5, 6, 7, 12 cover all specified test targets ✓
- §6 Plugin extension points — `kind` is string-keyed (Task 2 ✓); `Theme` routing in delegates is partial in v0 (deferred); `CodeBlockProcessorRegistry` consultation is deferred until the foundation registry is exposed via EditorBackend (a follow-up)
- §7 Approach 3 — explicitly out of scope ✓
- §8 Out of scope — confirmed; nothing in this plan crosses these lines ✓

**Placeholder scan:**
- All steps have explicit code or commands. No "TBD" or "implement later."
- "Add appropriate error handling" — none in this plan; error handling is per spec §4 table.

**Type consistency:**
- `BlockKind::*` constants used uniformly (Tasks 2, 3, 6, 7).
- `BlockKey` / `BlockRecord` field names match across tasks.
- `LiveSelectionModel` API method names match between Task 5 and call sites in Tasks 7, 8, 10.
- `LiveBlockModel::roleForName` introduced in Task 6 and used in Tasks 7, 10, 12.
- `LiveListModelBinding` method signatures (`editorBackend`, `model`, `selectionModel`) consistent across Task 7's tests and Task 10's QML usage.

**One minor follow-up captured but not blocking:** Section 6 of the spec mentions `CodeBlockProcessorRegistry` consultation in `CodeBlockDelegate`. The foundation has the registry, but it's not yet exposed through `EditorBackend`. v0 ships with the registry consultation deferred — `CodeBlockDelegate` falls back to the `KSyntaxHighlighting` `definition` mapping directly. A follow-up task can wire registry consultation when the foundation surfaces it.
