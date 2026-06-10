// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextCursor>
#include <QTextTable>

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
};

QTEST_MAIN(TstViewContractStyled)
#include "tst_view_contract_styled.moc"
