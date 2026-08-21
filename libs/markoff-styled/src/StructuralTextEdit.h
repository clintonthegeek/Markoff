// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QTextEdit>

#include <markoff/core/ClipboardCodec.h>
#include <markoff/core/SourceTextDocumentBinding.h>  // complete type for QPointer member

class QKeyEvent;
class QMimeData;

namespace Markoff::Styled {

/// QTextEdit that intercepts structural keys (Enter/Backspace/Delete/Tab/
/// Backtab) and undo/redo chords BEFORE Qt's native editing runs, routing
/// them to the SourceTextDocumentBinding / MarkoffDocument. Everything else
/// (typing, navigation, selection) falls through to native QTextEdit. This
/// prevents Qt's native QTextList editing from restructuring list blocks in
/// ways the observe-and-infer binding cannot reverse (queue #8.8).
///
/// Cluster N: clipboard create/insert also routes through ClipboardCodec so
/// Reading-mode Copy emits markdown-faithful multi-flavor mime (not themed
/// Qt HTML) and Paste converts HTML/RTF → markdown into the D2 buffer.
class StructuralTextEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit StructuralTextEdit(QWidget *parent = nullptr);
    void setBinding(Markoff::SourceTextDocumentBinding *b) { m_binding = b; }

    QByteArray selectedMarkdown() const;
    void copyWithFlavor(Markoff::ClipboardCodec::Flavor flavor);
    void pasteWithMode(Markoff::ClipboardCodec::PasteMode mode);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    QMimeData *createMimeDataFromSelection() const override;
    void insertFromMimeData(const QMimeData *source) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;

private:
    // QPointer so a teardown-order race (the sibling binding destroyed before
    // this widget) leaves m_binding null rather than dangling; the use-site
    // `if (m_binding)` guard then stays safe.
    QPointer<Markoff::SourceTextDocumentBinding> m_binding;
};

}  // namespace Markoff::Styled
