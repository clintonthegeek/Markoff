// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/EditorContext.h>

class TestEditorContext : public QObject {
    Q_OBJECT
private slots:
    void defaults_are_paragraph() {
        Markoff::EditorContext c;
        QCOMPARE(c.blockKind, QString(Markoff::BlockKindNames::Paragraph));
        QCOMPARE(c.headingLevel, 0);
        QCOMPARE(c.inTable, false);
        QCOMPARE(c.tableRow, -1);
        QCOMPARE(c.tableCol, -1);
    }
    void block_kind_names_are_canonical() {
        QCOMPARE(QString(Markoff::BlockKindNames::Paragraph),      QStringLiteral("paragraph"));
        QCOMPARE(QString(Markoff::BlockKindNames::Heading),        QStringLiteral("heading"));
        QCOMPARE(QString(Markoff::BlockKindNames::ListItem),       QStringLiteral("list-item"));
        QCOMPARE(QString(Markoff::BlockKindNames::CodeBlock),      QStringLiteral("code-block"));
        QCOMPARE(QString(Markoff::BlockKindNames::Blockquote),     QStringLiteral("blockquote"));
        QCOMPARE(QString(Markoff::BlockKindNames::Image),          QStringLiteral("image"));
        QCOMPARE(QString(Markoff::BlockKindNames::Math),           QStringLiteral("math"));
        QCOMPARE(QString(Markoff::BlockKindNames::HorizontalRule), QStringLiteral("horizontal-rule"));
    }
};
QTEST_GUILESS_MAIN(TestEditorContext)
#include "tst_v10_editor_context.moc"
