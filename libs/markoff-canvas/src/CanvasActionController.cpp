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
}

}  // namespace Markoff::Canvas
