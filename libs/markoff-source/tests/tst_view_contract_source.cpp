// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

#include "../../markoff-core/tests/ViewContractChecks.h"

class TstViewContractSource : public QObject {
    Q_OBJECT
    Markoff::MarkoffDocument *m_doc = nullptr;
    Markoff::Source::Editor  *m_ed  = nullptr;
private Q_SLOTS:
    void init() {
        m_doc = new Markoff::MarkoffDocument(1);
        m_doc->loadFromMarkdown(ViewContract::fixture());
        m_ed  = new Markoff::Source::Editor;
        m_ed->setDocument(m_doc);
        QTest::qWait(50);
    }
    void cleanup() { delete m_ed; delete m_doc; }

    void cursor_round_trip()            { ViewContract::checkCursorRoundTrip(m_ed); }
    void read_only_blocks()             { ViewContract::checkReadOnlyBlocksUndoAndKeepsBytes(m_ed, m_doc); }
    void undo_redo_via_base()           { ViewContract::checkUndoRedoViaBase(m_ed, m_doc); }
    void font_scale_signal()            { ViewContract::checkFontScaleSignal(m_ed); }
    void context_changed_kind_gated()   { ViewContract::checkContextChangedKindGated(m_ed); }
    void context_changed_on_structural_kind_change_without_caret_move() {
        ViewContract::checkContextChangedOnStructuralKindChangeWithoutCaretMove(m_ed, m_doc);
    }

    // ---- Task 12(A): fontScale actually scales the inner editor font ----
    //
    // Spec §8: Source::Editor::setFontScale must scale the inner
    // QPlainTextEdit's font point size from the captured base size, and
    // return to base when reset to 1.0. The signal test above (font_scale_signal)
    // only proves the base store + signal; this slot proves the actual
    // visual scaling (invariant 4: falsifiable by breaking the override).
    void font_scale_scales_inner_editor_font() {
        auto *te = m_ed->findChild<QPlainTextEdit *>();
        QVERIFY(te);
        const qreal base = te->font().pointSizeF();
        static_cast<Markoff::MarkdownView *>(m_ed)->setFontScale(2.0);
        QVERIFY(qFuzzyCompare(te->font().pointSizeF(), base * 2.0));
        static_cast<Markoff::MarkdownView *>(m_ed)->setFontScale(1.0);
        QVERIFY(qFuzzyCompare(te->font().pointSizeF(), base));
    }

    // ---- Task 12(B) §10 check 4: find — see dedicated test binary ----
    //
    // Spec §10 check 4 (find highlight) is covered by tst_source_find_adapter,
    // which drives attachFindController() + match counts + extraSelection positions
    // against the real production SourceFindAdapter. That binary is the
    // authoritative find contract for this leaf; duplication here would not
    // add falsifiability. Reference: libs/markoff-source/tests/tst_source_find_adapter.cpp.

    // ---- Task 12(B) §10 check 6: format verbs via base pointer -----------
    //
    // Source leaf must expose format verbs through the MarkdownView base pointer
    // (Corbomite's polymorphic handle). The contract is: a toggleBold() call
    // via `MarkdownView *` wraps the selection in the document.
    // Mirrors styled's format_verbs_match_source_semantics.
    void format_verbs_via_base_pointer() {
        auto *te = m_ed->plainTextEdit();
        QVERIFY(te);
        // Select "one" in "alpha one" (block 0, qt-positions 6..9).
        QTextCursor c = te->textCursor();
        c.setPosition(6);
        c.setPosition(9, QTextCursor::KeepAnchor);
        te->setTextCursor(c);
        // Drive via the BASE pointer (the polymorphic handle Corbomite uses).
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QVERIFY(m_doc->serializeForSave().startsWith("alpha **one**"));
        // Second call toggles back (wrapToggle round-trip).
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QVERIFY(m_doc->serializeForSave().startsWith("alpha one"));
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
    // The native path for QPlainTextEdit is verticalScrollBar()->valueChanged.
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

        Markoff::Source::Editor ed;
        ed.resize(400, 200);
        ed.setDocument(&doc);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));
        QTest::qWait(30);

        auto *inner = ed.findChild<QPlainTextEdit *>();
        QVERIFY(inner);
        auto *sb = inner->verticalScrollBar();
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

    // ---- Completion revival Task 2: caretRect() maps inner cursorRect ----
    //
    // The base default returns an invalid QRect; the Source override must map
    // the inner QPlainTextEdit's cursorRect() (viewport coords) into editor-
    // widget coords. Before attach there is no document, so caretRect() must
    // stay invalid; after attach + a caret move it must be valid, inside the
    // editor, and track the cursor downward as the caret moves to a later line.
    void caretRect_validAfterAttach_tracksCursor()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha one.\n\nBeta two.\n\nGamma three.\n"));
        Markoff::Source::Editor ed;
        ed.resize(600, 400);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));

        QVERIFY(!ed.caretRect().isValid());   // before attach: no caret
        ed.setDocument(&doc);
        ed.setCursorPosition({1, 1});
        const QRect r1 = ed.caretRect();
        QVERIFY(r1.isValid());
        QVERIFY(ed.rect().contains(r1.topLeft()));
        ed.setCursorPosition({3, 1});          // a later visual line
        const QRect r3 = ed.caretRect();
        QVERIFY(r3.isValid());
        QVERIFY2(r3.top() > r1.top(), "caretRect must move down with the cursor");
    }
};

QTEST_MAIN(TstViewContractSource)
#include "tst_view_contract_source.moc"
