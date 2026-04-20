// SPDX-License-Identifier: GPL-3.0-or-later
#include "FoldGutter.h"

#include "FoldingModel.h"
#include "SceneCoordinator.h"
#include "GutterColumn.h"

#include <QPainter>
#include <QGraphicsSceneMouseEvent>

namespace Markoff {

FoldGutter::FoldGutter(FoldingModel *model, QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_model(model)
{
    Q_ASSERT(model);
    connect(m_model, &FoldingModel::foldStateChanged, this, [this]() {
        update();
    });
    // The gutter does not intercept events meant for items behind it by default.
    setAcceptedMouseButtons(Qt::LeftButton);
}

FoldGutter::~FoldGutter()
{
    qDeleteAll(m_columns);
}

void FoldGutter::setCoordinator(SceneCoordinator *coordinator)
{
    m_coordinator = coordinator;
}

void FoldGutter::setColumns(QList<GutterColumn *> columns)
{
    prepareGeometryChange();
    qDeleteAll(m_columns);
    m_columns = std::move(columns);
    update();
}

int FoldGutter::width() const
{
    if (m_columns.isEmpty())
        return 0;
    int sum = 0;
    for (const auto *col : m_columns)
        sum += col->width();
    return sum + m_separator;
}

QRectF FoldGutter::boundingRect() const
{
    return QRectF(0, 0, width(), 100000.0);
}

void FoldGutter::paint(QPainter *painter,
                       const QStyleOptionGraphicsItem * /*option*/,
                       QWidget * /*widget*/)
{
    if (m_columns.isEmpty() || !m_model)
        return;

    const auto &headings = m_model->headings();
    if (headings.isEmpty())
        return;

    if (!m_coordinator) {
        // TODO(Task 11): coordinator is wired in Editor; nothing to paint yet.
        return;
    }

    // For each heading, resolve its scene-Y and paint each column cell.
    for (int hIdx = 0; hIdx < headings.size(); ++hIdx) {
        qreal sceneY = m_coordinator->headingSceneY(hIdx);
        if (sceneY < 0.0)
            continue; // heading not found / hidden

        // Map scene-Y to local-Y (this item is typically positioned at scene origin,
        // but use mapFromScene to be correct regardless).
        qreal localY = mapFromScene(QPointF(0.0, sceneY)).y();

        // Cell height: one line. Use a fixed 20px for Task 10; Task 11 can
        // query the actual block height from the coordinator.
        const int cellH = 20;

        int colX = 0;
        for (auto *col : m_columns) {
            QRect cellRect(colX, static_cast<int>(localY), col->width(), cellH);
            col->paintCell(painter, cellRect, hIdx);
            colX += col->width();
        }
    }

    // Draw the right-side separator line.
    int sepX = width() - m_separator;
    painter->setPen(QPen(painter->pen().color(), 1));
    painter->drawLine(sepX, 0, sepX, static_cast<int>(boundingRect().height()));
}

bool FoldGutter::handleMouseClickForTesting(QPoint localPos,
                                            int headingIndex,
                                            Qt::KeyboardModifiers mods)
{
    if (headingIndex < 0)
        return false;

    int colLocalX = 0;
    int colIdx = columnAt(localPos.x(), &colLocalX);
    if (colIdx < 0)
        return false;

    QPoint colLocal(colLocalX, localPos.y());
    return m_columns[colIdx]->handleClick(colLocal, headingIndex, mods);
}

void FoldGutter::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!m_coordinator || m_columns.isEmpty()) {
        event->ignore();
        return;
    }

    int colLocalX = 0;
    int colIdx = columnAt(static_cast<int>(event->pos().x()), &colLocalX);
    if (colIdx < 0) {
        event->ignore();
        return;
    }

    int hIdx = m_coordinator->headingIndexAtSceneY(event->scenePos().y());
    if (hIdx < 0) {
        event->ignore();
        return;
    }

    QPoint colLocal(colLocalX, static_cast<int>(event->pos().y()));
    bool handled = m_columns[colIdx]->handleClick(colLocal, hIdx, event->modifiers());
    if (handled)
        event->accept();
    else
        event->ignore();
}

int FoldGutter::columnAt(qreal x, int *localXOut) const
{
    qreal cursor = 0.0;
    for (int i = 0; i < m_columns.size(); ++i) {
        qreal right = cursor + m_columns[i]->width();
        if (x >= cursor && x < right) {
            if (localXOut)
                *localXOut = static_cast<int>(x - cursor);
            return i;
        }
        cursor = right;
    }
    return -1;
}

} // namespace Markoff
