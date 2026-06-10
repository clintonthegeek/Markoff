// SPDX-License-Identifier: GPL-3.0-or-later
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextTable>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/styled/Editor.h>

#include "../../markoff-core/tests/ViewContractChecks.h"
#include "support/TableTestHelpers.h"

class TstViewContractStyled : public QObject {
    Q_OBJECT
    Markoff::MarkoffDocument *m_doc = nullptr;
    Markoff::Styled::Editor  *m_ed  = nullptr;
private Q_SLOTS:
    void init() {
        m_doc = new Markoff::MarkoffDocument(1);
        m_doc->loadFromMarkdown(ViewContract::fixture());
        m_ed  = new Markoff::Styled::Editor;
        m_ed->setDocument(m_doc);
        QTest::qWait(50);
    }
    void cleanup() { delete m_ed; delete m_doc; }

    void cursor_round_trip()            { ViewContract::checkCursorRoundTrip(m_ed); }
    void read_only_blocks()             { ViewContract::checkReadOnlyBlocksUndoAndKeepsBytes(m_ed, m_doc); }
    void undo_redo_via_base()           { ViewContract::checkUndoRedoViaBase(m_ed, m_doc); }
    void font_scale_signal()            { ViewContract::checkFontScaleSignal(m_ed); }
    void context_changed_kind_gated()   { ViewContract::checkContextChangedKindGated(m_ed); }

    void format_verbs_match_source_semantics() {
        auto *te = m_ed->textEdit();
        QVERIFY(te);
        // Coordinate-space probe (Task 6 precondition): with no table frame
        // in the document, the styled QTextEdit's plain text IS
        // widgetFlatView, so cursor qt-positions are flat-view positions.
        QCOMPARE(te->toPlainText(),
                 QString::fromUtf8(m_doc->widgetFlatView()));
        // Select "one" in block 0 ("alpha one" — qt positions 6..9) and
        // bold it via the BASE pointer.
        QTextCursor c = te->textCursor();
        c.setPosition(6);
        c.setPosition(9, QTextCursor::KeepAnchor);
        te->setTextCursor(c);
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QVERIFY(m_doc->serializeForSave().startsWith("alpha **one**"));
        // The wrapper re-applies FormatOps' returned range, so the selection
        // now covers "one" inside the markers (qt 8..11) — same post-toggle
        // contract as the source leaf (FormatOps::wrapToggle Wrap mode shifts
        // the selection by delimLen). A second toggle therefore unwraps via
        // the surrounded-outside path without manual re-selection.
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QVERIFY(m_doc->serializeForSave().startsWith("alpha one"));
    }

    void format_verbs_noop_inside_table_frame() {
        // Table fixture: caret inside the rendered QTextTable frame; verbs
        // must not mutate (the frame's character stream is not the block's
        // flat bytes — coordinate space is untrustworthy there).
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("| A | B |\n| --- | --- |\n| x | y |\n\ntail");
        Markoff::Styled::Editor ed;
        ed.setDocument(&doc);
        QTest::qWait(50);
        auto *te = ed.textEdit();
        QVERIFY(te);
        QTextTable *t = firstTable(te->document());
        QVERIFY(t);
        te->setTextCursor(t->cellAt(0, 0).firstCursorPosition());
        const QByteArray before = doc.serializeForSave();
        static_cast<Markoff::MarkdownView *>(&ed)->toggleBold();
        static_cast<Markoff::MarkdownView *>(&ed)->setHeadingLevel(2);
        QCOMPARE(doc.serializeForSave(), before);

        // Content AFTER the frame is also guarded (positions diverge from
        // widgetFlatView once a frame compresses the document).
        QTextCursor after(te->document());
        after.movePosition(QTextCursor::End);
        te->setTextCursor(after);
        static_cast<Markoff::MarkdownView *>(&ed)->toggleBold();
        QCOMPARE(doc.serializeForSave(), before);
    }

    void format_verbs_respect_read_only() {
        const QByteArray before = m_doc->serializeForSave();
        auto *te = m_ed->textEdit();
        QVERIFY(te);
        QTextCursor c = te->textCursor();
        c.setPosition(0);
        c.setPosition(5, QTextCursor::KeepAnchor);
        te->setTextCursor(c);
        m_ed->setReadOnly(true);
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QCOMPARE(m_doc->serializeForSave(), before);
        m_ed->setReadOnly(false);
    }

    // ---- Task 11(B): scrollPositionChanged fires on programmatic scroll ----
    //
    // setScrollPositionVisualLine(pos) must emit scrollPositionChanged.
    // The exact emitted value may differ from pos when the document fits
    // entirely in the viewport (scrollbar clamped to 0); the contract only
    // requires that the signal fires at least once.
    void scrollPositionChanged_fires_on_set() {
        QSignalSpy spy(m_ed,
                       &Markoff::MarkdownView::scrollPositionChanged);
        QVERIFY(spy.isValid());
        m_ed->setScrollPositionVisualLine(0.5f);
        QTRY_VERIFY(spy.count() >= 1);
    }

    // ---- Task 11(B) GAP 1: scrollPositionChanged fires on NATIVE scroll ----
    //
    // Spec §9 requires the signal on the native change signal (user drag).
    // The native path for QTextEdit is verticalScrollBar()->valueChanged.
    // Load a tall document so the scrollbar has range, then drive setValue
    // directly — this IS the native valueChanged path — and verify the signal
    // fires. The spec §9 connection in the constructor routes valueChanged to
    // emit scrollPositionChanged unconditionally.
    void scrollPositionChanged_fires_on_native_scroll() {
        // Use a fresh editor + tall document so the scrollbar has range.
        Markoff::MarkoffDocument doc(2);
        // 30 paragraphs is tall enough for any reasonable default font.
        QByteArray md;
        for (int i = 0; i < 30; ++i)
            md += QByteArrayLiteral("Paragraph line ") + QByteArray::number(i) + "\n\n";
        doc.loadFromMarkdown(md);

        Markoff::Styled::Editor ed;
        ed.resize(400, 200);
        ed.setDocument(&doc);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));
        QTest::qWait(50);

        auto *te = ed.textEdit();
        QVERIFY(te);
        auto *sb = te->verticalScrollBar();
        QVERIFY(sb);
        if (sb->maximum() == 0) {
            QSKIP("Scrollbar has no range in this viewport — content too short (offscreen rendering)");
        }

        QSignalSpy spy(static_cast<Markoff::MarkdownView *>(&ed),
                       &Markoff::MarkdownView::scrollPositionChanged);
        QVERIFY(spy.isValid());

        // Drive the native path: setValue fires valueChanged, which is
        // connected in the constructor to emit scrollPositionChanged.
        sb->setValue(sb->maximum() / 2);
        QTRY_VERIFY(spy.count() >= 1);
    }

    // ---- Contract v2: cursorPositionChanged fires on caret move ----
    //
    // The base MarkdownView::cursorPositionChanged(line, column) must fire
    // when the caret moves, so a host can drive an Ln/Col statusbar without
    // reaching into the inner widget (per the leaf's documented contract).
    // Falsifiable: without the constructor connection the spy stays empty.
    // The emitted (line, column) must equal the leaf's own cursorPosition().
    void cursorPositionChanged_fires_on_caret_move() {
        QSignalSpy spy(static_cast<Markoff::MarkdownView *>(m_ed),
                       &Markoff::MarkdownView::cursorPositionChanged);
        QVERIFY(spy.isValid());
        m_ed->setCursorPosition({5, 1});
        QTRY_VERIFY(spy.count() >= 1);
        const auto args = spy.last();
        QCOMPARE(args.at(0).toInt(), m_ed->cursorPosition().line);
        QCOMPARE(args.at(1).toInt(), m_ed->cursorPosition().column);
    }
};

QTEST_MAIN(TstViewContractStyled)
#include "tst_view_contract_styled.moc"
