// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>

class TestSaveWatermark : public QObject {
    Q_OBJECT
private slots:
    void initially_clean_after_load() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello");
        // Caller is expected to mark saved at startup; verify that path:
        doc.markSaved(doc.d2EditSequence());
        QVERIFY(!doc.dirty());
        QCOMPARE(doc.savedSequence(), doc.d2EditSequence());
    }

    void becomes_dirty_after_edit() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello");
        doc.markSaved(doc.d2EditSequence());
        QSignalSpy spy(&doc, &Markoff::MarkoffDocument::dirtyChanged);
        doc.applyFlatEdit(5, 5, " world", Markoff::Origin::UserEdit);
        QVERIFY(doc.dirty());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    void becomes_clean_after_marksaved() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello");
        doc.markSaved(doc.d2EditSequence());
        doc.applyFlatEdit(5, 5, " world", Markoff::Origin::UserEdit);
        QSignalSpy spy(&doc, &Markoff::MarkoffDocument::dirtyChanged);
        doc.markSaved(doc.d2EditSequence());
        QVERIFY(!doc.dirty());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    }

    void dirtychanged_fires_only_on_transitions() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello");
        doc.markSaved(doc.d2EditSequence());
        QSignalSpy spy(&doc, &Markoff::MarkoffDocument::dirtyChanged);
        doc.applyFlatEdit(5, 5, " a", Markoff::Origin::UserEdit);
        doc.applyFlatEdit(7, 7, " b", Markoff::Origin::UserEdit);
        doc.applyFlatEdit(9, 9, " c", Markoff::Origin::UserEdit);
        QCOMPARE(spy.count(), 1);  // one transition only
    }

    void marksaved_with_old_seq_during_edit_race() {
        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("hello");
        doc.markSaved(doc.d2EditSequence());
        const quint64 captured = doc.d2EditSequence();
        doc.applyFlatEdit(5, 5, " world", Markoff::Origin::UserEdit);
        // Save started before edit; markSaved with stale seq → still dirty.
        doc.markSaved(captured);
        QVERIFY(doc.dirty());
    }
};

QTEST_MAIN(TestSaveWatermark)
#include "tst_markoff_doc_save_watermark.moc"
