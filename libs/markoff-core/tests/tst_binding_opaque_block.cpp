// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opaque-block seam for SourceTextDocumentBinding.
//
// A view (markoff-styled) wants to render a Table block as a native QTextTable
// frame in the QTextDocument. The binding's reverse path is a whole-document
// text diff (expected = widgetFlatView() vs actual = toPlainText()); a frame's
// toPlainText is cell-text-as-paragraphs, never the pipe source, so a naive
// reverse pass corrupts the table region on EVERY model change. The opaque
// seam switches the reverse path to per-block reconciliation when an
// OpaqueBlockRenderer is registered, leaving unchanged frames in place.
//
// NOTE: QTextDocument::contentsChange (with position args) only fires when a
// QAbstractTextDocumentLayout is installed — tests use QPlainTextEdit::document().
// d2DocumentChanged is debounced via QTimer::singleShot(0,...) → pumpEvents().
//
// Spec: docs/superpowers/specs/2026-05-30-styled-table-rendering-design.md §3.
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/OpaqueBlockRenderer.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

static void pumpEvents() { QCoreApplication::processEvents(); }

namespace {

QTextTable *firstTable(QTextDocument *doc) {
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f)) return t;
    return nullptr;
}

bool hasBlockText(QTextDocument *doc, const QString &text) {
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.text() == text) return true;
    return false;
}

// Minimal renderer: treats Table blocks as opaque; materializes a 2x2 frame
// filled with the fixture's cells (a/b/c/d) and tagged with the BlockId key.
class TableTestRenderer : public OpaqueBlockRenderer {
public:
    bool isOpaque(BlockId, BlockKind kind) const override {
        return kind == BlockKind::Table;
    }
    int renderOpaque(QTextCursor &at, BlockId id) override {
        const int before = at.position();
        QTextTableFormat tf;
        tf.setBorder(1);
        tf.setProperty(OpaqueBlockKeyProperty, QString::number(id.raw()));
        QTextTable *t = at.insertTable(2, 2, tf);
        t->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("a"));
        t->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("b"));
        t->cellAt(1, 0).firstCursorPosition().insertText(QStringLiteral("c"));
        t->cellAt(1, 1).firstCursorPosition().insertText(QStringLiteral("d"));
        return at.position() - before;
    }
};

constexpr const char *kFixture =
    "para A\n\n| a | b |\n|---|---|\n| c | d |\n\npara B";

}  // namespace

class TstBindingOpaqueBlock : public QObject {
    Q_OBJECT
private slots:
    // The opaque-aware seed builds a real frame with intact cells and no raw
    // pipe text, with the surrounding paragraphs preserved.
    void seed_renders_table_as_frame() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(kFixture);
        QPlainTextEdit edit;
        QTextDocument *qdoc = edit.document();
        SourceTextDocumentBinding binding;
        TableTestRenderer rnd;
        binding.setTextDocument(qdoc);
        binding.setMarkoffDocument(&doc);
        binding.setOpaqueRenderer(&rnd);   // re-seeds opaque-aware

        QTextTable *t = firstTable(qdoc);
        QVERIFY(t != nullptr);
        QCOMPARE(t->rows(), 2);
        QCOMPARE(t->columns(), 2);
        QCOMPARE(t->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("a"));
        QCOMPARE(t->cellAt(1, 1).firstCursorPosition().block().text(),
                 QStringLiteral("d"));
        // No raw pipe text leaked into the document.
        QVERIFY(!qdoc->toPlainText().contains(QLatin1Char('|')));
        // Surrounding paragraphs intact.
        QVERIFY(hasBlockText(qdoc, QStringLiteral("para A")));
        QVERIFY(hasBlockText(qdoc, QStringLiteral("para B")));
    }

    // THE GATING SLOT: after editing an UNRELATED block, the frame must remain
    // intact AND no raw pipe text may appear. With the stub per-block path
    // (== whole-doc diff) this FAILS; the real per-block reconciliation makes
    // it pass. Falsifiability proof for the opaque seam.
    void frame_survives_unrelated_edit() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(kFixture);
        QPlainTextEdit edit;
        QTextDocument *qdoc = edit.document();
        SourceTextDocumentBinding binding;
        TableTestRenderer rnd;
        binding.setTextDocument(qdoc);
        binding.setMarkoffDocument(&doc);
        binding.setOpaqueRenderer(&rnd);
        QVERIFY(firstTable(qdoc) != nullptr);  // precondition

        // Edit para A (the first block): prepend 'X'.
        const auto blocks = doc.iterateBlocks();
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(blocks.front(), 0, 0, QByteArray("X"), t);
        }
        pumpEvents();  // debounced d2DocumentChanged → reverse pass

        QTextTable *t = firstTable(qdoc);
        QVERIFY2(t != nullptr, "table frame was destroyed by the reverse pass");
        QCOMPARE(t->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("a"));
        QCOMPARE(t->cellAt(1, 1).firstCursorPosition().block().text(),
                 QStringLiteral("d"));
        QVERIFY2(!qdoc->toPlainText().contains(QLatin1Char('|')),
                 "raw pipe text leaked into the document");
        QVERIFY(hasBlockText(qdoc, QStringLiteral("Xpara A")));
        QVERIFY(hasBlockText(qdoc, QStringLiteral("para B")));
    }

    // Editing the block AFTER the frame (the region carrying Qt's trailing
    // structural empty block) must update that block and leave the frame intact.
    void edit_after_frame_preserves_frame() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(kFixture);
        QPlainTextEdit edit;
        QTextDocument *qdoc = edit.document();
        SourceTextDocumentBinding binding;
        TableTestRenderer rnd;
        binding.setTextDocument(qdoc);
        binding.setMarkoffDocument(&doc);
        binding.setOpaqueRenderer(&rnd);

        // Append '!' to para B (the last model block).
        const auto blocks = doc.iterateBlocks();
        const BlockId last = blocks.back();
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(last, quint32(doc.blockText(last).size()),
                                  0, QByteArray("!"), t);
        }
        pumpEvents();

        QTextTable *t = firstTable(qdoc);
        QVERIFY2(t != nullptr, "frame destroyed by edit after it");
        QCOMPARE(t->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("a"));
        QVERIFY(!qdoc->toPlainText().contains(QLatin1Char('|')));
        QVERIFY(hasBlockText(qdoc, QStringLiteral("para A")));
        QVERIFY(hasBlockText(qdoc, QStringLiteral("para B!")));
    }
};

QTEST_MAIN(TstBindingOpaqueBlock)
#include "tst_binding_opaque_block.moc"
