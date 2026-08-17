// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveBlockModel.h>

class TestEmptyDocFocus : public QObject {
    Q_OBJECT
private slots:
    void empty_doc_loads_one_empty_paragraph_block() {
        // An empty markdown load now synthesizes one empty Paragraph block
        // (Corbomite Cluster K P0: a genuinely zero-block document left every
        // view's caret null and swallowed all keystrokes — canvas's
        // View::keyPressEvent bails whenever m_caret.block.isNull(); a
        // brand-new note was unusable). The binding reflects that one row;
        // a host editor no longer needs a 0-row special case.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("");
        QCOMPARE(binding.model()->rowCount(), 1);
    }

    void edit_into_empty_block_lands_correctly() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("");
        // NOTE: toMarkdownUtf8() is NOT updated by D2 edits.
        // Check blockText() instead.
        doc.applyFlatEdit(0, 0, "X", Markoff::Origin::UserEdit);
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[0])), QStringLiteral("X"));
    }
};

QTEST_MAIN(TestEmptyDocFocus)
#include "tst_live_render_empty_doc_focus.moc"
