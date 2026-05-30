// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QTextEdit>

#include <markoff/core/SourceTextDocumentBinding.h>  // complete type for QPointer member

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
    // QPointer so a teardown-order race (the sibling binding destroyed before
    // this widget) leaves m_binding null rather than dangling; the use-site
    // `if (m_binding)` guard then stays safe.
    QPointer<Markoff::SourceTextDocumentBinding> m_binding;
};

}  // namespace Markoff::Styled
