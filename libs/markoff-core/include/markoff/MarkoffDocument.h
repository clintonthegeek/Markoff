// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <markoff/CanonicalBuffer.h>   // for CursorBias enum — leaves CursorPosition fwd-decl'd
#include <markoff/MarkoffCoreExport.h>

class QUndoStack;

namespace Markoff {

class CanonicalBuffer;
class CursorPosition;
class ParsePool;
class Document;  // markoff-parser

enum class Origin {
    FirstOpen,              // empty stack, no command pushed
    ExternalReloadClean,    // stack cleared
    ExternalReloadResolved, // stack cleared (post-merge-modal, any outcome)
    UserRevertToSaved,      // pushes one mega MarkdownDelta
    TestFixture,            // stack cleared
};

class MARKOFF_CORE_EXPORT MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    explicit MarkoffDocument(QObject *parent = nullptr);
    MarkoffDocument(std::unique_ptr<CanonicalBuffer> buffer,
                    ParsePool *pool = nullptr,
                    QObject *parent = nullptr);
    ~MarkoffDocument() override;

    // Reads
    const QString  &toMarkdown() const;
    qsizetype       length() const;
    QString         substring(qsizetype offset, qsizetype len) const;
    const Document *parsedDocument() const;
    bool            parseIsPending() const;

    // Writes — undo-stack only
    QUndoStack *undoStack() const;
    void        resetContent(const QString &newContent, Origin origin);

    // Anchors
    CursorPosition trackCursor(qsizetype offset, CursorBias bias);
    qsizetype      resolveCursor(const CursorPosition &) const;

    // Coalescing
    void setCoalescingIdleMs(int ms);
    int  coalescingIdleMs() const;

    // Package-private helpers (friend-used by MarkdownDelta + CursorPosition).
    // Kept public for simplicity; a later pass can tighten via friend decls.
    QString canonicalSubstring(qsizetype offset, qsizetype len) const;
    void    applyCanonicalDelta(qsizetype offset,
                                qsizetype removedLength,
                                const QString &inserted);
    void    releaseAnchorHandle(quint64 handle);

Q_SIGNALS:
    void contentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted);
    void parseUpdated(const Markoff::Document *parsed);
    void documentReloaded();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
