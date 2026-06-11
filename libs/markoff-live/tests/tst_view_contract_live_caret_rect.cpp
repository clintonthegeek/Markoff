// SPDX-License-Identifier: GPL-3.0-or-later
//
// MarkdownView contract — caretRect() on the live leaf (contract-v2
// extension, 2026-06-11; consumer: Corbomite completion revival).
//
// INVARIANTS note: caretRect is a read-only query over the focused QML
// delegate's TextEdit (window activeFocusItem) — no new cursor authority,
// no stored state (INVARIANTS #3 trivially satisfied). INVARIANTS #5:
// this test drives the REAL path — QQuickWidget scene + initial-focus
// seed + activeFocusItem — not a mock.

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/EditorWidget.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveListModelBinding.h>

#include <QStringList>
#include <QTest>

using namespace Markoff::Live;

namespace {
QString makeParagraphs(int count)
{
    QStringList blocks;
    blocks.reserve(count);
    for (int i = 0; i < count; ++i)
        blocks.append(QStringLiteral("Paragraph %1 line A.").arg(i));
    return blocks.join(QStringLiteral("\n\n"));
}
} // namespace

class TestViewContractLiveCaretRect : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void caretRect_invalidBeforeAttach()
    {
        EditorWidget w;
        w.resize(800, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(!w.caretRect().isValid());
    }

    void caretRect_validAfterSeed_withinBounds_tracksCursor()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(makeParagraphs(8).toUtf8());

        EditorWidget w;
        w.resize(800, 400);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.setDocument(&doc);

        // Wait for the initial-focus seed to establish a TextCaret.
        auto *cs = w.binding()->cursorState();
        QTRY_VERIFY(cs->currentTextCaret().has_value());

        QTRY_VERIFY2(w.caretRect().isValid(),
                     "caretRect stays invalid after a TextCaret is established");
        const QRect r0 = w.caretRect();
        QVERIFY2(w.rect().contains(r0.topLeft()),
                 qPrintable(QStringLiteral("caret %1,%2 outside widget %3x%4")
                                .arg(r0.x()).arg(r0.y())
                                .arg(w.width()).arg(w.height())));

        // Move to a later block; the rect must follow downward.
        w.setCursorPosition({5, 1});
        QTRY_VERIFY(w.caretRect().isValid() && w.caretRect().top() > r0.top());
    }
};

QTEST_MAIN(TestViewContractLiveCaretRect)
#include "tst_view_contract_live_caret_rect.moc"
