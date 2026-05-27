// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkInteraction.h"

#include <QEvent>
#include <QMouseEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
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

    // Convert Qt UTF-16 char position to UTF-8 byte offset in the
    // document's flat view. The flat view is what the QTextDocument
    // mirrors (per SourceTextDocumentBinding's contract), so a char
    // offset there maps to the matching byte offset here.
    const QString plain = m_edit->toPlainText();
    if (charPos < 0 || charPos > plain.size()) return std::nullopt;
    const QByteArray prefix = plain.left(charPos).toUtf8();
    const quint32 byteOffset = static_cast<quint32>(prefix.size());

    // Bisect to the containing block via the CRDT index.
    // textAnchorAt()'s D2 path walks per-block CRDT buffers, accumulating
    // byte counts, and tags the returned anchor with the block's ID.
    // anchor.block() gives us the block ID directly — O(blocks) but with
    // early termination, which is still far cheaper than the old O(blocks +
    // all_spans) approach for documents where the target is not the last block.
    // blockAt(TextAnchor) is not used here because it relies on
    // latestBlockRanges, which is only populated in the legacy (pre-D2) path.
    const Markoff::TextAnchor anchor =
        m_doc->textAnchorAt(byteOffset, /*rightBias=*/false);
    const Markoff::BlockId id = anchor.block();
    if (id.isNull()) return std::nullopt;

    // Offset within the block, in UTF-8 bytes.
    // offsetInBlock()'s D2 path resolves directly against the per-block
    // CRDT buffer, bypassing latestBlockRanges.
    const int blockByteOffset = m_doc->offsetInBlock(id, anchor);

    // Convert block-byte-offset to UTF-16 char offset within the
    // block by converting the block-text prefix. Block text is
    // typically <1KB; this conversion is cheap.
    const QByteArray blockBytes = m_doc->blockText(id);
    const QByteArray blockBytesPrefix = blockBytes.left(blockByteOffset);
    const int blockCharPos = QString::fromUtf8(blockBytesPrefix).size();

    // Walk only this block's spans.
    for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(id)) {
        if (!span.isLink && !span.isWikilink) continue;
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
