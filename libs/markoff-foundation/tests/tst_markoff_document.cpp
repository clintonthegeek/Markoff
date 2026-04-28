// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

Q_DECLARE_METATYPE(QList<Markoff::MarkoffEdit>)

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

    void apply_local_edit_inserts_text() {
        MarkoffDocument doc(1);
        // Seed via direct buffer init through a single insert at offset 0.
        QList<MarkoffEdit> seed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "hello";
        seed << ins;
        doc.applyLocalEdit(seed);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello"));
        QCOMPARE(doc.visibleLength(), quint32(5));
    }

    void apply_local_edit_replaces_range() {
        MarkoffDocument doc(1);

        QList<MarkoffEdit> seed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "hello world";
        seed << ins;
        doc.applyLocalEdit(seed);

        QList<MarkoffEdit> edits;
        MarkoffEdit replace;
        replace.oldStart = 6;
        replace.oldEnd = 11;
        replace.newText = "there";
        edits << replace;
        doc.applyLocalEdit(edits);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello there"));
    }

    void apply_local_edit_deletes_range() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> seed;
        MarkoffEdit ins;
        ins.oldStart = 0;
        ins.oldEnd = 0;
        ins.newText = "abcdef";
        seed << ins;
        doc.applyLocalEdit(seed);

        QList<MarkoffEdit> del;
        MarkoffEdit d;
        d.oldStart = 2;
        d.oldEnd = 4;
        d.newText.clear();
        del << d;
        doc.applyLocalEdit(del);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("abef"));
    }

    void apply_local_edit_emits_contents_changed() {
        MarkoffDocument doc(1);
        // Seed.
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "abc";
            seed << i;
            doc.applyLocalEdit(seed);
        }

        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        QList<MarkoffEdit> edits;
        MarkoffEdit ins;
        ins.oldStart = 1;
        ins.oldEnd = 1;
        ins.newText = "X";
        edits << ins;
        doc.applyLocalEdit(edits);

        QCOMPARE(spy.count(), 1);
        const QList<MarkoffEdit> received =
            spy.takeFirst().at(0).value<QList<MarkoffEdit>>();
        QVERIFY(!received.isEmpty());
        // The first received edit should describe the insertion at oldStart=1.
        QCOMPARE(received.first().oldStart, quint32(1));
    }

    void apply_local_edit_batch() {
        MarkoffDocument doc(1);
        {
            QList<MarkoffEdit> seed;
            MarkoffEdit i;
            i.oldStart = 0;
            i.oldEnd = 0;
            i.newText = "aaaa bbbb cccc";
            seed << i;
            doc.applyLocalEdit(seed);
        }
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("aaaa bbbb cccc"));

        // Two non-overlapping replacements in one batch.
        QList<MarkoffEdit> edits;
        MarkoffEdit r1;
        r1.oldStart = 0;
        r1.oldEnd = 4;
        r1.newText = "AAAA";
        edits << r1;
        MarkoffEdit r2;
        r2.oldStart = 10;
        r2.oldEnd = 14;
        r2.newText = "CCCC";
        edits << r2;
        doc.applyLocalEdit(edits);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("AAAA bbbb CCCC"));
    }
};

int main(int argc, char *argv[]) {
    qRegisterMetaType<QList<Markoff::MarkoffEdit>>("QList<Markoff::MarkoffEdit>");
    QApplication app(argc, argv);
    TstMarkoffDocument tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_markoff_document.moc"
