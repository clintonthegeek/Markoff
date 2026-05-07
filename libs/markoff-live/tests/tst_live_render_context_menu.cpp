// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff::Live;
using namespace Markoff;

class TstContextMenu : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void canUndo_false_before_any_edit() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        Markoff::BlockAnchor anchor = ids[0];
        QVERIFY(!doc.canUndoForBlock(anchor));
    }

    void canUndo_true_after_edit() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto ids = doc.iterateBlocks();
        const Markoff::BlockAnchor anchor = ids[0];
        Cmd::insertCharacter(doc, anchor, 5, QChar('!'));
        QVERIFY(doc.canUndoForBlock(anchor));
    }

    void undoForBlock_reverts_edit() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto ids = doc.iterateBlocks();
        const Markoff::BlockAnchor anchor = ids[0];
        Cmd::insertCharacter(doc, anchor, 5, QChar('!'));
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[0])), QStringLiteral("hello!"));
        doc.undoForBlock(anchor);
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[0])), QStringLiteral("hello"));
    }
};
QTEST_MAIN(TstContextMenu)
#include "tst_live_render_context_menu.moc"
