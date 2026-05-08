// SPDX-License-Identifier: GPL-3.0-or-later
#include "Gutter.h"
#include "InnerEditor.h"
#include <markoff/source/Editor.h>

#include <QPainter>
#include <QPaintEvent>
#include <QTextBlock>

namespace Markoff::Source::Widget {

Gutter::Gutter(Editor *editor) : QWidget(editor), m_editor(editor) {}

QSize Gutter::sizeHint() const { return QSize(m_editor->gutterWidth(), 0); }

void Gutter::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    const Markoff::Theme &t = m_editor->theme();

    const QColor bgBase  = t.color(Markoff::Theme::Slot::EditorBackground);
    // Slightly darker (or lighter on dark) gutter strip.
    const QColor bgGutter = bgBase.lightnessF() > 0.5
        ? bgBase.darker(108) : bgBase.lighter(115);
    const QColor fg       = t.color(Markoff::Theme::Slot::TextDefault);
    QColor digit          = fg; digit.setAlphaF(0.55);
    QColor digitActive    = fg;
    QColor sep            = fg; sep.setAlphaF(0.18);

    p.fillRect(event->rect(), bgGutter);
    // Right-edge separator
    p.setPen(sep);
    p.drawLine(width() - 1, event->rect().top(), width() - 1, event->rect().bottom());

    // Access protected QPlainTextEdit geometry methods via InnerEditor (promotes them to public).
    auto *pe = static_cast<InnerEditor *>(m_editor->plainTextEdit());
    QTextBlock block = pe->firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = pe->blockBoundingGeometry(block)
                    .translated(pe->contentOffset()).top();
    qreal bottom = top + pe->blockBoundingRect(block).height();
    const int currentLine = pe->textCursor().blockNumber();

    p.setFont(pe->font());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            p.setPen(blockNumber == currentLine ? digitActive : digit);
            const QString num = QString::number(blockNumber + 1);
            p.drawText(0, int(top), width() - 6,
                       pe->fontMetrics().height(),
                       Qt::AlignRight | Qt::AlignVCenter, num);
        }
        block = block.next();
        top = bottom;
        bottom = top + pe->blockBoundingRect(block).height();
        ++blockNumber;
    }
}

} // namespace
