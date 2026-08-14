// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#include <markoff/canvas/CanvasActionController.h>

#include <QKeySequence>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/canvas/View.h>

namespace Markoff::Canvas {

CanvasActionController::CanvasActionController(QObject *parent)
    : QObject(parent)
{
    setupActions();
    setupTableActions();
}

void CanvasActionController::setupActions()
{
    m_bold       = new QAction(tr("Bold"),          this);
    m_italic     = new QAction(tr("Italic"),        this);
    m_strike     = new QAction(tr("Strikethrough"), this);
    m_inlineCode = new QAction(tr("Inline Code"),   this);
    m_link       = new QAction(tr("Link"),          this);
    m_heading[0] = new QAction(tr("Paragraph"),     this);
    m_heading[1] = new QAction(tr("Heading 1"),     this);
    m_heading[2] = new QAction(tr("Heading 2"),     this);
    m_heading[3] = new QAction(tr("Heading 3"),     this);
    m_heading[4] = new QAction(tr("Heading 4"),     this);
    m_heading[5] = new QAction(tr("Heading 5"),     this);
    m_heading[6] = new QAction(tr("Heading 6"),     this);

    // Same shortcut set as LiveActionController (mirror, not shared code —
    // Corbomite overrides these with its own KF6 KActions anyway; the
    // defaults here are for the standalone demo harness).
    m_bold->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    m_italic->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    m_strike->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    m_inlineCode->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_link->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    for (int lvl = 0; lvl <= 6; ++lvl) {
        m_heading[lvl]->setShortcut(
            QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + lvl)));
    }

    // Initial enabled state: disabled until a view+document are wired.
    for (auto *a : {m_bold, m_italic, m_strike, m_inlineCode, m_link})
        a->setEnabled(false);
    for (int lvl = 0; lvl <= 6; ++lvl) m_heading[lvl]->setEnabled(false);

    // Triggers call straight through to the composed View's own verb —
    // View::toggleBold/../setHeadingLevel are themselves thin drivers over
    // core FormatOps's per-block overloads (P4.3); this class owns no
    // format logic of its own.
    connect(m_bold, &QAction::triggered, this, [this] {
        if (m_view) m_view->toggleBold();
    });
    connect(m_italic, &QAction::triggered, this, [this] {
        if (m_view) m_view->toggleItalic();
    });
    connect(m_strike, &QAction::triggered, this, [this] {
        if (m_view) m_view->toggleStrikethrough();
    });
    connect(m_inlineCode, &QAction::triggered, this, [this] {
        if (m_view) m_view->toggleInlineCode();
    });
    connect(m_link, &QAction::triggered, this, [this] {
        if (m_view) m_view->insertLink();
    });
    for (int lvl = 0; lvl <= 6; ++lvl) {
        connect(m_heading[lvl], &QAction::triggered, this, [this, lvl] {
            if (m_view) m_view->setHeadingLevel(lvl);
        });
    }
}

void CanvasActionController::setupTableActions()
{
    m_insertTable       = new QAction(tr("Insert Table"),        this);
    m_insertRowAbove    = new QAction(tr("Insert Row Above"),    this);
    m_insertRowBelow    = new QAction(tr("Insert Row Below"),    this);
    m_deleteRow         = new QAction(tr("Delete Row"),          this);
    m_insertColumnLeft  = new QAction(tr("Insert Column Left"),  this);
    m_insertColumnRight = new QAction(tr("Insert Column Right"), this);
    m_deleteColumn      = new QAction(tr("Delete Column"),       this);

    m_alignGroup = new QActionGroup(this);
    m_alignGroup->setExclusive(true);
    m_alignColumn[int(TableAlign::None)]   = new QAction(tr("No Alignment"), this);
    m_alignColumn[int(TableAlign::Left)]   = new QAction(tr("Align Left"),   this);
    m_alignColumn[int(TableAlign::Center)] = new QAction(tr("Align Center"), this);
    m_alignColumn[int(TableAlign::Right)]  = new QAction(tr("Align Right"),  this);
    for (QAction *a : m_alignColumn) {
        a->setCheckable(true);
        m_alignGroup->addAction(a);
    }

    for (auto *a : {m_insertTable, m_insertRowAbove, m_insertRowBelow, m_deleteRow,
                    m_insertColumnLeft, m_insertColumnRight, m_deleteColumn})
        a->setEnabled(false);
    for (QAction *a : m_alignColumn)
        a->setEnabled(false);

    // Triggers call straight through to the composed View's own verb, same
    // "no format logic of its own" shape as setupActions()'s connections.
    connect(m_insertTable, &QAction::triggered, this, [this] {
        if (m_view) m_view->insertTable();
    });
    connect(m_insertRowAbove, &QAction::triggered, this, [this] {
        if (m_view) m_view->insertTableRowAbove();
    });
    connect(m_insertRowBelow, &QAction::triggered, this, [this] {
        if (m_view) m_view->insertTableRowBelow();
    });
    connect(m_deleteRow, &QAction::triggered, this, [this] {
        if (m_view) m_view->deleteTableRow();
    });
    connect(m_insertColumnLeft, &QAction::triggered, this, [this] {
        if (m_view) m_view->insertTableColumnLeft();
    });
    connect(m_insertColumnRight, &QAction::triggered, this, [this] {
        if (m_view) m_view->insertTableColumnRight();
    });
    connect(m_deleteColumn, &QAction::triggered, this, [this] {
        if (m_view) m_view->deleteTableColumn();
    });
    for (int i = 0; i < 4; ++i) {
        const TableAlign align = static_cast<TableAlign>(i);
        connect(m_alignColumn[i], &QAction::triggered, this, [this, align] {
            if (m_view) m_view->setTableColumnAlignment(align);
        });
    }
}

void CanvasActionController::setView(View *view)
{
    if (m_view)
        disconnect(m_view, nullptr, this, nullptr);
    m_view = view;
    if (m_view) {
        connect(m_view, &View::caretChanged,
                this, &CanvasActionController::updateEnabledStates);
        connect(m_view, &View::readOnlyChanged,
                this, &CanvasActionController::updateEnabledStates);
    }
    updateEnabledStates();
}

void CanvasActionController::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = doc;
    if (m_document) {
        connect(m_document, &Markoff::MarkoffDocument::d2DocumentChanged,
                this, &CanvasActionController::updateEnabledStates);
    }
    updateEnabledStates();
}

void CanvasActionController::updateEnabledStates()
{
    // hasDoc mirrors LiveActionController's own gate: a document attached
    // to the view we're wired to (not just `m_document`'s presence — the
    // two are kept in step by whoever owns this controller, but the view
    // is the actual mutation surface every trigger above calls into).
    const bool hasDoc = m_view && m_view->document() != nullptr;
    const bool ro     = !m_view || m_view->isReadOnly();
    const bool enabled = hasDoc && !ro;

    for (auto *a : {m_bold, m_italic, m_strike, m_inlineCode, m_link})
        a->setEnabled(enabled);
    for (int lvl = 0; lvl <= 6; ++lvl) m_heading[lvl]->setEnabled(enabled);

    // Table actions (P5.2). InsertTable works from outside a table too —
    // it's enabled/disabled exactly like the format verbs. Every other
    // table action needs the caret actually sitting in a table cell.
    m_insertTable->setEnabled(enabled);

    const auto ctx = (enabled && m_view) ? m_view->caretTableContext() : std::nullopt;
    const bool inTable = ctx.has_value();

    m_insertRowAbove->setEnabled(inTable && ctx->row > 0);
    m_insertRowBelow->setEnabled(inTable);
    m_deleteRow->setEnabled(inTable && ctx->row > 0);
    m_insertColumnLeft->setEnabled(inTable);
    m_insertColumnRight->setEnabled(inTable);
    m_deleteColumn->setEnabled(inTable && ctx->colCount > 1);

    for (QAction *a : m_alignColumn)
        a->setEnabled(inTable);
    // Reflect the caret column's current alignment in the checked state
    // (radio-button group) rather than leaving whatever was checked before
    // the caret moved — this is display sync, not a document write, so it
    // does not go through setChecked's own toggled() signal semantics for
    // triggering anything.
    if (inTable)
        m_alignColumn[int(ctx->columnAlign)]->setChecked(true);
}

}  // namespace Markoff::Canvas
