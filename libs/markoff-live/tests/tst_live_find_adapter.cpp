// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/FindSpan.h>
#include <markoff/live/LiveBlockModel.h>

#include "Detail/LiveFindAdapter.h"

using namespace Markoff::Live;

// Populate `model` with rows mirroring `doc`'s blocks (anchor + kind + text).
static void mirrorDocBlocksIntoModel(const Markoff::MarkoffDocument &doc,
                                     LiveBlockModel &model)
{
    for (const Markoff::BlockId &id : doc.iterateBlocks()) {
        model.insertTestRow(id,
                            /*kind*/ QStringLiteral("paragraph"),
                            QString::fromUtf8(doc.blockText(id)));
    }
}

class TestLiveFindAdapter : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<Markoff::BlockAnchor>("Markoff::BlockAnchor");
        qRegisterMetaType<Markoff::Live::FindSpan>("Markoff::Live::FindSpan");
        qRegisterMetaType<Markoff::FindController::Match>(
            "Markoff::FindController::Match");
    }

    void matchesChanged_populatesModelFindSpans_perBlock() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("the cat\n\nsat on the mat"));

        LiveBlockModel model;
        mirrorDocBlocksIntoModel(doc, model);
        QCOMPARE(model.rowCount(), 2);

        Detail::LiveFindAdapter adapter(&model, /*cursorState*/ nullptr);
        Markoff::FindController fc(&doc);
        adapter.attach(&fc);
        fc.activate();
        fc.setNeedle("the");

        const auto spans0 = model.data(model.index(0, 0),
            LiveBlockModel::FindSpansRole).value<QList<FindSpan>>();
        const auto spans1 = model.data(model.index(1, 0),
            LiveBlockModel::FindSpansRole).value<QList<FindSpan>>();
        QCOMPARE(spans0.size(), 1);
        QCOMPARE(spans1.size(), 1);
        QCOMPARE(spans0.first().byteOffset, quint32(0));
        QCOMPARE(spans0.first().byteLength, quint32(3));
        // "sat on the mat" → "the" at byte 7.
        QCOMPARE(spans1.first().byteOffset, quint32(7));
        QCOMPARE(spans1.first().byteLength, quint32(3));
    }

    void currentMatchChanged_updatesIsCurrentFlags() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("the cat\n\nsat on the mat"));

        LiveBlockModel model;
        mirrorDocBlocksIntoModel(doc, model);

        Detail::LiveFindAdapter adapter(&model, /*cursorState*/ nullptr);
        Markoff::FindController fc(&doc);
        adapter.attach(&fc);
        fc.activate();
        fc.setNeedle("the");

        // Initial state: matchCount = 2, currentMatchIndex = 0 → block 0 current.
        {
            const auto s0 = model.data(model.index(0, 0),
                LiveBlockModel::FindSpansRole).value<QList<FindSpan>>();
            const auto s1 = model.data(model.index(1, 0),
                LiveBlockModel::FindSpansRole).value<QList<FindSpan>>();
            QVERIFY(s0.first().isCurrent);
            QVERIFY(!s1.first().isCurrent);
        }

        fc.findNext();

        // After next: block 1 current.
        {
            const auto s0 = model.data(model.index(0, 0),
                LiveBlockModel::FindSpansRole).value<QList<FindSpan>>();
            const auto s1 = model.data(model.index(1, 0),
                LiveBlockModel::FindSpansRole).value<QList<FindSpan>>();
            QVERIFY(!s0.first().isCurrent);
            QVERIFY(s1.first().isCurrent);
        }
    }

    void detach_clearsAllSpans() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray("the cat"));

        LiveBlockModel model;
        mirrorDocBlocksIntoModel(doc, model);

        Detail::LiveFindAdapter adapter(&model, /*cursorState*/ nullptr);
        Markoff::FindController fc(&doc);
        adapter.attach(&fc);
        fc.activate();
        fc.setNeedle("the");
        QVERIFY(!model.data(model.index(0, 0),
            LiveBlockModel::FindSpansRole).value<QList<FindSpan>>().isEmpty());

        adapter.detach();
        QVERIFY(model.data(model.index(0, 0),
            LiveBlockModel::FindSpansRole).value<QList<FindSpan>>().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLiveFindAdapter)
#include "tst_live_find_adapter.moc"
