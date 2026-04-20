// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic test — dumps internal state at each pipeline stage
// to understand table rendering bugs seen in the showcase.
#include <QObject>
#include <QTest>
#include <QApplication>
#include <QFile>
#include <QTextDocument>
#include <QTextTable>
#include <QTextFrame>
#include <QTextBlock>
#include <QTextCursor>
#include <markoff/Editor.h>
#include "SceneCoordinator.h"
#include "MarkdownTextItem.h"
#include "SelectableItem.h"
#include "TextControl.h"

using namespace Markoff;

class TestTableDiagnostics : public QObject {
    Q_OBJECT

private:
    void dumpDocumentStructure(const QTextDocument *doc, const char *label);
    void dumpItems(SceneCoordinator *coord, const char *label);

private slots:
    void showcaseTableSection();
    void actualShowcaseRemnants();
};

void TestTableDiagnostics::dumpDocumentStructure(const QTextDocument *doc,
                                                   const char *label)
{
    fprintf(stderr, "\n=== DOCUMENT STRUCTURE: %s ===\n", label);
    fprintf(stderr, "  charCount=%d\n", doc->characterCount());

    // Root-level blocks (what doc->begin()/next() sees)
    fprintf(stderr, "  --- Root-level blocks (begin/next iteration) ---\n");
    int blockIdx = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        fprintf(stderr, "  block[%d] pos=%d len=%d visible=%d text=[%s]\n",
                blockIdx, b.position(), b.length(), b.isVisible(),
                b.text().left(60).toUtf8().constData());
        blockIdx++;
    }

    // Child frames
    fprintf(stderr, "  --- Child frames ---\n");
    auto frames = doc->rootFrame()->childFrames();
    fprintf(stderr, "  frameCount=%d\n", (int)frames.size());
    for (auto *frame : frames) {
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            fprintf(stderr, "  TABLE: %dx%d firstPos=%d lastPos=%d\n",
                    table->rows(), table->columns(),
                    table->firstPosition(), table->lastPosition());
            // Dump cell contents
            for (int r = 0; r < table->rows(); ++r) {
                for (int c = 0; c < table->columns(); ++c) {
                    QTextTableCell cell = table->cellAt(r, c);
                    QTextCursor cur = cell.firstCursorPosition();
                    cur.setPosition(cell.lastCursorPosition().position(),
                                    QTextCursor::KeepAnchor);
                    fprintf(stderr, "    cell(%d,%d) pos=%d-%d text=[%s]\n",
                            r, c, cell.firstCursorPosition().position(),
                            cell.lastCursorPosition().position(),
                            cur.selectedText().toUtf8().constData());
                }
            }
        } else {
            fprintf(stderr, "  FRAME: firstPos=%d lastPos=%d (not a table)\n",
                    frame->firstPosition(), frame->lastPosition());
        }
    }

    // Frame iterator (what allMarkdown uses)
    fprintf(stderr, "  --- Frame iterator (root->begin/end) ---\n");
    QTextFrame *root = doc->rootFrame();
    int elemIdx = 0;
    for (auto it = root->begin(); it != root->end(); ++it) {
        if (auto *childFrame = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(childFrame)) {
                fprintf(stderr, "  elem[%d] TABLE %dx%d pos=%d-%d\n",
                        elemIdx, table->rows(), table->columns(),
                        table->firstPosition(), table->lastPosition());
            } else {
                fprintf(stderr, "  elem[%d] FRAME pos=%d-%d\n",
                        elemIdx, childFrame->firstPosition(),
                        childFrame->lastPosition());
            }
        } else {
            QTextBlock block = it.currentBlock();
            fprintf(stderr, "  elem[%d] BLOCK pos=%d len=%d text=[%s]\n",
                    elemIdx, block.position(), block.length(),
                    block.text().left(60).toUtf8().constData());
        }
        elemIdx++;
    }
}

void TestTableDiagnostics::dumpItems(SceneCoordinator *coord,
                                      const char *label)
{
    fprintf(stderr, "\n=== SCENE ITEMS: %s ===\n", label);
    const auto &items = coord->items();
    fprintf(stderr, "  itemCount=%d\n", (int)items.size());
    for (int i = 0; i < items.size(); ++i) {
        auto *item = items[i];
        if (item->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(item);
            QTextDocument *doc = ti->document();
            int tableCount = 0;
            for (auto *f : doc->rootFrame()->childFrames()) {
                if (qobject_cast<QTextTable *>(f)) tableCount++;
            }
            QString md = ti->toMarkdown();
            fprintf(stderr, "  item[%d] TEXT charCount=%d tables=%d "
                    "markdown(first80)=[%s]\n",
                    i, doc->characterCount(), tableCount,
                    md.left(80).toUtf8().constData());
        } else {
            fprintf(stderr, "  item[%d] BLOCK markdown=[%s]\n",
                    i, item->toMarkdown().left(60).toUtf8().constData());
        }
    }
}

void TestTableDiagnostics::showcaseTableSection()
{
    // Reproduce the showcase's table section with surrounding content
    QString md = QStringLiteral(
        "## Tables\n"
        "\n"
        "| Feature | Status | Notes |\n"
        "|---------|--------|-------|\n"
        "| Headings | Done | H1-H6 with syntax highlighting |\n"
        "| Bold/Italic | Done | Plus strikethrough, inline code |\n"
        "| Code blocks | Done | KSyntaxHighlighting, 450+ languages |\n"
        "| Callouts | Done | 13 types with colors |\n"
        "| Math | Done | JKQTMathText for LaTeX |\n"
        "| Tables | Done | With alignment support |\n"
        "| Live Preview | Done | Cursor-aware block rendering |\n"
        "\n"
        "| Left | Center | Right |\n"
        "|:-----|:------:|------:|\n"
        "| L1   | C1     | R1    |\n"
        "| L2   | C2     | R2    |\n"
        "\n"
        "## Mathematics\n"
        "\n"
        "Inline math: The quadratic formula is $x = 1$.\n"
        "\n"
        "## Horizontal Rules\n"
        "\n"
        "Content below.");

    fprintf(stderr, "\n========== INPUT ==========\n%s\n",
            md.toUtf8().constData());
    fprintf(stderr, "INPUT LENGTH: %d\n", md.length());

    // Stage 1: Create editor and load
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(md);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    // Stage 2: Examine scene items immediately after load
    auto *coord = editor.coordinatorForTesting();
    dumpItems(coord, "after setPlainText");

    // Stage 3: Examine each text item's document structure
    const auto &items = coord->items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i]->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(items[i]);
            char label[64];
            snprintf(label, sizeof(label), "item[%d] after load", i);
            dumpDocumentStructure(ti->document(), label);
        }
    }

    // Stage 4: Check toPlainText (round-trip)
    QString output = editor.toPlainText();
    fprintf(stderr, "\n========== OUTPUT (toPlainText) ==========\n%s\n",
            output.toUtf8().constData());
    fprintf(stderr, "OUTPUT LENGTH: %d\n", output.length());

    // Stage 5: Wait for reparse and check again
    QTest::qWait(300); // wait for 150ms reparse timer + margin
    QApplication::processEvents();

    fprintf(stderr, "\n========== AFTER REPARSE ==========\n");
    dumpItems(coord, "after reparse");

    for (int i = 0; i < coord->items().size(); ++i) {
        if (coord->items()[i]->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(coord->items()[i]);
            char label[64];
            snprintf(label, sizeof(label), "item[%d] after reparse", i);
            dumpDocumentStructure(ti->document(), label);
        }
    }

    QString outputAfterReparse = editor.toPlainText();
    fprintf(stderr, "\n========== OUTPUT AFTER REPARSE ==========\n%s\n",
            outputAfterReparse.toUtf8().constData());

    // Stage 6: Verify buildHighlightingSource alignment
    fprintf(stderr, "\n========== HIGHLIGHTING SOURCE VERIFICATION ==========\n");
    for (int i = 0; i < coord->items().size(); ++i) {
        if (coord->items()[i]->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(coord->items()[i]);
            QTextDocument *doc = ti->document();

            // Strip substitutions to get source form (same precondition as reparse)
            ti->stripInlineSubstitutions();
            QString hlSrc = ti->buildHighlightingSource();
            ti->refreshInlineSubstitutions();

            fprintf(stderr, "  hlSrc length=%d docCharCount=%d match=%s\n",
                    (int)hlSrc.length(), doc->characterCount(),
                    hlSrc.length() == doc->characterCount() ? "YES" : "NO");

            // Verify each non-table block's text appears at the right offset
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                bool inTable = false;
                for (auto *f : doc->rootFrame()->childFrames()) {
                    if (b.position() >= f->firstPosition() && b.position() <= f->lastPosition()) {
                        inTable = true;
                        break;
                    }
                }
                if (inTable || b.text().isEmpty()) continue;

                QString fromHl = hlSrc.mid(b.position(), b.text().length());
                bool matches = (fromHl == b.text());
                if (!matches) {
                    fprintf(stderr, "  MISMATCH block pos=%d: doc=[%s] hl=[%s]\n",
                            b.position(), b.text().left(40).toUtf8().constData(),
                            fromHl.left(40).toUtf8().constData());
                }
            }
            fprintf(stderr, "  Verification complete for item %d\n", i);
        }
    }

    QVERIFY(true);
}

void TestTableDiagnostics::actualShowcaseRemnants()
{
    // Load the REAL showcase.md — not a simplified version
    QFile f(QStringLiteral(CMAKE_CURRENT_SOURCE_DIR "/showcase.md"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QSKIP("showcase.md not found");
    }
    QString md = QString::fromUtf8(f.readAll());
    f.close();

    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(md);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    auto *coord = editor.coordinatorForTesting();
    const auto &items = coord->items();

    for (int i = 0; i < items.size(); ++i) {
        if (!items[i]->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(items[i]);
        QTextDocument *doc = ti->document();

        QList<QTextTable *> tables;
        for (auto *frame : doc->rootFrame()->childFrames()) {
            if (auto *t = qobject_cast<QTextTable *>(frame))
                tables.append(t);
        }
        if (tables.isEmpty()) continue;

        fprintf(stderr, "\n=== ITEM %d: %d tables, charCount=%d ===\n",
                i, (int)tables.size(), doc->characterCount());

        // Dump blocks around table boundaries
        for (auto *t : tables) {
            fprintf(stderr, "  TABLE %dx%d firstPos=%d lastPos=%d\n",
                    t->rows(), t->columns(), t->firstPosition(), t->lastPosition());

            // Blocks BEFORE the table frame
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                if (b.position() >= t->firstPosition() - 20 &&
                    b.position() < t->firstPosition()) {
                    fprintf(stderr, "    BEFORE pos=%d len=%d text=[%s]\n",
                            b.position(), b.length(),
                            b.text().left(40).toUtf8().constData());
                }
            }
            // Blocks AFTER the table frame
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                if (b.position() > t->lastPosition() &&
                    b.position() <= t->lastPosition() + 30) {
                    fprintf(stderr, "    AFTER  pos=%d len=%d text=[%s]\n",
                            b.position(), b.length(),
                            b.text().left(40).toUtf8().constData());
                }
            }
        }

        // Dump ALL non-empty short blocks (potential remnants)
        fprintf(stderr, "  --- Short non-table blocks (len 2-5, potential remnants) ---\n");
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            bool inTable = false;
            for (auto *t : tables) {
                if (b.position() >= t->firstPosition() && b.position() <= t->lastPosition()) {
                    inTable = true;
                    break;
                }
            }
            if (!inTable && b.text().length() >= 1 && b.text().length() <= 4) {
                fprintf(stderr, "    REMNANT? pos=%d text=[%s]\n",
                        b.position(), b.text().toUtf8().constData());
            }
        }
    }

    QVERIFY(true);
}

QTEST_MAIN(TestTableDiagnostics)
#include "tst_table_diagnostics.moc"
