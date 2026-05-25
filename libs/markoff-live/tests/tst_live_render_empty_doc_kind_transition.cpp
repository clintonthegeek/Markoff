// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QCoreApplication>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

class TestEmptyDocKindTransition : public QObject {
    Q_OBJECT
private slots:
    void hash_space_makes_heading() {
        // Start with a plain paragraph, then edit it to "# Heading".
        // The binding sees the block as an Equal op (same blockId, new text),
        // so the kind-transition heuristic in onD2Changed fires and promotes
        // the block kind from Paragraph to Heading via Cmd::changeKind.
        // The changeKind command schedules another d2DocumentChanged, so
        // we drain the event loop twice.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Hello\n");

        // Replace the entire block text with a heading.
        // blockText includes trailing '\n', so the block occupies bytes 0..5.
        doc.applyFlatEdit(0, 5, "# Heading", Markoff::Origin::UserEdit);

        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        const auto blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());
        QCOMPARE(doc.blockKind(blocks[0]), Markoff::BlockKind::Heading);
    }
};

QTEST_MAIN(TestEmptyDocKindTransition)
#include "tst_live_render_empty_doc_kind_transition.moc"
