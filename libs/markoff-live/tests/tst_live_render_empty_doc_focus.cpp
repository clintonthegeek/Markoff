// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveBlockModel.h>

class TestEmptyDocFocus : public QObject {
    Q_OBJECT
private slots:
    void empty_doc_loads_zero_blocks() {
        // An empty markdown load produces zero blocks in the document and
        // consequently zero rows in the model.  The binding correctly reflects
        // the underlying document state; a host editor must handle the 0-row
        // case when presenting an empty document to the user.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("");
        QCOMPARE(binding.model()->rowCount(), 0);
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
