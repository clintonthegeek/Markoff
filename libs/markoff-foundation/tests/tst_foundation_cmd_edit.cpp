// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/Cmd/Edit.h>
#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

class TstFoundationCmdEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_wrapper_reverts_last_local_edit() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "ab";
        ed << i;
        doc.applyLocalEdit(ed);
        Cmd::undo(doc);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }

    void redo_wrapper_reapplies() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "ab";
        ed << i;
        doc.applyLocalEdit(ed);
        Cmd::undo(doc);
        Cmd::redo(doc);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("ab"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdEdit)
#include "tst_foundation_cmd_edit.moc"
