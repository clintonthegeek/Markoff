// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/AttrNames.h>
using namespace Markoff;

class TstD5SiblingMapEmit : public QObject {
    Q_OBJECT
private slots:
    void changeKind_emitsKindTagMapAndAttrOps() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("para\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);

        const auto blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());
        Cmd::changeKind(doc, blocks.front(), BlockKind::Heading,
                        { AttrNames::Level }, { AttrValue{1} });

        QCOMPARE(spy.count(), 1);
        const auto ops = spy.takeFirst().at(0).value<QList<MarkoffOp>>();
        bool sawKindOp = false, sawAttrOp = false;
        for (const auto &op : ops) {
            if (op.target == CrdtTarget::KindTagMap)    sawKindOp = true;
            if (op.target == CrdtTarget::BlockAttrsMap) sawAttrOp = true;
        }
        QVERIFY2(sawKindOp, "expected a KindTagMap op");
        QVERIFY2(sawAttrOp, "expected a BlockAttrsMap op");
    }

    void singleUser_emitsNothing() {
        MarkoffDocument doc;  // no replicaId — single-user mode
        doc.loadFromMarkdown(QByteArrayLiteral("para\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);
        const auto blocks = doc.iterateBlocks();
        Cmd::changeKind(doc, blocks.front(), BlockKind::Heading,
                        { AttrNames::Level }, { AttrValue{1} });
        QCOMPARE(spy.count(), 0);
    }
};
QTEST_MAIN(TstD5SiblingMapEmit)
#include "tst_d5_sibling_map_emit.moc"
