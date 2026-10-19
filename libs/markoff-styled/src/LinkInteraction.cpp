// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkInteraction.h"

#include <QEvent>
#include <QMouseEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/parser/SourceSpan.h>

namespace Markoff::Styled {

LinkInteraction::LinkInteraction(QTextEdit *edit, QObject *parent)
    : QObject(parent), m_edit(edit) {
    if (m_edit && m_edit->viewport()) {
        m_edit->viewport()->installEventFilter(this);
    }
}

LinkInteraction::~LinkInteraction() = default;

bool LinkInteraction::eventFilter(QObject *obj, QEvent *event) {
    if (m_edit && obj == m_edit->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            handlePress(static_cast<QMouseEvent *>(event));
            // Don't consume — let the editor still place the caret.
            break;
        case QEvent::MouseMove:
            handleMove(static_cast<QMouseEvent *>(event));
            break;
        case QEvent::Leave:
            handleLeave();
            break;
        default: break;
        }
    }
    return QObject::eventFilter(obj, event);
}

std::optional<Markoff::LinkActivation>
LinkInteraction::resolveLinkAt(int charPos, Qt::KeyboardModifiers mods) const {
    if (!m_doc || !m_edit) return std::nullopt;
    QTextDocument *qdoc = m_edit->document();
    if (!qdoc) return std::nullopt;
    if (charPos < 0 || charPos > qdoc->characterCount()) return std::nullopt;

    // The block under the cursor decides everything. Resolve it (and its link
    // spans) once per block entry; a move within the same block only scans the
    // cached list. The old path rebuilt the whole flat prefix and re-walked the
    // block list on every mouse move (ASK-M17).
    QTextCursor cur(qdoc);
    cur.setPosition(charPos);
    const QTextBlock block = cur.block();
    if (!block.isValid()) return std::nullopt;

    if (m_cache.blockNumber != block.blockNumber()) {
        m_cache.blockNumber = block.blockNumber();
        m_cache.id = Markoff::BlockId{};
        m_cache.linkSpans.clear();

        const QString plain = m_edit->toPlainText();
        const quint32 byteOffset =
            static_cast<quint32>(plain.left(block.position()).toUtf8().size());
        const auto hit = Markoff::Detail::findBlockAtSepByte(
            m_doc, byteOffset, /*biasForward=*/true);
        if (hit) {
            m_cache.id = hit->blockId;
            for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(hit->blockId)) {
                if (span.isLink || span.isWikilink)
                    m_cache.linkSpans.append(span);
            }
        }
    }
    if (m_cache.id.isNull()) return std::nullopt;

    const int blockCharPos = cur.positionInBlock();
    for (const Markoff::SourceSpan &span : m_cache.linkSpans) {
        if (blockCharPos < span.charOffset) continue;
        if (blockCharPos >= span.charOffset + span.charLength) continue;

        Markoff::LinkActivation a;
        a.kind        = span.isWikilink ? Markoff::LinkKind::WikiLink
                                        : Markoff::LinkKind::External;
        a.rawText     = span.isWikilink ? span.linkTarget.page
                                        : span.linkTarget.url;
        a.modifiers   = mods;
        a.fromContext = m_fromContext;
        return a;
    }
    return std::nullopt;
}

void LinkInteraction::setMarkoffDocument(Markoff::MarkoffDocument *doc)
{
    if (m_doc == doc) return;
    QObject::disconnect(m_docChangedCon);
    m_docChangedCon = {};
    m_doc = doc;
    m_cache = {};
    if (doc) {
        // Span offsets and block identity move with every edit: drop the cache
        // rather than trying to validate it.
        m_docChangedCon = connect(doc, &Markoff::MarkoffDocument::d2DocumentChanged,
                                  this, [this] { m_cache = {}; });
    }
}

void LinkInteraction::handlePress(QMouseEvent *e) {
    if (!m_service) return;
    QTextCursor c = m_edit->cursorForPosition(e->pos());
    auto act = resolveLinkAt(c.position(), e->modifiers());
    if (act) m_service->activate(*act);
}

void LinkInteraction::handleMove(QMouseEvent *e) {
    if (!m_service || !m_doc) return;
    QTextCursor c = m_edit->cursorForPosition(e->pos());
    const auto act = resolveLinkAt(c.position(), e->modifiers());
    const QString newRaw = act ? act->rawText : QString();
    if (newRaw == m_currentHoveredRawText) return;

    if (!m_currentHoveredRawText.isEmpty()) {
        m_service->notifyHoverLeft(m_currentHoveredRawText);
    }
    m_currentHoveredRawText = newRaw;
    if (act) {
        m_service->notifyHover(*act, e->globalPosition().toPoint());
        m_edit->viewport()->setCursor(Qt::PointingHandCursor);
    } else {
        m_edit->viewport()->setCursor(Qt::IBeamCursor);
    }
}

void LinkInteraction::handleLeave() {
    if (m_currentHoveredRawText.isEmpty()) return;
    if (m_service) m_service->notifyHoverLeft(m_currentHoveredRawText);
    m_currentHoveredRawText.clear();
    if (m_edit && m_edit->viewport()) {
        m_edit->viewport()->setCursor(Qt::IBeamCursor);
    }
}

}  // namespace Markoff::Styled
