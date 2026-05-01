// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQuickTextDocument>
#include <QString>
#include <qqmlintegration.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::View::Qml {

/// Per-delegate cycle-guarded write path for the live editor.
///
/// Connects to `QTextDocument::contentsChange` on the delegate's TextEdit.
/// On user-driven changes, translates the QTextDocument position (UTF-16) to
/// a document-global UTF-8 byte offset and calls `MarkoffDocument::applyLocalEdit`.
///
/// The cycle guard (`m_applyingModelUpdate`) prevents the model-driven text
/// update path from looping back into `applyLocalEdit`: QML calls
/// `beginModelUpdate()` before assigning new text to the TextEdit and
/// `endModelUpdate()` after; any `contentsChange` fired in that window is
/// suppressed.
class LiveEditBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document
               WRITE setDocument
               NOTIFY documentChanged)
    Q_PROPERTY(Markoff::BlockAnchor blockAnchor
               READ blockAnchor
               WRITE setBlockAnchor
               NOTIFY blockAnchorChanged)
    Q_PROPERTY(QQuickTextDocument *textDocument
               READ textDocument
               WRITE setTextDocument
               NOTIFY textDocumentChanged)

public:
    explicit LiveEditBinding(QObject *parent = nullptr);
    ~LiveEditBinding() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *doc);

    Markoff::BlockAnchor blockAnchor() const;
    void setBlockAnchor(const Markoff::BlockAnchor &anchor);

    QQuickTextDocument *textDocument() const;
    void setTextDocument(QQuickTextDocument *qtd);

    /// Set before assigning model-driven text to the TextEdit; prevents
    /// the resulting contentsChange from being forwarded to MarkoffDocument.
    Q_INVOKABLE void beginModelUpdate();

    /// Clear after the model-driven text assignment is complete.
    Q_INVOKABLE void endModelUpdate();

Q_SIGNALS:
    void documentChanged();
    void blockAnchorChanged();
    void textDocumentChanged();

private Q_SLOTS:
    void onContentsChange(int qtPos, int charsRemoved, int charsAdded);

private:
    void rewireTextDocument();

    Markoff::MarkoffDocument *m_document    = nullptr;
    Markoff::BlockAnchor      m_blockAnchor = {};
    QQuickTextDocument       *m_quickDoc    = nullptr;
    QTextDocument            *m_textDoc     = nullptr;

    bool m_applyingModelUpdate = false;
};

}  // namespace Markoff::View::Qml
