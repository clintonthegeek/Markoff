// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTextEdit>

namespace Markoff { class SourceTextDocumentBinding; }

namespace Markoff::Styled {

/// QTextEdit that intercepts structural keys (Enter/Backspace/Delete/Tab/
/// Backtab) and undo/redo chords BEFORE Qt's native editing runs, routing
/// them to the SourceTextDocumentBinding / MarkoffDocument. Everything else
/// (typing, navigation, selection) falls through to native QTextEdit. This
/// prevents Qt's native QTextList editing from restructuring list blocks in
/// ways the observe-and-infer binding cannot reverse (queue #8.8).
class StructuralTextEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit StructuralTextEdit(QWidget *parent = nullptr);
    void setBinding(Markoff::SourceTextDocumentBinding *b) { m_binding = b; }

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    Markoff::SourceTextDocumentBinding *m_binding = nullptr;
};

}  // namespace Markoff::Styled
