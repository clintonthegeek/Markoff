// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QPlainTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

class TstSourceParagraphMargins : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void margins_present_on_every_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        QTest::qWait(50);

        QTextDocument *qdoc = e.plainTextEdit()->document();
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
            QTextBlockFormat bf = b.blockFormat();
            QVERIFY2(bf.topMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 topMargin=%2")
                                .arg(b.blockNumber()).arg(bf.topMargin())));
            QVERIFY2(bf.bottomMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 bottomMargin=%2")
                                .arg(b.blockNumber()).arg(bf.bottomMargin())));
        }
    }
};

QTEST_MAIN(TstSourceParagraphMargins)
#include "tst_source_paragraph_margins.moc"
