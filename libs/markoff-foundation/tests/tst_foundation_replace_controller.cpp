// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/ReplaceController.h>
#include <markoff-foundation/SearchEngine.h>
#include <markoff-foundation/Session.h>

using namespace Markoff;

class TstFoundationReplaceController : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void replace_current_replaces_primary_match_and_advances() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "foo foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine se;
        se.findAll(&doc, sess, "foo", {});
        se.findNext(&doc, sess);  // primary -> first match

        ReplaceController rc;
        QVERIFY(rc.replaceCurrent(&doc, sess, "bar").has_value());
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("bar foo"));
    }

    void replace_all_replaces_every_match() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "foo bar foo baz foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine().findAll(&doc, sess, "foo", {});

        ReplaceController rc;
        const auto r = rc.replaceAll(&doc, sess, "X");
        QCOMPARE(r.count, 3);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("X bar X baz X"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationReplaceController)
#include "tst_foundation_replace_controller.moc"
