// SPDX-License-Identifier: GPL-3.0-or-later
#include "StructuralTextEdit.h"

#include <QKeyEvent>
#include <QTextCursor>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff::Styled {

StructuralTextEdit::StructuralTextEdit(QWidget *parent) : QTextEdit(parent) {}

void StructuralTextEdit::keyPressEvent(QKeyEvent *e) {
    if (m_binding) {
        const int key = e->key();
        const auto mods = e->modifiers();

        // Undo/redo: styled disables QTextDocument's own undo stack, so route
        // to the foundation's D2 undo.
        if ((mods & Qt::ControlModifier) && !(mods & Qt::AltModifier)) {
            Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
            // If doc is null the binding isn't fully wired yet; the undo/redo
            // branches below are skipped and the chord falls through to native,
            // which is a no-op (the QTextDocument's own undo stack is disabled).
            if (doc && key == Qt::Key_Z && !(mods & Qt::ShiftModifier)) {
                doc->undoD2(); e->accept(); return;
            }
            if (doc && (key == Qt::Key_Y
                        || (key == Qt::Key_Z && (mods & Qt::ShiftModifier)))) {
                doc->redoD2(); e->accept(); return;
            }
        }

        // Structural keys → forward to the binding before native editing runs.
        const bool structural =
            key == Qt::Key_Return || key == Qt::Key_Enter
            || key == Qt::Key_Backspace || key == Qt::Key_Delete
            || key == Qt::Key_Tab || key == Qt::Key_Backtab;
        if (structural) {
            const QTextCursor c = textCursor();
            if (m_binding->handleStructuralKey(
                    key, static_cast<int>(mods), c.position(), c.anchor())) {
                e->accept();
                return;
            }
        }
    }
    QTextEdit::keyPressEvent(e);
}

}  // namespace Markoff::Styled
