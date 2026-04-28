// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationParsePool : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsed_document_initially_null() {
        MarkoffDocument doc(1);
        QVERIFY(doc.parsedDocument() == nullptr);
    }

    void parse_updated_fires_after_local_edit() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);

        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "# Hello\n\nWorld\n";
        ed << i;
        doc.applyLocalEdit(ed);

        QVERIFY(spy.wait(2000));
        QVERIFY(doc.parsedDocument() != nullptr);
    }
};

QTEST_MAIN(TstFoundationParsePool)
#include "tst_foundation_parse_pool.moc"
