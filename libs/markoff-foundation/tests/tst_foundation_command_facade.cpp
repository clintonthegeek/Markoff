// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/CommandFacade.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

using namespace Markoff;

class TstFoundationCommandFacade : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void toggle_bold_via_facade() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "hello";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        Selection p; p.anchor = doc.textAnchorAt(0, /*rightBias*/ false);
        p.active = doc.textAnchorAt(5, /*rightBias*/ true);
        sess->setPrimarySelection(p);

        CommandFacade facade;
        facade.setDocument(&doc);
        facade.setSession(sess);
        facade.toggleBold();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("**hello**"));
    }

    void undo_via_facade() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "x";
        ed << i;
        doc.applyLocalEdit(ed);

        CommandFacade facade;
        facade.setDocument(&doc);
        facade.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }
};

QTEST_APPLESS_MAIN(TstFoundationCommandFacade)
#include "tst_foundation_command_facade.moc"
