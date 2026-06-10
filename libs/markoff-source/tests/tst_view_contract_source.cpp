// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

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
};

QTEST_MAIN(TstViewContractSource)
#include "tst_view_contract_source.moc"
