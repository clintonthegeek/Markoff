// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/SearchEngine.h>
#include <markoff-foundation/Session.h>

using namespace Markoff;

class TstFoundationSearchEngine : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void find_all_populates_secondary_search_matches() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "foo bar foo baz foo";
        ed << i;
        doc.applyLocalEdit(ed);

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
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "Foo FOO foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        QCOMPARE(s.findAll(&doc, sess, "foo", {}), 3);
    }

    void find_all_case_sensitive() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "Foo FOO foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        QCOMPARE(s.findAll(&doc, sess, "foo",
                           SearchEngine::FindFlag::CaseSensitive), 1);
    }
};

QTEST_APPLESS_MAIN(TstFoundationSearchEngine)
#include "tst_foundation_search_engine.moc"
