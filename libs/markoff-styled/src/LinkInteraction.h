// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QObject>
#include <QPoint>
#include <QString>
#include <optional>

#include <markoff/core/BlockId.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/parser/SourceSpan.h>

class QTextEdit;
class QMouseEvent;

namespace Markoff {
class LinkService;
class MarkoffDocument;
}

namespace Markoff::Styled {

class LinkInteraction : public QObject {
    Q_OBJECT
public:
    explicit LinkInteraction(QTextEdit *edit, QObject *parent = nullptr);
    ~LinkInteraction() override;

    void setMarkoffDocument(Markoff::MarkoffDocument *doc);
    void setLinkService(Markoff::LinkService *svc)         { m_service = svc; }
    void setFromContext(const QString &c)                  { m_fromContext = c; }

    /// Returns the link span (if any) covering the cursor at `charPos`.
    /// Public for testability. globalPos is only used by callers that need
    /// to forward to LinkService::notifyHover; resolution itself is
    /// position-only.
    std::optional<Markoff::LinkActivation>
    resolveLinkAt(int charPos, Qt::KeyboardModifiers mods) const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void handlePress(QMouseEvent *e);
    void handleMove(QMouseEvent *e);
    void handleLeave();

    QTextEdit                 *m_edit       = nullptr;
    QPointer<Markoff::MarkoffDocument> m_doc;
    Markoff::LinkService      *m_service    = nullptr;
    QString                    m_fromContext;
    QString                    m_currentHoveredRawText;

    /// The link spans of the block under the cursor. Mouse-move resolution only
    /// pays the document-level lookup (flat prefix, block resolve, span walk)
    /// when the cursor enters a different block; within one block it is a
    /// cached-list scan (ASK-M17). Cleared on every document edit.
    struct BlockLinkCache {
        int                        blockNumber = -1;
        Markoff::BlockId           id;
        QList<Markoff::SourceSpan> linkSpans;
    };
    mutable BlockLinkCache     m_cache;
    QMetaObject::Connection    m_docChangedCon;
};

}  // namespace Markoff::Styled
