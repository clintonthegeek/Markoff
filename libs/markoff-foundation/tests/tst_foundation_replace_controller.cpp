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

    // D2: replaceInBlock replaces bytes within a specific block.
    void replaceInBlock_replacesMatch() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello world\n");

        const auto blocks = doc.iterateBlocks();
        QVERIFY(!blocks.empty());
        const BlockId firstBlockId = blocks.front();

        // Determine the actual block text to get the correct byte layout.
        // "hello world" is the paragraph content; blockText may include a
        // trailing newline depending on how the block was stored.
        const QByteArray original = doc.blockText(firstBlockId);
        QVERIFY(original.startsWith("hello world"));

        // "world" starts at byte offset 6, length 5.
        const bool ok = ReplaceController::replaceInBlock(
            doc, firstBlockId, 6, 5, QByteArray("earth"));
        QVERIFY(ok);

        const QByteArray updated = doc.blockText(firstBlockId);
        QVERIFY(updated.startsWith("hello earth"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationReplaceController)
#include "tst_foundation_replace_controller.moc"
