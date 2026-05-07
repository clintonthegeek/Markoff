// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QCoreApplication>

#include <markoff/core/Cmd/Edit.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

using namespace Markoff;

static QByteArray fullText(const MarkoffDocument &doc) {
    QByteArray out;
    for (BlockId id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}

class TstFoundationCmdEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_wrapper_reverts_last_local_edit() {
        MarkoffDocument doc(1);
        doc.applyFlatEdit(0, 0, "ab", Origin::UserEdit);
        QCoreApplication::processEvents();
        Cmd::undo(doc);
        QCoreApplication::processEvents();
        QCOMPARE(fullText(doc), QByteArray());
    }

    void redo_wrapper_reapplies() {
        MarkoffDocument doc(1);
        doc.applyFlatEdit(0, 0, "ab", Origin::UserEdit);
        QCoreApplication::processEvents();
        Cmd::undo(doc);
        QCoreApplication::processEvents();
        Cmd::redo(doc);
        QCoreApplication::processEvents();
        QCOMPARE(fullText(doc), QByteArray("ab"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdEdit)
#include "tst_foundation_cmd_edit.moc"
