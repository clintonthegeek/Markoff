// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include "markoff/Editor.h"

using namespace Markoff;

class TestUndoGrouping : public QObject {
    Q_OBJECT
private slots:
    void headingToggleOnMultipleLinesIsAtomicUndo();
};

void TestUndoGrouping::headingToggleOnMultipleLinesIsAtomicUndo()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("Line one\nLine two\nLine three"));

    // Select all three lines
    editor.selectAll();

    // Toggle heading (increase level)
    editor.increaseHeadingLevel();

    // Verify all lines have heading markers
    QString after = editor.toPlainText();
    QVERIFY(after.contains(QStringLiteral("# Line one")));
    QVERIFY(after.contains(QStringLiteral("# Line two")));
    QVERIFY(after.contains(QStringLiteral("# Line three")));

    // Single undo should restore ALL lines
    editor.undo();
    QString undone = editor.toPlainText();
    QCOMPARE(undone, QStringLiteral("Line one\nLine two\nLine three"));
}

QTEST_MAIN(TestUndoGrouping)
#include "tst_undo_grouping.moc"
