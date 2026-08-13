// SPDX-License-Identifier: GPL-3.0-or-later
#include "InnerEditor.h"

#include <QPainter>
#include <QPaintEvent>
#include <QTextBlock>

namespace Markoff::Source::Detail {

void InnerEditor::paintEvent(QPaintEvent *event)
{
    // Base class paints the document text first — each ListItem block's
    // QTextBlockFormat left margin (set by Editor::applyListItemMarkerDecorations)
    // already reserves the gap this method draws into, so text never overlaps
    // the marker.
    QPlainTextEdit::paintEvent(event);

    if (m_listItemMarkers.isEmpty()) return;

    QPainter p(viewport());
    p.setFont(font());
    p.setPen(palette().text().color());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const auto it = m_listItemMarkers.constFind(blockNumber);
            if (it != m_listItemMarkers.constEnd() && !it.value().isEmpty()) {
                const QTextBlockFormat bf = block.blockFormat();
                p.drawText(QRectF(0, top, bf.leftMargin(), bottom - top),
                           Qt::AlignLeft | Qt::AlignVCenter, it.value());
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++blockNumber;
    }
}

} // namespace Markoff::Source::Detail
