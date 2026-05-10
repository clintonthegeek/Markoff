// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveActionController.h>

#include <QApplication>
#include <QClipboard>
#include <QKeySequence>
#include <QMimeData>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveClipboardController.h>
#include <markoff/live/LiveFormatController.h>
#include <markoff/live/LiveListModelBinding.h>

namespace Markoff::Live {

LiveActionController::LiveActionController(QObject *parent)
    : QObject(parent)
{
    setupActions();
    connect(QApplication::clipboard(), &QClipboard::changed,
            this, &LiveActionController::onClipboardChanged);
}

void LiveActionController::setupActions() {
    m_cut       = new QAction(tr("Cut"),        this);
    m_copy      = new QAction(tr("Copy"),       this);
    m_paste     = new QAction(tr("Paste"),      this);
    m_selectAll = new QAction(tr("Select All"), this);
    m_delete    = new QAction(tr("Delete"),     this);
    m_undo      = new QAction(tr("Undo"),       this);
    m_redo      = new QAction(tr("Redo"),       this);
    m_bold      = new QAction(tr("Bold"),       this);
    m_italic    = new QAction(tr("Italic"),     this);
    m_link      = new QAction(tr("Link"),       this);
    m_save      = new QAction(tr("Save"),       this);
    m_zoomIn    = new QAction(tr("Zoom In"),    this);
    m_zoomOut   = new QAction(tr("Zoom Out"),   this);
    m_zoomReset = new QAction(tr("Reset Zoom"), this);

    m_cut->setShortcut(QKeySequence::Cut);
    m_copy->setShortcut(QKeySequence::Copy);
    m_paste->setShortcut(QKeySequence::Paste);
    m_selectAll->setShortcut(QKeySequence::SelectAll);
    m_undo->setShortcut(QKeySequence::Undo);
    m_redo->setShortcut(QKeySequence::Redo);
    m_bold->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    m_italic->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    m_link->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    m_save->setShortcut(QKeySequence::Save);
    m_delete->setShortcut(QKeySequence::Delete);
    m_zoomIn->setShortcuts({
        QKeySequence(Qt::CTRL | Qt::Key_Equal),
        QKeySequence(Qt::CTRL | Qt::Key_Plus),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
    });
    m_zoomOut  ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    m_zoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));

    // Initial enabled state (all disabled until document+selection wired).
    for (auto *a : {m_cut, m_copy, m_paste, m_selectAll, m_delete,
                    m_undo, m_redo, m_bold, m_italic, m_link, m_save,
                    m_zoomIn, m_zoomOut, m_zoomReset})
        a->setEnabled(false);

    // Wire triggers.
    connect(m_selectAll, &QAction::triggered, this, [this] {
        if (m_selection) m_selection->selectAll();
    });
    connect(m_delete, &QAction::triggered, this, [this] {
        if (m_selection) m_selection->deleteSelection();
    });
    connect(m_save, &QAction::triggered, this, [this] {
        Q_EMIT saveRequested();
    });
    connect(m_undo, &QAction::triggered, this, [this] {
        if (m_document) m_document->undoD2();
    });
    connect(m_redo, &QAction::triggered, this, [this] {
        if (m_document) m_document->redoD2();
    });
    connect(m_zoomIn, &QAction::triggered, this, [this]{
        if (m_binding) m_binding->setFontScale(m_binding->fontScale() * kFontScaleStep);
    });
    connect(m_zoomOut, &QAction::triggered, this, [this]{
        if (m_binding) m_binding->setFontScale(m_binding->fontScale() / kFontScaleStep);
    });
    connect(m_zoomReset, &QAction::triggered, this, [this]{
        if (m_binding) m_binding->setFontScale(kDefaultFontScale);
    });
    // cut/copy/paste wired after setClipboardController.
    // bold/italic/link wired after setFormatController.
}

void LiveActionController::setDocument(Markoff::MarkoffDocument *doc) {
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = doc;
    if (m_document) {
        connect(m_document, &Markoff::MarkoffDocument::d2DocumentChanged,
                this, &LiveActionController::updateEnabledStates);
    }
    updateEnabledStates();
}

void LiveActionController::setSelectionView(LiveSelectionView *sv) {
    if (m_selection) {
        disconnect(m_selection, nullptr, this, nullptr);
    }
    m_selection = sv;
    if (m_selection) {
        connect(m_selection, &LiveSelectionView::selectionChanged,
                this, &LiveActionController::updateEnabledStates);
    }
    updateEnabledStates();
}

void LiveActionController::setClipboardController(LiveClipboardController *cc) {
    m_clipboard = cc;
    if (cc) {
        connect(m_cut,   &QAction::triggered, cc, &LiveClipboardController::cut);
        connect(m_copy,  &QAction::triggered, cc, &LiveClipboardController::copy);
        connect(m_paste, &QAction::triggered, cc, &LiveClipboardController::paste);
    }
    updateEnabledStates();
}

void LiveActionController::updateEnabledStates() {
    const bool hasSel = m_selection && m_selection->hasSelection();
    const bool hasDoc = m_document != nullptr;
    const bool hasClip = QApplication::clipboard()->mimeData() &&
                         (QApplication::clipboard()->mimeData()->hasText() ||
                          QApplication::clipboard()->mimeData()->hasFormat(
                              LiveClipboardController::kBlocksMime));

    m_cut->setEnabled(hasSel && hasDoc);
    m_copy->setEnabled(hasSel && hasDoc);
    m_paste->setEnabled(hasDoc && hasClip);
    m_selectAll->setEnabled(hasDoc);
    m_delete->setEnabled(hasSel && hasDoc);
    m_save->setEnabled(hasDoc);

    // Undo/redo: enabled whenever a document is wired; no per-D2 depth query.
    m_undo->setEnabled(hasDoc);
    m_redo->setEnabled(hasDoc);

    // Bold/italic/link: enabled when selection exists (format controller not wired yet).
    m_bold->setEnabled(hasSel && hasDoc);
    m_italic->setEnabled(hasSel && hasDoc);
    m_link->setEnabled(hasDoc);  // link allows empty selection (placeholder)

    const bool hasBinding = m_binding != nullptr;
    m_zoomIn   ->setEnabled(hasBinding);
    m_zoomOut  ->setEnabled(hasBinding);
    m_zoomReset->setEnabled(hasBinding);
}

void LiveActionController::setBinding(LiveListModelBinding *b) {
    m_binding = b;
    updateEnabledStates();
}

void LiveActionController::setFormatController(LiveFormatController *fc) {
    m_format = fc;
    if (fc) {
        connect(m_bold,   &QAction::triggered, fc, &LiveFormatController::toggleBold);
        connect(m_italic, &QAction::triggered, fc, &LiveFormatController::toggleItalic);
        connect(m_link,   &QAction::triggered, fc, &LiveFormatController::insertLink);
    }
}

void LiveActionController::onClipboardChanged() {
    updateEnabledStates();
}

}  // namespace Markoff::Live
