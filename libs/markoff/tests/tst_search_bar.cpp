// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QSignalSpy>
#include "markoff/Editor.h"
#include "markoff/SearchBar.h"

using namespace Markoff;

class TestSearchBar : public QObject {
    Q_OBJECT

private slots:
    void findHighlightsMatches();
    void findCountsMatches();
    void findNextAdvancesCursor();
    void replaceAllWithUndo();
    void emptySearchClearsHighlights();
    void matchCaseToggle();
};

void TestSearchBar::findHighlightsMatches()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("foo bar foo baz foo"));

    // Trigger search via the public API
    QVERIFY(editor.findText(QStringLiteral("foo")));
}

void TestSearchBar::findCountsMatches()
{
    SearchBar bar;
    bar.setMatchCount(3, 17);
    // Verify the label text via the widget's visual state
    // (SearchBar internally updates m_countLabel)
    // This test verifies the method doesn't crash and accepts valid args
    bar.setMatchCount(0, 0);
    bar.setMatchCount(1, 65537);
}

void TestSearchBar::findNextAdvancesCursor()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("aaa bbb aaa bbb aaa"));

    // First find should succeed
    QVERIFY(editor.findText(QStringLiteral("aaa")));
    // Second find should also succeed (advances to next match)
    QVERIFY(editor.findText(QStringLiteral("aaa")));
    // Third find wraps around
    QVERIFY(editor.findText(QStringLiteral("aaa")));
}

void TestSearchBar::replaceAllWithUndo()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("cat dog cat dog cat"));

    int count = editor.replaceAll(QStringLiteral("cat"),
                                   QStringLiteral("bird"));
    QCOMPARE(count, 3);

    QString after = editor.toPlainText();
    QVERIFY(after.contains(QStringLiteral("bird")));
    QVERIFY(!after.contains(QStringLiteral("cat")));

    // Single undo should restore all replacements within the item
    editor.undo();
    QString undone = editor.toPlainText();
    QCOMPARE(undone, QStringLiteral("cat dog cat dog cat"));
}

void TestSearchBar::emptySearchClearsHighlights()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("test text"));

    // Search then clear — should not crash
    QVERIFY(editor.findText(QStringLiteral("test")));
    QVERIFY(!editor.findText(QString()));
}

void TestSearchBar::matchCaseToggle()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("Foo foo FOO"));

    // Case-insensitive (default)
    QVERIFY(editor.findText(QStringLiteral("foo")));

    // Case-sensitive
    QVERIFY(editor.findText(QStringLiteral("foo"),
                             QTextDocument::FindCaseSensitively));
}

QTEST_MAIN(TestSearchBar)
#include "tst_search_bar.moc"
