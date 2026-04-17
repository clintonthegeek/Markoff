// SPDX-License-Identifier: GPL-3.0-or-later
#include "GutterColumn.h"
#include "FoldingModel.h"
#include <QPainter>
#include <QPolygon>

namespace Markoff {

void FoldArrowColumn::paintCell(QPainter *p, const QRect &rect, int idx) {
    if (idx < 0 || idx >= m_model->headings().size()) return;
    const auto &entry = m_model->headings()[idx];
    const bool folded = m_model->isFolded(entry.path);

    // 7px triangle centered in the 16px cell. Adapted from
    // ~/src/kde/src/ktexteditor/src/view/kateviewhelpers.cpp:2194.
    const QPoint c = rect.center();
    const int s = 3; // half-size
    QPolygon tri;
    if (folded) {
        // Rightward: closed fold. Base is on the left side of the cell;
        // shift base one pixel left so it lands clearly in the left quarter
        // of a 16-px cell (base at c.x()-s-1 = 3 for center=7).
        tri << QPoint(c.x() - s - 1, c.y() - s)
            << QPoint(c.x() - s - 1, c.y() + s)
            << QPoint(c.x() + s,     c.y());
    } else {
        // Downward: open fold.
        tri << QPoint(c.x() - s, c.y() - s)
            << QPoint(c.x() + s, c.y() - s)
            << QPoint(c.x(),     c.y() + s);
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(128, 128, 128));  // theme-aware in Task 11
    p->drawPolygon(tri);
    p->restore();
}

bool FoldArrowColumn::handleClick(QPoint, int idx, Qt::KeyboardModifiers mods) {
    if (idx < 0 || idx >= m_model->headings().size()) return false;
    const auto &entry = m_model->headings()[idx];
    if (mods & Qt::ControlModifier) {
        const int level = entry.info.level;
        // Toggle: if all at this level already folded, unfold them; else fold.
        bool allFolded = true;
        for (const auto &h : m_model->headings()) {
            if (h.info.level == level && !m_model->isFolded(h.path)) {
                allFolded = false; break;
            }
        }
        if (allFolded) m_model->unfoldAllAtLevel(level);
        else m_model->foldAllAtLevel(level);
    } else {
        m_model->toggle(entry.path);
    }
    return true;
}

} // namespace Markoff
