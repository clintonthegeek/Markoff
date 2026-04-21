// SPDX-License-Identifier: GPL-3.0-or-later
// PhaseC3: Whole file pending rewrite against Phase-C3 API in Task 8.
// Current API uses Phase-A setPlainText/plainText removed in Task 6.
// Rewrite mappings:
//   setPlainText(text) → resetContent(text, Origin::FirstOpen) (or TestFixture)
//   plainText() → toMarkdown()
#include <QTest>
#include <QSignalSpy>

#include <markoff/MarkoffDocument.h>
#include <markoff/SearchController.h>
#include <markoff/SearchAdapter.h>

using namespace Markoff;

namespace {
class StubAdapter : public SearchAdapter {
public:
    int cursorSourceOffset() const override { return cursor; }
    void highlightMatches(QVector<TextSpan> s) override { highlighted = s; }
    void clearMatchHighlight() override { highlighted.clear(); }
    void scrollMatchIntoView(TextSpan s) override { scrolled = s; }

    int cursor = 0;
    QVector<TextSpan> highlighted;
    TextSpan scrolled{-1, -1};
};
}  // namespace

class TstSearchController : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void findsAllLiteralMatches() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("ab ab AB"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("ab"));
        QCOMPARE(c.matchCount(), 2);  // case-insensitive off by default? test below
    }

    void caseSensitiveFlag() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("ab ab AB"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        SearchController::Flags f;
        f.caseSensitive = true;
        c.setFlags(f);
        c.setQuery(QStringLiteral("ab"));
        QCOMPARE(c.matchCount(), 2);
        f.caseSensitive = false;
        c.setFlags(f);
        QCOMPARE(c.matchCount(), 3);
    }

    void nextPrevWraps() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("x y x y x"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("x"));
        QCOMPARE(c.matchCount(), 3);
        QCOMPARE(c.currentIndex(), 0);
        c.next();
        QCOMPARE(c.currentIndex(), 1);
        c.next();
        QCOMPARE(c.currentIndex(), 2);
        c.next();
        QCOMPARE(c.currentIndex(), 0);  // wrap
        c.prev();
        QCOMPARE(c.currentIndex(), 2);  // wrap back
    }

    void highlightsMatchesOnQueryChange() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a bb a"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(adapter.highlighted.size(), 2);
    }

    void clearsOnEmptyQuery() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("a"));
        QCOMPARE(adapter.highlighted.size(), 1);
        c.setQuery({});
        QVERIFY(adapter.highlighted.isEmpty());
    }

    void wholeWordFlag() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("cat catalog category"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        SearchController::Flags f;
        f.wholeWord = true;
        c.setFlags(f);
        c.setQuery(QStringLiteral("cat"));
        QCOMPARE(c.matchCount(), 1);
    }

    void regexFlag() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("foo123 bar456"));
        StubAdapter adapter;
        SearchController c(&doc, &adapter);
        SearchController::Flags f;
        f.regex = true;
        c.setFlags(f);
        c.setQuery(QStringLiteral("[a-z]+\\d+"));
        QCOMPARE(c.matchCount(), 2);
    }

    void nextStartsFromCursor() {
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("xxxxx"));
        StubAdapter adapter;
        adapter.cursor = 3;
        SearchController c(&doc, &adapter);
        c.setQuery(QStringLiteral("x"));
        // Adapter cursor at 3 → first match at or after 3 is index 3.
        QCOMPARE(c.currentIndex(), 3);
    }
};

QTEST_MAIN(TstSearchController)
#include "tst_search_controller.moc"
