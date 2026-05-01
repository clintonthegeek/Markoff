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

    /// Atomically set the TextEdit's plain text while holding the cycle guard.
    /// This is the preferred path for model-driven text updates: the underlying
    /// QTextDocument::setPlainText() fires contentsChange synchronously, so
    /// the guard is active for the entire duration. Calling beginModelUpdate /
    /// textEdit.text = x / endModelUpdate from QML does NOT work reliably
    /// because Qt Quick defers the TextEdit text update past the guard window.
    Q_INVOKABLE void setModelText(const QString &text);

    /// Set before assigning model-driven text to the TextEdit; prevents
    /// the resulting contentsChange from being forwarded to MarkoffDocument.
    /// Prefer setModelText() over begin/end when possible.
    Q_INVOKABLE void beginModelUpdate();

    /// Clear after the model-driven text assignment is complete.
    Q_INVOKABLE void endModelUpdate();

Q_SIGNALS:
    void documentChanged();
    void blockAnchorChanged();
    void textDocumentChanged();

    /// Emitted after an edit is applied to the CRDT document.
    /// `postText` is the block's current text from the QTextDocument (post-edit).
    void editApplied(const Markoff::BlockAnchor &blockAnchor, const QString &postText);

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
