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
#include <markoff/core/SourceTextDocumentBinding.h>
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
        if (event->type() == QEvent::MouseButtonPress) {
            handlePress(static_cast<QMouseEvent *>(event));
            // Don't consume — let the editor still place the caret.
        }
    }
    return QObject::eventFilter(obj, event);
}

std::optional<Markoff::LinkActivation>
LinkInteraction::resolveLinkAt(int charPos, Qt::KeyboardModifiers mods) const {
    if (!m_doc || !m_edit) return std::nullopt;
    const QByteArray flat = m_doc->flatView();

    quint32 bytePos = 0;
    const auto blocks = m_doc->iterateBlocks();
    static constexpr int kSepLen = 2;  // "\n\n"
    for (size_t i = 0; i < blocks.size(); ++i) {
        const Markoff::BlockId id = blocks[i];
        const QByteArray text = m_doc->blockText(id);
        const quint32 blockStartBytes = bytePos;
        const quint32 blockEndBytes   = bytePos + static_cast<quint32>(text.size());

        const int startQt = Markoff::SourceTextDocumentBinding
            ::byteOffsetToQtPos(flat, blockStartBytes);
        const int endQt = Markoff::SourceTextDocumentBinding
            ::byteOffsetToQtPos(flat, blockEndBytes);

        bytePos = blockEndBytes;
        if (i + 1 < blocks.size()) bytePos += kSepLen;

        if (charPos < startQt || charPos > endQt) continue;

        for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(id)) {
            if (!span.isLink && !span.isWikilink) continue;
            const int spanStartQt = startQt + span.charOffset;
            const int spanEndQt   = spanStartQt + span.charLength;
            if (charPos < spanStartQt || charPos >= spanEndQt) continue;

            Markoff::LinkActivation a;
            a.kind        = span.isWikilink ? Markoff::LinkKind::WikiLink
                                            : Markoff::LinkKind::External;
            a.rawText     = span.isWikilink ? span.linkTarget.page
                                            : span.linkTarget.url;
            a.modifiers   = mods;
            a.fromContext = m_fromContext;
            return a;
        }
    }
    return std::nullopt;
}

void LinkInteraction::handlePress(QMouseEvent *e) {
    if (!m_service) return;
    QTextCursor c = m_edit->cursorForPosition(e->pos());
    auto act = resolveLinkAt(c.position(), e->modifiers());
    if (act) m_service->activate(*act);
}

void LinkInteraction::handleMove(QMouseEvent *) {
    // Implemented in Task 11.
}

void LinkInteraction::handleLeave() {
    // Implemented in Task 12.
}

}  // namespace Markoff::Styled
