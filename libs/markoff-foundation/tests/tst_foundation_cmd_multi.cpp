// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/Cmd.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/Selection.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

class TstFoundationCmdMulti : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void multi_cursor_toggle_bold_each_secondary() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "abc def ghi";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        Selection p; p.anchor = doc.anchorAt(0, Bias::Left);
        p.active = doc.anchorAt(3, Bias::Right); p.kind = Selection::Kind::Primary;
        sess->setPrimarySelection(p);

        Selection s2; s2.anchor = doc.anchorAt(8, Bias::Left);
        s2.active = doc.anchorAt(11, Bias::Right); s2.kind = Selection::Kind::Secondary;
        sess->addSecondarySelection(s2);

        Cmd::applyToAllPrimaryAndSecondaries(doc, *sess, &Cmd::editsForToggleBold);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("**abc** def **ghi**"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdMulti)
#include "tst_foundation_cmd_multi.moc"
