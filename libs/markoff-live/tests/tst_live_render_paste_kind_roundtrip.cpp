// SPDX-License-Identifier: GPL-3.0-or-later
//
// Investigation: does copy+paste preserve block KINDs across the
// LiveClipboardController → applyStructuredPaste round-trip?
//
// User-reported dogfood bug: pasted headings render as paragraphs,
// paragraphs render as headings, list-items don't behave like list-items.
//
// This test exercises three reproductions in isolation:
//   A. Full-doc copy → paste at end of doc (cross-block at boundary).
//   B. Full-block copy of a single Heading → paste at end of a paragraph.
//   C. Partial-block copy of "ading One" out of a Heading → paste at end.
//
// We do NOT call this test green-on-pass; the asserts encode the EXPECTED
// behavior (each pasted block keeps its source kind). Failures here describe
// what the user is actually seeing.
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/UndoLog.h>
#include <markoff/live/BlockKind.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveBlockModel.h>

namespace BlockKind = Markoff::Live::BlockKind;

namespace {

// Spin the event loop until LiveListModelBinding has drained any pending
// d2DocumentChanged (which runs kind-transition heuristics).
void settle(Markoff::Live::LiveListModelBinding *binding) {
    QSignalSpy spy(binding->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex,QList<int>)));
    QCoreApplication::processEvents();
    QTest::qWait(50);
    QCoreApplication::processEvents();
}

QString kindAtRow(Markoff::Live::LiveListModelBinding *binding, int row) {
    return binding->model()->recordAt(row).kind;
}

QString textAtRow(Markoff::Live::LiveListModelBinding *binding, int row) {
    return binding->model()->recordAt(row).text;
}

}  // namespace

class TestPasteKindRoundTrip : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void initTestCase() {
        // QApplication required for QClipboard.
    }

    /// A. Multi-block copy → paste at end of doc.
    /// Source doc: "# H\n\nP". Two blocks: Heading, Paragraph.
    /// Copy all, paste at end. Expect 4 blocks: H, P, H, P.
    void copy_all_paste_at_end_preserves_kinds() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("# H\n\nP\n");
        settle(&binding);

        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(kindAtRow(&binding, 0), BlockKind::Heading);
        QCOMPARE(kindAtRow(&binding, 1), BlockKind::Paragraph);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.selectionView());
        cc.setModel(binding.model());

        // Select all: from block 0 pos 0 to block 1 pos end.
        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(1, textAtRow(&binding, 1).length());

        cc.copy();
        QVERIFY(QApplication::clipboard()->mimeData()->hasFormat(
            Markoff::Live::LiveClipboardController::kBlocksMime));

        // Collapse selection to end of last block.
        const int lastRow = binding.model()->rowCount() - 1;
        const int lastPos = textAtRow(&binding, lastRow).length();
        binding.selectionView()->begin(lastRow, lastPos);
        binding.selectionView()->extend(lastRow, lastPos);

        cc.paste();
        settle(&binding);

        const int rc = binding.model()->rowCount();
        qInfo() << "After paste: rowCount=" << rc;
        for (int i = 0; i < rc; ++i)
            qInfo() << "  row" << i << "kind=" << kindAtRow(&binding, i)
                    << "text=" << textAtRow(&binding, i);

        // Destination-wins (per user direction 2026-05-10): pasting at end of
        // a paragraph absorbs the first pasted chunk into that paragraph; the
        // \n\n separator creates a new paragraph for the second chunk. So
        // expected layout is [H "# H"], [P "P# H"], [P "P"] (3 rows, not 4).
        QCOMPARE(binding.model()->rowCount(), 3);
        QCOMPARE(kindAtRow(&binding, 0), BlockKind::Heading);
        QCOMPARE(kindAtRow(&binding, 1), BlockKind::Paragraph);
        QCOMPARE(kindAtRow(&binding, 2), BlockKind::Paragraph);
        QVERIFY2(textAtRow(&binding, 1).endsWith(QStringLiteral("# H")),
                 qPrintable(QStringLiteral("row 1 should end with the pasted heading prefix; got: ")
                            + textAtRow(&binding, 1)));
    }

    /// B. Single full-block heading copy → paste at end of paragraph.
    /// Source: "Paragraph\n". Clipboard: a copied "# H" heading.
    /// Paste at end of paragraph. Expect either (i) heading appended as new
    /// block (best), or (ii) heading concatenated into paragraph (current).
    void single_heading_paste_into_paragraph_end() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Paragraph\n");
        settle(&binding);
        QCOMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(kindAtRow(&binding, 0), BlockKind::Paragraph);

        // Build a clipboard payload manually, kind=heading, text="# H".
        QJsonObject payload;
        payload["version"]         = 1;
        payload["sourceReplicaId"] = 42;
        QJsonArray blocks;
        QJsonObject b;
        b["kind"] = BlockKind::Heading;
        b["text"] = QStringLiteral("# H");
        QJsonObject attrs;
        attrs[QString::fromUtf8(Markoff::AttrNames::Level)] = 1;
        attrs[QString::fromUtf8(Markoff::AttrNames::HeadingForm)] = QStringLiteral("atx");
        b["attrs"] = attrs;
        blocks.append(b);
        payload["blocks"] = blocks;

        auto *mime = new QMimeData();
        mime->setData(Markoff::Live::LiveClipboardController::kBlocksMime,
                      QJsonDocument(payload).toJson(QJsonDocument::Compact));
        mime->setText(QStringLiteral("# H"));
        QApplication::clipboard()->setMimeData(mime);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.selectionView());
        cc.setModel(binding.model());

        // Collapse selection to end of "Paragraph".
        binding.selectionView()->begin(0, textAtRow(&binding, 0).length());
        binding.selectionView()->extend(0, textAtRow(&binding, 0).length());

        cc.paste();
        settle(&binding);

        const int rc = binding.model()->rowCount();
        qInfo() << "After paste-heading-at-paragraph-end: rowCount=" << rc;
        for (int i = 0; i < rc; ++i)
            qInfo() << "  row" << i << "kind=" << kindAtRow(&binding, i)
                    << "text=" << textAtRow(&binding, i);
    }

    /// E. Off-by-one fix: applyFlatEdit at a block boundary, cursor edit
    /// (oldStart == oldEnd). Pre-fix the `<=` matched the previous block
    /// and appended content there; post-fix cursor edits bias to the
    /// next block. We call applyFlatEdit directly (rather than going
    /// through LiveClipboardController) to keep this test small and free
    /// of clipboard-state coupling.
    void apply_flat_edit_at_boundary_lands_in_next_block() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("P\n\nQ\n");
        settle(&binding);
        QCOMPARE(binding.model()->rowCount(), 2);

        // Compute the byte offset at the boundary: end of block 0 buffer
        // (= start of block 1 buffer).
        const auto ids = doc.iterateBlocks();
        const uint32_t block0Bytes =
            static_cast<uint32_t>(doc.blockText(ids[0]).size());

        // Insert "X" at the boundary as a cursor edit (start == end).
        doc.applyFlatEdit(block0Bytes, block0Bytes, QByteArrayLiteral("X"),
                          Markoff::Origin::UserEdit);
        settle(&binding);

        // With the off-by-one fix, the "X" lands in block 1's buffer
        // (now "XQ\n"), not appended to block 0 (which would have been
        // "P\nX" pre-fix).
        const auto idsAfter = doc.iterateBlocks();
        QCOMPARE(idsAfter.size(), static_cast<size_t>(2));
        QCOMPARE(doc.blockText(idsAfter[0]), QByteArrayLiteral("P\n"));
        QCOMPARE(doc.blockText(idsAfter[1]), QByteArrayLiteral("XQ\n"));
    }

    /// D. User-reported repro: foundation-design.md style header + body.
    /// Source: "## 1. TL;DR\n\nThe existing Markoff family...the bottom."
    /// Selection: full doc. Paste at end of paragraph.
    /// Expected (destination-wins): pasted heading text concatenates into
    /// existing paragraph; new paragraph contains "The existing...".
    /// Bug observed: new paragraph renders as a heading.
    void foundation_design_repro() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        const QByteArray src =
            "## 1. TL;DR\n\nThe existing Markoff family has accumulated significant "
            "architectural debt concentrated in `markoff-live` (per the audit: bandage-"
            "saturated `Editor`, leaky `MarkdownView` base contract). Rather than refactoring "
            "each pain point in place, this spec proposes a **hard reset on the upper part "
            "of the stack** while salvaging and refining the bottom.\n";
        doc.loadFromMarkdown(src);
        settle(&binding);

        const int rcBefore = binding.model()->rowCount();
        qInfo() << "Initial blocks (count=" << rcBefore << ")";
        for (int i = 0; i < rcBefore; ++i)
            qInfo().noquote() << "  row" << i << "kind=" << kindAtRow(&binding, i)
                              << "len=" << textAtRow(&binding, i).length();

        QCOMPARE(rcBefore, 2);
        QCOMPARE(kindAtRow(&binding, 0), BlockKind::Heading);
        QCOMPARE(kindAtRow(&binding, 1), BlockKind::Paragraph);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.selectionView());
        cc.setModel(binding.model());

        // Select from start of heading to end of paragraph.
        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(1, textAtRow(&binding, 1).length());
        cc.copy();
        qInfo().noquote() << "Clipboard payload:"
                          << QApplication::clipboard()->mimeData()->data(
                                 Markoff::Live::LiveClipboardController::kBlocksMime);

        // Paste at end of paragraph.
        const int paraEnd = textAtRow(&binding, 1).length();
        binding.selectionView()->begin(1, paraEnd);
        binding.selectionView()->extend(1, paraEnd);
        cc.paste();
        settle(&binding);

        const int rcAfter = binding.model()->rowCount();
        qInfo() << "After paste: rowCount=" << rcAfter;
        for (int i = 0; i < rcAfter; ++i) {
            const QString txt = textAtRow(&binding, i);
            qInfo().noquote() << "  row" << i << "kind=" << kindAtRow(&binding, i)
                              << "len=" << txt.length()
                              << "head=" << txt.left(60)
                              << "tail=" << txt.right(60);
            const auto rec = binding.model()->recordAt(i);
            qInfo().noquote() << "    headingLevel=" << rec.headingLevel
                              << "headingForm=" << rec.headingForm;
        }
    }

    /// C. Partial selection of heading content (skip the "# " prefix) →
    /// paste at end. Expect the receiver to see only the substring; the
    /// clipboard JSON loses the prefix because serializeSelection takes
    /// rec.text.mid(start, end-start) only.
    void partial_heading_selection_loses_prefix() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("# Heading One\n\nParagraph\n");
        settle(&binding);
        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(kindAtRow(&binding, 0), BlockKind::Heading);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.selectionView());
        cc.setModel(binding.model());

        // Select "Heading One" inside the heading (skip "# ").
        binding.selectionView()->begin(0, 2);
        binding.selectionView()->extend(0, textAtRow(&binding, 0).length());

        cc.copy();
        const QByteArray payloadBytes =
            QApplication::clipboard()->mimeData()->data(
                Markoff::Live::LiveClipboardController::kBlocksMime);
        qInfo() << "Clipboard JSON payload:" << payloadBytes;

        // Paste at end of paragraph (row 1).
        const int paraRow = 1;
        const int paraEnd = textAtRow(&binding, paraRow).length();
        binding.selectionView()->begin(paraRow, paraEnd);
        binding.selectionView()->extend(paraRow, paraEnd);
        cc.paste();
        settle(&binding);

        const int rc = binding.model()->rowCount();
        qInfo() << "After partial-heading paste at paragraph end: rowCount=" << rc;
        for (int i = 0; i < rc; ++i)
            qInfo() << "  row" << i << "kind=" << kindAtRow(&binding, i)
                    << "text=" << textAtRow(&binding, i);
    }
};

QTEST_MAIN(TestPasteKindRoundTrip)
#include "tst_live_render_paste_kind_roundtrip.moc"
