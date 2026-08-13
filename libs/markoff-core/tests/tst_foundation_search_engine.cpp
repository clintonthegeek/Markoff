// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/core/Session.h>

using namespace Markoff;

class TstFoundationSearchEngine : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // clearMatches is a small Session-selection utility independent of the
    // legacy findAll/findNext/findPrevious trio removed 2026-06-10 (queue
    // #11) — populate a SearchMatch selection directly rather than via the
    // deleted legacy-buffer search.
    void clear_matches_removes_search_kind() {
        MarkoffDocument doc(1);
        doc.resetContent("abc", Origin::TestFixture);

        Session *sess = doc.createSession();
        Selection x;
        x.kind = Selection::Kind::SearchMatch;
        x.anchor = doc.textAnchorAt(0, /*rightBias*/ false);
        x.active = doc.textAnchorAt(1, /*rightBias*/ true);
        sess->addSecondarySelection(x);

        SearchEngine s;
        s.clearMatches(sess);
        for (const Selection &y : sess->secondarySelections())
            QVERIFY(y.kind != Selection::Kind::SearchMatch);
    }

    // D2: per-block search finds matches in multiple blocks
    void search_findsMatchAcrossBlocks() {
        MarkoffDocument doc(1);
        // Two paragraphs separated by a blank line: block 0 = "foo bar",
        // block 1 = "baz foo".
        doc.loadFromMarkdown("foo bar\n\nbaz foo\n");

        const auto hits = SearchEngine::findByBlock(doc, "foo");
        QCOMPARE(hits.size(), 2);

        // The two blocks must be distinct.
        QVERIFY(hits[0].blockId != hits[1].blockId);

        // First block: "foo" at byte offset 0, length 3.
        QCOMPARE(hits[0].matchStart, uint32_t(0));
        QCOMPARE(hits[0].matchLen,   uint32_t(3));

        // Second block: "baz foo" — "foo" starts at byte offset 4, length 3.
        QCOMPARE(hits[1].matchStart, uint32_t(4));
        QCOMPARE(hits[1].matchLen,   uint32_t(3));
    }
};

QTEST_APPLESS_MAIN(TstFoundationSearchEngine)
#include "tst_foundation_search_engine.moc"
