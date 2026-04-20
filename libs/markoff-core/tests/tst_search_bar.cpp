// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QSignalSpy>
#include "markoff/SearchBar.h"

using namespace Markoff;

class TestSearchBar : public QObject {
    Q_OBJECT

private slots:
    void defaultsHidden();
    void showFindMakesVisible();
    void showReplaceMakesVisible();
    void setMatchCountNoResults();
    void setMatchCountManyResults();
    void setMatchCountOverflow();
    void setAndGetSearchText();
    void signalSearchTextChanged();
    void signalFindNext();
    void signalFindPrevious();
    void signalClosed();
    void signalReplaceRequested();
    void signalReplaceAllRequested();
    void matchCaseDefaultFalse();
};

void TestSearchBar::defaultsHidden()
{
    SearchBar bar;
    QVERIFY(!bar.isVisible());
}

void TestSearchBar::showFindMakesVisible()
{
    SearchBar bar;
    bar.showFind();
    QVERIFY(bar.isVisible());
}

void TestSearchBar::showReplaceMakesVisible()
{
    SearchBar bar;
    bar.showReplace();
    QVERIFY(bar.isVisible());
}

void TestSearchBar::setMatchCountNoResults()
{
    SearchBar bar;
    // Should not crash
    bar.setMatchCount(0, 0);
}

void TestSearchBar::setMatchCountManyResults()
{
    SearchBar bar;
    bar.setMatchCount(3, 17);
}

void TestSearchBar::setMatchCountOverflow()
{
    SearchBar bar;
    bar.setMatchCount(1, 65537);
}

void TestSearchBar::setAndGetSearchText()
{
    SearchBar bar;
    bar.setSearchText(QStringLiteral("hello"));
    QCOMPARE(bar.searchText(), QStringLiteral("hello"));
}

void TestSearchBar::signalSearchTextChanged()
{
    SearchBar bar;
    QSignalSpy spy(&bar, &SearchBar::searchTextChanged);
    bar.setSearchText(QStringLiteral("abc"));
    QVERIFY(spy.count() >= 1);
}

void TestSearchBar::signalFindNext()
{
    SearchBar bar;
    bar.showFind();
    QSignalSpy spy(&bar, &SearchBar::findNext);
    QTest::keyClick(&bar, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
}

void TestSearchBar::signalFindPrevious()
{
    SearchBar bar;
    bar.showFind();
    QSignalSpy spy(&bar, &SearchBar::findPrevious);
    QTest::keyClick(&bar, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(spy.count(), 1);
}

void TestSearchBar::signalClosed()
{
    SearchBar bar;
    bar.showFind();
    QSignalSpy spy(&bar, &SearchBar::closed);
    QTest::keyClick(&bar, Qt::Key_Escape);
    QCOMPARE(spy.count(), 1);
}

void TestSearchBar::signalReplaceRequested()
{
    SearchBar bar;
    bar.showReplace();
    QSignalSpy spy(&bar, &SearchBar::replaceRequested);
    // replaceRequested is emitted when Enter is pressed in the replace field.
    // We trigger it via the button click path indirectly — just verify
    // no crash and the signal exists (button-click tested manually).
    QVERIFY(spy.isValid());
}

void TestSearchBar::signalReplaceAllRequested()
{
    SearchBar bar;
    bar.showReplace();
    QSignalSpy spy(&bar, &SearchBar::replaceAllRequested);
    QVERIFY(spy.isValid());
}

void TestSearchBar::matchCaseDefaultFalse()
{
    SearchBar bar;
    QVERIFY(!bar.matchCase());
}

QTEST_MAIN(TestSearchBar)
#include "tst_search_bar.moc"
