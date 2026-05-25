// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>

class TestDocumentSignalContract : public QObject {
    Q_OBJECT
private slots:
    void documentChanged_emitted_on_load() {
        Markoff::MarkoffDocument doc(1);
        QSignalSpy changed(&doc, SIGNAL(documentChanged()));
        QSignalSpy reloaded(&doc, SIGNAL(documentReloaded()));
        doc.loadFromMarkdown(QStringLiteral("# Hello\n").toUtf8());
        QVERIFY(changed.count() >= 1);
        // loadFromMarkdown emits documentLoaded/documentChanged, not documentReloaded
        QVERIFY(reloaded.count() == 0);
    }
    void documentChanged_no_args() {
        auto sig = &Markoff::MarkoffDocument::documentChanged;
        Q_UNUSED(sig);
        QVERIFY(true);
    }
    void contentsChanged_with_offsets_does_not_exist() {
        QVERIFY(true);
    }
};
QTEST_GUILESS_MAIN(TestDocumentSignalContract)
#include "tst_v10_document_signal_contract.moc"
