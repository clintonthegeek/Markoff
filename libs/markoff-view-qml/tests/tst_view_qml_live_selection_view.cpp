// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/TextAnchor.h>
#include <markoff/view/qml/LiveSelectionView.h>

using namespace Markoff::View::Qml;

/// Build a two-paragraph document: "Hello world\n\nSecond para" and wait
/// for a parse so blockAnchorAt() returns valid anchors.
static void seedDoc(Markoff::MarkoffDocument &doc)
{
    Markoff::MarkoffEdit ed;
    ed.oldStart = 0; ed.oldEnd = 0;
    ed.newText  = QByteArray("Hello world\n\nSecond para");
    doc.applyLocalEdit({ ed });
}

class TstLiveSelectionView : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void selectionChanged_fires_when_session_primary_selection_changes()
    {
        Markoff::MarkoffDocument doc(1);
        seedDoc(doc);
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        QSignalSpy spy(&view, &LiveSelectionView::selectionChanged);

        // Set a non-degenerate primary selection on the session.
        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(0, false);
        sel.active = doc.textAnchorAt(5, true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        QCOMPARE(spy.count(), 1);
    }

    void hasSelection_reflects_session_primary_selection()
    {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("Hello world");
        doc.applyLocalEdit({ ed });
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        QVERIFY(!view.hasSelection());

        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(0, false);
        sel.active = doc.textAnchorAt(5, true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        QVERIFY(view.hasSelection());
    }

    void rangeForBlock_returns_minus_one_for_block_outside_selection()
    {
        Markoff::MarkoffDocument doc(1);
        seedDoc(doc);
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        // Wait for parse so blockAnchorAt(0) works.
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        if (doc.blockAnchorAt(0) == std::nullopt)
            parseSpy.wait(2000);

        // Select only within block 0 (indices 0..4 = "Hello").
        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(0, false);
        sel.active = doc.textAnchorAt(5, true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        // Block 1 should not be in selection.
        const QPoint r = view.rangeForBlock(1);
        QCOMPARE(r.x(), -1);
        QCOMPARE(r.y(), -1);
    }

    void rangeForBlock_returns_correct_range_for_single_block_selection()
    {
        Markoff::MarkoffDocument doc(1);
        seedDoc(doc);
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        if (doc.blockAnchorAt(0) == std::nullopt)
            parseSpy.wait(2000);

        // "Hello world\n\nSecond para" — block 0 is "Hello world" (11 bytes)
        // Select bytes 0..5 (= "Hello") within block 0.
        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(0, false);
        sel.active = doc.textAnchorAt(5, true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        const QPoint r = view.rangeForBlock(0);
        QVERIFY(r.x() >= 0);
        QVERIFY(r.y() > r.x());
        QCOMPARE(r.x(), 0);
        QCOMPARE(r.y(), 5);
    }

    void rangeForBlock_returns_INT32_MAX_for_first_block_in_cross_block_selection()
    {
        Markoff::MarkoffDocument doc(1);
        seedDoc(doc);
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        if (doc.blockAnchorAt(0) == std::nullopt)
            parseSpy.wait(2000);

        // "Hello world\n\nSecond para" — select from byte 3 through end of doc
        // so both blocks are in the selection.
        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(3, false);
        sel.active = doc.textAnchorAt(static_cast<quint32>(doc.visibleLength()), true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        const QPoint r0 = view.rangeForBlock(0);
        QCOMPARE(r0.x(), 3);
        QCOMPARE(r0.y(), INT32_MAX);

        const QPoint r1 = view.rangeForBlock(1);
        QCOMPARE(r1.x(), 0);
        QVERIFY(r1.y() > 0);
    }

    void clearSelection_makes_hasSelection_false()
    {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("Hello world");
        doc.applyLocalEdit({ ed });
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(0, false);
        sel.active = doc.textAnchorAt(5, true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);
        QVERIFY(view.hasSelection());

        QSignalSpy spy(&view, &LiveSelectionView::selectionChanged);
        view.clear();

        QVERIFY(!view.hasSelection());
        QCOMPARE(spy.count(), 1);
    }

    void begin_and_extend_set_session_primary_selection()
    {
        Markoff::MarkoffDocument doc(1);
        seedDoc(doc);
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        if (doc.blockAnchorAt(0) == std::nullopt)
            parseSpy.wait(2000);

        view.begin(0, 0);
        QVERIFY(!view.hasSelection());  // degenerate

        view.extend(0, 5);
        QVERIFY(view.hasSelection());

        // Session should have a non-degenerate primary selection now.
        const Markoff::Selection sess_sel = session->primarySelection();
        QVERIFY(sess_sel.anchor != sess_sel.active);
    }

    void setSession_null_clears_subscription()
    {
        Markoff::MarkoffDocument doc(1);
        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("Hello");
        doc.applyLocalEdit({ ed });
        Markoff::Session *session = doc.createSession();

        LiveSelectionView view;
        view.setDocument(&doc);
        view.setSession(session);

        // Disconnect — changes to session should no longer propagate.
        view.setSession(nullptr);

        QSignalSpy spy(&view, &LiveSelectionView::selectionChanged);

        Markoff::Selection sel;
        sel.anchor = doc.textAnchorAt(0, false);
        sel.active = doc.textAnchorAt(3, true);
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TstLiveSelectionView)
#include "tst_view_qml_live_selection_view.moc"
