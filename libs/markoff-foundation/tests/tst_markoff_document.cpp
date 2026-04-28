// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructed_with_replica_id() {
        MarkoffDocument doc(/*replicaId=*/42);
        QCOMPARE(doc.replicaId(), quint16(42));
    }

    void empty_document_has_zero_length() {
        MarkoffDocument doc(1);
        QCOMPARE(doc.visibleLength(), quint32(0));
        QVERIFY(doc.toMarkdownUtf8().isEmpty());
        QVERIFY(doc.toMarkdown().isEmpty());
    }

    void replica_ids_independent() {
        MarkoffDocument a(7);
        MarkoffDocument b(13);
        QCOMPARE(a.replicaId(), quint16(7));
        QCOMPARE(b.replicaId(), quint16(13));
    }
};

QTEST_MAIN(TstMarkoffDocument)
#include "tst_markoff_document.moc"
