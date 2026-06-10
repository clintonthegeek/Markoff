// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

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
};

QTEST_MAIN(TstViewContractSource)
#include "tst_view_contract_source.moc"
