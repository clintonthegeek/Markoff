// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <optional>

#include <markoff/core/LinkActivation.h>

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

    void setMarkoffDocument(Markoff::MarkoffDocument *doc) { m_doc = doc; }
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
    Markoff::MarkoffDocument  *m_doc        = nullptr;
    Markoff::LinkService      *m_service    = nullptr;
    QString                    m_fromContext;
    QString                    m_currentHoveredRawText;
};

}  // namespace Markoff::Styled
