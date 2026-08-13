// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/canvas/View.h>

#include <QPaintEvent>

namespace Markoff::Canvas {

View::View(QWidget *parent) : QAbstractScrollArea(parent)
{
    viewport()->setFocusPolicy(Qt::NoFocus);
    setFocusPolicy(Qt::StrongFocus);
}

View::~View() = default;

void View::setDocument(MarkoffDocument *doc)
{
    if (m_doc == doc)
        return;
    m_doc = doc;
    // T1: drop the layout cache, recompute the scroll range, repaint.
    viewport()->update();
}

MarkoffDocument *View::document() const
{
    return m_doc;
}

void View::paintEvent(QPaintEvent *event)
{
    // T0 scaffold paints nothing. T1 walks the visible realized blocks
    // and calls QTextLayout::draw() for each.
    Q_UNUSED(event);
}

}  // namespace Markoff::Canvas
