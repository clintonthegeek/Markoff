// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/Cmd/Insert.h>
#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

class TstFoundationCmdInsert : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insert_link() {
        MarkoffDocument doc(1);
        Cmd::insertLink(doc, doc.anchorAt(0, Bias::Left), "click", "https://x");
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("[click](https://x)"));
    }

    void insert_image() {
        MarkoffDocument doc(1);
        Cmd::insertImage(doc, doc.anchorAt(0, Bias::Left), "alt", "img.png");
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("![alt](img.png)"));
    }

    void insert_horizontal_rule() {
        MarkoffDocument doc(1);
        Cmd::insertHorizontalRule(doc, doc.anchorAt(0, Bias::Left));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\n---\n"));
    }

    void insert_table_2x2_with_header() {
        MarkoffDocument doc(1);
        Cmd::insertTable(doc, doc.anchorAt(0, Bias::Left), 2, 2, true);
        const QByteArray expected = "|  |  |\n|---|---|\n|  |  |\n";
        QCOMPARE(doc.toMarkdownUtf8(), expected);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdInsert)
#include "tst_foundation_cmd_insert.moc"
