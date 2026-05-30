# `Markoff::Styled::DocumentRenderer` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a headless, read-only `Markoff::Styled::DocumentRenderer` that formats a `MarkoffDocument` into a caller-owned `QTextDocument` (T1) and measures/paints it without a widget (T2).

**Architecture:** Extract the format-application logic from `StyleApplier` into an internal `FormatPass` (single source of truth). `StyleApplier` (stateful, widget-aware, model-mutating) and the new public `DocumentRenderer` (stateless, read-only) both call it. The model-mutating `Cmd::changeKind` decision becomes a value returned by `FormatPass` that only `StyleApplier` acts on.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets), CMake, QtTest. Builds in `build-dev`, tests via `scripts/run-tests.sh` (offscreen).

**Spec:** [`docs/specs/2026-05-29-markoff-styled-document-renderer-design.md`](../specs/2026-05-29-markoff-styled-document-renderer-design.md)

---

## File Structure

| File | Responsibility |
|------|----------------|
| `libs/markoff-styled/src/FormatPass.h` (create) | Internal API: `Options`/`Result`/`KindSuggestion`/`BlockHashMap` + `apply()`. |
| `libs/markoff-styled/src/FormatPass.cpp` (create) | The block walk + per-kind helpers, moved verbatim from `StyleApplier.cpp`. Pure w.r.t. the model. |
| `libs/markoff-styled/src/StyleApplier.cpp` (modify) | `applyFormats()` becomes a thin wrapper over `FormatPass::apply`. |
| `libs/markoff-styled/include/markoff/styled/DocumentRenderer.h` (create) | Public, exported, read-only renderer. |
| `libs/markoff-styled/src/DocumentRenderer.cpp` (create) | T1 `renderInto` + T2 `idealHeight`/`paint`. |
| `libs/markoff-styled/CMakeLists.txt` (modify) | Add the two new `.cpp`/`.h` to the library target. |
| `libs/markoff-styled/tests/tst_styled_document_renderer.cpp` (create) | Falsifiable tests for T1+T2. |
| `libs/markoff-styled/tests/CMakeLists.txt` (modify) | Register the new test binary. |
| `libs/markoff-styled/CLAUDE.md` (modify) | Document the new public surface + internal `FormatPass`. |
| `docs/queue.md` (modify) | Back-reference + any Discipline Log entry. |

---

## Phase 0 — Pre-flight baseline

### Task 0: Confirm branch + capture green baseline

**Files:** none.

- [ ] **Step 1: Confirm branch**

Run: `git branch --show-current`
Expected: `feature/styled-document-renderer`

- [ ] **Step 2: Configure + build**

Run: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build-dev -j 8`
Expected: builds with no errors.

- [ ] **Step 3: Capture styled baseline**

Run: `scripts/run-tests.sh -R styled`
Expected: record the pass/fail counts (per CLAUDE.md, `tst_styled_block_formats` has 2 known pre-existing failures: `heading_levels_descend_in_size`, `horizontal_rule_uses_monospace`). This is the regression baseline for Phase 1 — Phase 1 must not change it.

---

## Phase 1 — Extract `FormatPass` (behavior-preserving)

### Task 1: Create `FormatPass.h`

**Files:**
- Create: `libs/markoff-styled/src/FormatPass.h`

- [ ] **Step 1: Write the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <vector>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>

class QTextDocument;

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Styled::FormatPass {

/// Per-call configuration.
struct Options {
    qreal                 fontScale = 1.0;
    const Markoff::Theme *theme     = nullptr;
    bool                  inferKind = false;  ///< false ⇒ no kind suggestions
};

/// A "this block's prefix disagrees with its stored kind" suggestion. The
/// caller (StyleApplier) decides whether to act (issue Cmd::changeKind);
/// FormatPass never mutates the model.
struct KindSuggestion {
    Markoff::BlockId   id;
    Markoff::BlockKind newKind;
};

using BlockHashMap = QHash<Markoff::BlockId, quint64>;

struct Result {
    quint64                     hashSkips  = 0;
    bool                        structural = false;  ///< block set changed vs gate
    std::vector<KindSuggestion> kindSuggestions;     ///< empty unless inferKind
};

/// Apply block + inline formats to `target` (which must already hold
/// `source->widgetFlatView()` text). PURE w.r.t. the model: issues no Cmd::*,
/// never mutates `source`. When `gate` is non-null, per-block hash gating is
/// applied and the map is updated + pruned; when null, every block is
/// formatted and no gate state is touched. Coalesces edits via
/// begin/endEditBlock and blocks `target`'s signals for the duration.
Result apply(QTextDocument *target,
             const Markoff::MarkoffDocument *source,
             const Options &opts,
             BlockHashMap *gate);

}  // namespace Markoff::Styled::FormatPass
```

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-styled/src/FormatPass.h
git commit -m "feat(styled): FormatPass internal header (DocumentRenderer prep)"
```

### Task 2: Create `FormatPass.cpp` by moving the walk out of `StyleApplier.cpp`

**Files:**
- Create: `libs/markoff-styled/src/FormatPass.cpp`
- Modify: `libs/markoff-styled/src/StyleApplier.cpp` (remove the moved code in Task 3)

- [ ] **Step 1: Create `FormatPass.cpp` shell + move the anonymous-namespace helpers**

Move these — **verbatim, bodies unchanged** — from the anonymous namespace of `StyleApplier.cpp` into an anonymous namespace in `FormatPass.cpp`: `kBaseBodyPt`, `emPt`, `paragraphMarginPt`, `listItemMarginPt`, `codeBlockMarginPt`, `blockquoteMarginPt`, `headingTopMarginPt`, `headingBotMarginPt`, `hruleMarginPt`, `docIndentWidthPx`, `baseBlockFormat`, `applyBlockCharFormat`, `applyHeading`, `applyParagraph`, `applyCodeBlock`, `applyBlockquote`, `applyListItem`, `applyHorizontalRule`, `charFormatForSpan`, `computeBlockHash`, `inferKindFromPrefix`, `ListStackEntry`, `manageListMembership`.

File preamble (the includes those helpers + the walk need):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FormatPass.h"

#include <cstring>

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextListFormat>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/parser/SourceSpan.h>

namespace {
// ... all the moved helpers (verbatim) ...
}  // namespace
```

- [ ] **Step 2: Write `FormatPass::apply` from the old walk body**

Adapt the body of the old `StyleApplier::applyFormats` (the part inside the `QSignalBlocker`/`beginEditBlock`..`endEditBlock` plus the structural-change detection) into this function. The only changes vs. the original walk: read `fontScale`/`theme` from `opts`; gate via `gate` (nullable); push kind suggestions to `out.kindSuggestions` (only when `opts.inferKind`) instead of mutating; return `hashSkips`/`structural`.

```cpp
namespace Markoff::Styled::FormatPass {

Result apply(QTextDocument *target,
             const Markoff::MarkoffDocument *source,
             const Options &opts,
             BlockHashMap *gate) {
    Result out;
    if (!target || !source) return out;

    const qreal fontScale = opts.fontScale;

    // Previous block IDs (for structural-change detection) — only meaningful
    // when a gate is supplied.
    QHash<Markoff::BlockId, char> previousBlockIds;
    if (gate) {
        for (auto it = gate->constBegin(); it != gate->constEnd(); ++it)
            previousBlockIds.insert(it.key(), 0);
    }

    QHash<Markoff::BlockId, char> currentIds;
    {
        QSignalBlocker block(target);
        QTextCursor cursor(target);
        cursor.beginEditBlock();

        target->setIndentWidth(docIndentWidthPx(fontScale));

        const QByteArray flatBytes = source->widgetFlatView();
        const std::vector<Markoff::BlockId> blocks = source->iterateBlocks();
        static constexpr int kSepLen = 1;  // single "\n"
        currentIds.reserve(static_cast<qsizetype>(blocks.size()));

        std::vector<ListStackEntry> listStack;

        quint32 bytePos = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const Markoff::BlockId id = blocks[i];
            currentIds.insert(id, 0);

            const QByteArray text = source->blockText(id);
            const quint32 blockStart = bytePos;
            const quint32 blockEnd   = bytePos + static_cast<quint32>(text.size());

            const Markoff::BlockKind kind = source->blockKind(id);
            const QList<Markoff::SourceSpan> spans = source->inlineSpansFor(id);
            const auto attrs = source->blockAttrs(id);
            const quint64 h = computeBlockHash(kind, text, spans, attrs, fontScale);

            const int startQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockStart);

            const bool hashSkipped = gate && (gate->value(id, 0) == h);
            if (hashSkipped) {
                ++out.hashSkips;
            } else {
                if (gate) (*gate)[id] = h;

                if (opts.inferKind) {
                    const Markoff::BlockKind inferred = inferKindFromPrefix(text, kind);
                    if (inferred != kind)
                        out.kindSuggestions.push_back({id, inferred});
                }

                const int endQt = Markoff::SourceTextDocumentBinding
                    ::byteOffsetToQtPos(flatBytes, blockEnd);

                cursor.setPosition(startQt);
                QTextBlock qblk = cursor.block();
                while (qblk.isValid() && qblk.position() <= endQt) {
                    QTextCursor blkCursor(qblk);
                    if (kind == Markoff::BlockKind::Heading) {
                        int level = 0;
                        while (level < text.size() && text[level] == '#') ++level;
                        level = qBound(1, level, 6);
                        applyHeading(blkCursor, level, fontScale);
                    } else if (kind == Markoff::BlockKind::Paragraph) {
                        applyParagraph(blkCursor, fontScale);
                    } else if (kind == Markoff::BlockKind::CodeBlock) {
                        applyCodeBlock(blkCursor, fontScale);
                    } else if (kind == Markoff::BlockKind::BlockQuote) {
                        int depth = 1;
                        if (auto it = attrs.find(Markoff::AttrNames::BlockQuoteDepth);
                            it != attrs.end() && std::holds_alternative<int>(*it))
                            depth = qMax(1, std::get<int>(*it));
                        applyBlockquote(blkCursor, depth, fontScale);
                    } else if (kind == Markoff::BlockKind::ListItem) {
                        int depth = 0;
                        if (auto it = attrs.find(Markoff::AttrNames::IndentLevel);
                            it != attrs.end() && std::holds_alternative<int>(*it))
                            depth = std::get<int>(*it);
                        QString markerStyle;
                        if (auto it = attrs.find(Markoff::AttrNames::MarkerStyle);
                            it != attrs.end() && std::holds_alternative<QString>(*it))
                            markerStyle = std::get<QString>(*it);
                        bool checked = false;
                        if (auto it = attrs.find(Markoff::AttrNames::Checked);
                            it != attrs.end() && std::holds_alternative<bool>(*it))
                            checked = std::get<bool>(*it);
                        applyListItem(blkCursor, depth, markerStyle, checked, fontScale);
                    } else if (kind == Markoff::BlockKind::HorizontalRule) {
                        applyHorizontalRule(blkCursor, fontScale);
                    } else {
                        applyParagraph(blkCursor, fontScale);
                    }

                    if (kind != Markoff::BlockKind::BlockQuote) {
                        int overlayDepth = 0;
                        if (auto it = attrs.find(Markoff::AttrNames::BlockQuoteDepth);
                            it != attrs.end() && std::holds_alternative<int>(*it))
                            overlayDepth = std::get<int>(*it);
                        if (overlayDepth > 0) {
                            QTextBlockFormat bf = blkCursor.blockFormat();
                            bf.setLeftMargin(bf.leftMargin()
                                             + emPt(fontScale) * overlayDepth);
                            blkCursor.setBlockFormat(bf);
                        }
                    }
                    qblk = qblk.next();
                }

                const int docLen = target->characterCount() - 1;
                for (const Markoff::SourceSpan &span : spans) {
                    if (span.charLength <= 0) continue;
                    const int spanStart = startQt + span.charOffset;
                    const int spanEnd   = startQt + span.charOffset + span.charLength;
                    if (spanStart >= docLen) continue;
                    QTextCursor c(target);
                    c.setPosition(spanStart);
                    c.setPosition(qMin(spanEnd, docLen), QTextCursor::KeepAnchor);
                    c.mergeCharFormat(charFormatForSpan(span, fontScale));
                }
            }  // end !hashSkipped

            const QTextBlock listBlk = target->findBlock(startQt);
            manageListMembership(listBlk, kind, attrs, listStack);

            bytePos = blockEnd;
            if (i + 1 < blocks.size()) bytePos += kSepLen;
        }

        if (gate) {
            for (auto it = gate->begin(); it != gate->end(); ) {
                if (!currentIds.contains(it.key())) it = gate->erase(it);
                else ++it;
            }
        }

        cursor.endEditBlock();
    }

    out.structural = (previousBlockIds.size() != currentIds.size());
    if (!out.structural && !previousBlockIds.isEmpty()) {
        for (auto it = currentIds.constBegin(); it != currentIds.constEnd(); ++it) {
            if (!previousBlockIds.contains(it.key())) { out.structural = true; break; }
        }
    }
    return out;
}

}  // namespace Markoff::Styled::FormatPass
```

- [ ] **Step 3: Do not build yet** — `StyleApplier.cpp` still defines the duplicated helpers; Task 3 removes them. Proceed.

### Task 3: Refactor `StyleApplier::applyFormats` onto `FormatPass`

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`

- [ ] **Step 1: Remove the moved helpers from `StyleApplier.cpp`**

Delete the entire anonymous-namespace block (the helpers listed in Task 2, Step 1) from `StyleApplier.cpp`. Add `#include "FormatPass.h"` to its includes. Leave the remaining includes (unused ones are harmless; trim after the build if desired).

- [ ] **Step 2: Replace the body of `StyleApplier::applyFormats`**

Replace the whole function with:

```cpp
void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;

    const int savedScroll = (m_pendingScrollCapture >= 0)
        ? m_pendingScrollCapture
        : ((m_textEdit && m_textEdit->verticalScrollBar())
           ? m_textEdit->verticalScrollBar()->value() : -1);
    m_pendingScrollCapture = -1;

    const bool hadPrevBlocks = !m_blockHashes.isEmpty();

    FormatPass::Options opts;
    opts.fontScale = m_fontScale;
    opts.theme     = m_theme;
    opts.inferKind = true;
    const FormatPass::Result r =
        FormatPass::apply(m_textDocument, m_markoffDocument, opts, &m_blockHashes);

    m_hashSkipsLastPass = r.hashSkips;
    for (const auto &sug : r.kindSuggestions)
        m_pendingKindChanges.push_back({sug.id, sug.newKind});

    if (!r.structural && hadPrevBlocks && savedScroll >= 0 && m_textEdit) {
        QPointer<QTextEdit> editPtr = m_textEdit;
        QTimer::singleShot(0, this, [editPtr, savedScroll]() {
            if (editPtr && editPtr->verticalScrollBar())
                editPtr->verticalScrollBar()->setValue(savedScroll);
        });
    }

    ++m_restyleCount;
    m_applyingFormats = false;

    if (!m_pendingKindChanges.empty())
        QTimer::singleShot(0, this, &StyleApplier::applyPendingKindChanges);
}
```

Leave `applyPendingKindChanges`, `rerender`, the setters, and all members unchanged. `PendingKindChange` stays declared in `StyleApplier.h`.

### Task 4: Wire `FormatPass` into CMake, build, regression-check

**Files:**
- Modify: `libs/markoff-styled/CMakeLists.txt`

- [ ] **Step 1: Add sources**

In the `add_library(markoff_styled STATIC ...)` list, after the `StyleApplier.cpp` line add:

```cmake
    src/FormatPass.h
    src/FormatPass.cpp
```

- [ ] **Step 2: Build (const-ness gate)**

Run: `cmake --build build-dev -j 8`
Expected: builds clean. If it fails because a `MarkoffDocument` accessor used by `FormatPass::apply` (`widgetFlatView`/`iterateBlocks`/`blockText`/`blockKind`/`inlineSpansFor`/`blockAttrs`) is **not** `const`, fix it at the core layer (make the offending cache member `mutable` so the accessor can be `const`) — do NOT drop `const` from `FormatPass::apply`'s `source` parameter. Rebuild.

- [ ] **Step 3: Regression run**

Run: `scripts/run-tests.sh -R styled`
Expected: identical pass/fail set to the Task 0 baseline (same 2 known pre-existing failures, nothing new). If anything else regresses, the move was not behavior-preserving — diff against the original walk and fix before continuing.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-styled/src/FormatPass.cpp libs/markoff-styled/src/StyleApplier.cpp libs/markoff-styled/CMakeLists.txt
git commit -m "refactor(styled): extract FormatPass; StyleApplier wraps it (no behavior change)"
```

---

## Phase 2 — `DocumentRenderer` T1 (TDD)

### Task 5: Failing tests for T1

**Files:**
- Create: `libs/markoff-styled/tests/tst_styled_document_renderer.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test file (T1 cases)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/DocumentRenderer.h>
#include <markoff/styled/Editor.h>

namespace {
QTextBlock blockN(const QTextDocument *doc, int n) {
    return doc->findBlockByNumber(n);
}
}  // namespace

class TstStyledDocumentRenderer : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void heading_is_bold_and_nonempty() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# Title\n\nbody"));
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, &doc);
        QVERIFY(target.characterCount() > 1);
        const QTextCharFormat cf = blockN(&target, 0).charFormat();
        QVERIFY(cf.fontPointSize() > 0);
        QCOMPARE(cf.fontWeight(), int(QFont::Bold));
    }

    void code_block_is_monospace_with_background() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode line\n```"));
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, &doc);
        const QTextBlock content = blockN(&target, 1);
        const QTextCharFormat cf = content.charFormat();
        QVERIFY(cf.fontFixedPitch() || !cf.fontFamilies().toStringList().isEmpty());
        QVERIFY(content.blockFormat().background().style() != Qt::NoBrush);
    }

    void blockquote_and_list_have_left_margin() {
        Markoff::MarkoffDocument q(1);
        q.loadFromMarkdown(QByteArrayLiteral("> quoted text"));
        QTextDocument tq;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&tq, &q);
        QVERIFY(blockN(&tq, 0).blockFormat().leftMargin() > 0);

        Markoff::MarkoffDocument l(1);
        l.loadFromMarkdown(QByteArrayLiteral("- first item"));
        QTextDocument tl;
        r.renderInto(&tl, &l);
        QVERIFY(blockN(&tl, 0).blockFormat().leftMargin() > 0);
    }

    void inline_bold_and_italic_applied() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("normal **bold** _italic_ text"));
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, &doc);
        bool sawBold = false, sawItalic = false;
        const QTextBlock blk = blockN(&target, 0);
        for (QTextBlock::iterator it = blk.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (f.fontWeight() == QFont::Bold) sawBold = true;
            if (f.fontItalic()) sawItalic = true;
        }
        QVERIFY(sawBold);
        QVERIFY(sawItalic);
    }

    void bytes_overload_renders_kinds() {
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target, QByteArrayLiteral("# H\n\nbody\n\n- a\n- b"));
        QVERIFY(target.characterCount() > 1);
        QCOMPARE(blockN(&target, 0).charFormat().fontWeight(), int(QFont::Bold));
    }

    void unsupported_block_degrades_to_text() {  // acceptance #3
        QTextDocument target;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&target,
                     QByteArrayLiteral("| a | b |\n| - | - |\n| 1 | 2 |"));
        QVERIFY(target.characterCount() > 1);  // non-empty, no crash
    }

    void matches_widget_path() {  // acceptance #1 (consistency)
        const QByteArray md = QByteArrayLiteral(
            "# H1\n\n## H2\n\npara **b**\n\n> quote\n\n- item\n\n```\ncode\n```");
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument wdoc(1);
        wdoc.loadFromMarkdown(md);
        auto *s = wdoc.createSession();
        e.setSession(s);
        e.setDocument(&wdoc);
        const QTextDocument *wq = e.textEdit()->document();

        Markoff::MarkoffDocument hdoc(1);
        hdoc.loadFromMarkdown(md);
        QTextDocument hq;
        Markoff::Styled::DocumentRenderer r;
        r.renderInto(&hq, &hdoc);

        QCOMPARE(hq.blockCount(), wq->blockCount());
        for (int i = 0; i < wq->blockCount(); ++i) {
            const QTextBlock wb = wq->findBlockByNumber(i);
            const QTextBlock hb = hq.findBlockByNumber(i);
            QCOMPARE(hb.charFormat().fontPointSize(), wb.charFormat().fontPointSize());
            QCOMPARE(hb.charFormat().fontWeight(),    wb.charFormat().fontWeight());
            QCOMPARE(hb.blockFormat().leftMargin(),   wb.blockFormat().leftMargin());
        }
    }
};

QTEST_MAIN(TstStyledDocumentRenderer)
#include "tst_styled_document_renderer.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:

```cmake
add_executable(tst_styled_document_renderer tst_styled_document_renderer.cpp)
add_test(NAME tst_styled_document_renderer COMMAND tst_styled_document_renderer)
target_link_libraries(tst_styled_document_renderer
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_document_renderer
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Add a temporary stub so it compiles + fails**

Create `libs/markoff-styled/include/markoff/styled/DocumentRenderer.h` and `src/DocumentRenderer.cpp` as stubs (empty method bodies / `return 0;`), and add them to `libs/markoff-styled/CMakeLists.txt` (alongside FormatPass). Use the real header from Task 6 Step 1, but stub the `.cpp` bodies to no-ops.

Run: `cmake --build build-dev -j 8 && scripts/run-tests.sh --bin tst_styled_document_renderer`
Expected: compiles; tests FAIL (e.g. `heading_is_bold_and_nonempty` fails — `characterCount()` is 1 / not bold). This proves the suite is falsifiable.

### Task 6: Implement `DocumentRenderer` T1

**Files:**
- Create/overwrite: `libs/markoff-styled/include/markoff/styled/DocumentRenderer.h`
- Create/overwrite: `libs/markoff-styled/src/DocumentRenderer.cpp`

- [ ] **Step 1: Header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QRectF>

#include <markoff/styled/MarkoffStyledExport.h>

class QPainter;
class QTextDocument;

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Styled {

/// Headless, read-only Markoff renderer. Formats a MarkoffDocument into a
/// caller-owned QTextDocument (T1) and measures/paints it without a widget
/// (T2). No cursor, no editing, no model mutation. Spec
/// docs/specs/2026-05-29-markoff-styled-document-renderer-design.md.
class MARKOFF_STYLED_EXPORT DocumentRenderer {
public:
    DocumentRenderer();
    ~DocumentRenderer();

    void setTheme(const Markoff::Theme *theme);  ///< non-owning, may be null
    void setFontScale(qreal s);

    // T1 — populate a caller-owned document.
    void renderInto(QTextDocument *target,
                    const Markoff::MarkoffDocument *source) const;
    void renderInto(QTextDocument *target,
                    const QByteArray &markdownUtf8) const;

    // T2 — convenience one-shots (build a transient doc over T1). For per-frame
    // canvas paint, own a QTextDocument, renderInto it once, and paint/measure
    // it directly instead.
    qreal idealHeight(const Markoff::MarkoffDocument *source, qreal width) const;
    void  paint(QPainter *painter, const QRectF &rect,
                const Markoff::MarkoffDocument *source) const;

private:
    const Markoff::Theme *m_theme     = nullptr;
    qreal                 m_fontScale = 1.0;
};

}  // namespace Markoff::Styled
```

- [ ] **Step 2: Implementation (T1 only for now; T2 added in Phase 3)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/DocumentRenderer.h>

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QPainter>
#include <QString>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

#include "FormatPass.h"

namespace Markoff::Styled {

DocumentRenderer::DocumentRenderer() = default;
DocumentRenderer::~DocumentRenderer() = default;

void DocumentRenderer::setTheme(const Markoff::Theme *theme) { m_theme = theme; }
void DocumentRenderer::setFontScale(qreal s) { m_fontScale = s; }

void DocumentRenderer::renderInto(QTextDocument *target,
                                  const Markoff::MarkoffDocument *source) const {
    if (!target || !source) return;
    target->setDefaultFont(
        m_theme ? m_theme->font(Markoff::Theme::FontRole::Body) : QFont());
    target->setPlainText(QString::fromUtf8(source->widgetFlatView()));
    FormatPass::Options opts;
    opts.fontScale = m_fontScale;
    opts.theme     = m_theme;
    opts.inferKind = false;  // read-only: never suggest/issue Cmd::changeKind
    FormatPass::apply(target, source, opts, /*gate=*/nullptr);
}

void DocumentRenderer::renderInto(QTextDocument *target,
                                  const QByteArray &markdownUtf8) const {
    if (!target) return;
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(markdownUtf8);
    renderInto(target, &doc);
}

qreal DocumentRenderer::idealHeight(const Markoff::MarkoffDocument *,
                                    qreal) const {
    return 0;  // implemented in Phase 3
}

void DocumentRenderer::paint(QPainter *, const QRectF &,
                             const Markoff::MarkoffDocument *) const {
    // implemented in Phase 3
}

}  // namespace Markoff::Styled
```

> If a `Theme::FontRole::Body` enumerator name differs in `markoff-core/Theme.h`, use the actual body-font role accessor; if `Theme` exposes no font accessor yet, replace the `setDefaultFont(...)` line with `target->setDefaultFont(QFont());` (matches the widget path's app-default font — keeps the `matches_widget_path` test valid).

- [ ] **Step 3: Build + run T1 tests**

Run: `cmake --build build-dev -j 8 && scripts/run-tests.sh --bin tst_styled_document_renderer`
Expected: all T1 cases PASS (T2 cases not added yet). If `matches_widget_path` fails on `fontPointSize`, confirm both docs use the same default font (no theme set on either ⇒ both `QFont()`/app-default).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-styled/include/markoff/styled/DocumentRenderer.h \
        libs/markoff-styled/src/DocumentRenderer.cpp \
        libs/markoff-styled/CMakeLists.txt \
        libs/markoff-styled/tests/tst_styled_document_renderer.cpp \
        libs/markoff-styled/tests/CMakeLists.txt
git commit -m "feat(styled): DocumentRenderer T1 renderInto (doc + raw bytes)"
```

---

## Phase 3 — `DocumentRenderer` T2 (TDD)

### Task 7: Failing T2 tests

**Files:**
- Modify: `libs/markoff-styled/tests/tst_styled_document_renderer.cpp`

- [ ] **Step 1: Add T2 cases (before the closing `};`)**

```cpp
    void ideal_height_positive_and_grows_when_narrow() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "This is a fairly long paragraph that should wrap onto several "
            "lines once the available width becomes small enough to force it."));
        Markoff::Styled::DocumentRenderer r;
        const qreal wide   = r.idealHeight(&doc, 600);
        const qreal narrow = r.idealHeight(&doc, 120);
        QVERIFY(wide > 0);
        QVERIFY(narrow >= wide);
    }

    void paint_draws_something() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# Heading\n\nbody text here"));
        Markoff::Styled::DocumentRenderer r;
        const qreal h = r.idealHeight(&doc, 300);
        QVERIFY(h > 0);
        QImage img(300, int(h) + 4, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        r.paint(&p, QRectF(0, 0, 300, h), &doc);
        p.end();
        bool drew = false;
        for (int y = 0; y < img.height() && !drew; ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) != QColor(Qt::white)) { drew = true; break; }
        QVERIFY(drew);
    }
```

- [ ] **Step 2: Build + verify they fail**

Run: `cmake --build build-dev -j 8 && scripts/run-tests.sh --bin tst_styled_document_renderer`
Expected: the two new cases FAIL (`idealHeight` returns 0; `paint` draws nothing).

### Task 8: Implement T2

**Files:**
- Modify: `libs/markoff-styled/src/DocumentRenderer.cpp`

- [ ] **Step 1: Replace the two stub bodies**

```cpp
qreal DocumentRenderer::idealHeight(const Markoff::MarkoffDocument *source,
                                    qreal width) const {
    if (!source) return 0;
    QTextDocument doc;
    renderInto(&doc, source);
    doc.setTextWidth(width);
    return doc.size().height();
}

void DocumentRenderer::paint(QPainter *painter, const QRectF &rect,
                             const Markoff::MarkoffDocument *source) const {
    if (!painter || !source) return;
    QTextDocument doc;
    renderInto(&doc, source);
    doc.setTextWidth(rect.width());
    painter->save();
    painter->translate(rect.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.clip = QRectF(0, 0, rect.width(), rect.height());
    doc.documentLayout()->draw(painter, ctx);
    painter->restore();
}
```

- [ ] **Step 2: Build + run**

Run: `cmake --build build-dev -j 8 && scripts/run-tests.sh --bin tst_styled_document_renderer`
Expected: ALL cases PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-styled/src/DocumentRenderer.cpp \
        libs/markoff-styled/tests/tst_styled_document_renderer.cpp
git commit -m "feat(styled): DocumentRenderer T2 idealHeight + paint"
```

---

## Phase 4 — Docs + full-suite gate

### Task 9: Update `libs/markoff-styled/CLAUDE.md`

**Files:**
- Modify: `libs/markoff-styled/CLAUDE.md`

- [ ] **Step 1: Edit the "Public surface" + "Internal" sections**

Under **Public surface**, add a bullet:

```markdown
- `Markoff::Styled::DocumentRenderer` — headless, read-only renderer
  (`include/markoff/styled/DocumentRenderer.h`). `renderInto(QTextDocument*,
  const MarkoffDocument*)` + raw-bytes overload (T1); `idealHeight(width)` +
  `paint(painter, rect)` (T2, convenience one-shots). No widget, no cursor, no
  model mutation. Canvas hot path: own a QTextDocument, `renderInto` once on
  change, paint/measure it directly. Spec
  `docs/specs/2026-05-29-markoff-styled-document-renderer-design.md`.
```

Under **Internal**, add:

```markdown
- `Markoff::Styled::FormatPass` (`src/FormatPass.{h,cpp}`) — single source of
  truth for block + inline formatting. `StyleApplier` and `DocumentRenderer`
  both call `FormatPass::apply`. PURE w.r.t. the model: kind-inference is
  returned as `KindSuggestion`s; `StyleApplier` remains the sole actor that
  issues `Cmd::changeKind` (INVARIANTS §2/§3).
```

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-styled/CLAUDE.md
git commit -m "docs(styled): document DocumentRenderer + FormatPass"
```

### Task 10: Full-suite regression gate + queue back-reference

**Files:**
- Modify: `docs/queue.md`

- [ ] **Step 1: Run the fast inner loop**

Run: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
Expected: no new failures vs the CLAUDE.md baseline (249/254; the 5 documented pre-existing failures unchanged). The new `tst_styled_document_renderer` is green.

- [ ] **Step 2: Add a queue back-reference**

Append a dated note to `docs/queue.md` pointing at the spec + plan and recording the landing (and a Discipline Log line if any smell was noticed during the refactor).

- [ ] **Step 3: Commit**

```bash
git add docs/queue.md
git commit -m "docs(queue): record DocumentRenderer landing"
```

---

## Definition of done
- All acceptance items (spec §7) covered by green tests in `tst_styled_document_renderer`.
- The existing styled suite is non-regressed vs the Task 0 baseline.
- `CLAUDE.md` + `queue.md` updated.
- Re-pin coordination with Corbomite is a follow-up handoff, not part of this branch.

## Out of scope (do not build — spec §5)
Embed/object-replacement hook, math/tables/images/callouts/mermaid coverage,
suspended mode, persistence key, callout frames, `Capabilities::Editable`
revival. Unsupported blocks graceful-degrade to source text.
