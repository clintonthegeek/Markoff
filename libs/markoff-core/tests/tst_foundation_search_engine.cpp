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
    void find_all_populates_secondary_search_matches() {
        MarkoffDocument doc(1);
        doc.resetContent("foo bar foo baz foo", Origin::TestFixture);

        Session *sess = doc.createSession();
        SearchEngine s;
        const int n = s.findAll(&doc, sess, "foo", {});
        QCOMPARE(n, 3);

        int matches = 0;
        for (const Selection &x : sess->secondarySelections())
            if (x.kind == Selection::Kind::SearchMatch) ++matches;
        QCOMPARE(matches, 3);
    }

    void find_all_case_insensitive_default() {
        MarkoffDocument doc(1);
        doc.resetContent("Foo FOO foo", Origin::TestFixture);

        Session *sess = doc.createSession();
        SearchEngine s;
        QCOMPARE(s.findAll(&doc, sess, "foo", {}), 3);
    }

    void find_all_case_sensitive() {
        MarkoffDocument doc(1);
        doc.resetContent("Foo FOO foo", Origin::TestFixture);

        Session *sess = doc.createSession();
        SearchEngine s;
        QCOMPARE(s.findAll(&doc, sess, "foo",
                           SearchEngine::FindFlag::CaseSensitive), 1);
    }

    void find_next_advances_primary_selection() {
        MarkoffDocument doc(1);
        doc.resetContent("ab cd ef", Origin::TestFixture);

        Session *sess = doc.createSession();
        SearchEngine s;
        s.findAll(&doc, sess, "cd", {});
        QVERIFY(s.findNext(&doc, sess));
        const auto p = sess->primarySelection();
        QCOMPARE(doc.resolveTextAnchor(p.anchor), quint32(3));
    }

    void clear_matches_removes_search_kind() {
        MarkoffDocument doc(1);
        doc.resetContent("abc", Origin::TestFixture);

        Session *sess = doc.createSession();
        SearchEngine s;
        s.findAll(&doc, sess, "a", {});
        s.clearMatches(sess);
        for (const Selection &x : sess->secondarySelections())
            QVERIFY(x.kind != Selection::Kind::SearchMatch);
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
