// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_FOLDGUTTER_H
#define MARKOFF_FOLDGUTTER_H

#include <QGraphicsObject>
#include <QList>

class QGraphicsSceneMouseEvent;

namespace Markoff {

class FoldingModel;
class SceneCoordinator;
class GutterColumn;

/// Viewport-pinned QGraphicsObject that owns a list of GutterColumns and
/// paints fold decorations for each visible heading. Dispatches mouse
/// press events to the appropriate column based on X position, resolving
/// the heading index via SceneCoordinator::headingIndexAtSceneY.
class FoldGutter : public QGraphicsObject {
    Q_OBJECT
public:
    explicit FoldGutter(FoldingModel *model, QGraphicsItem *parent = nullptr);
    ~FoldGutter() override;

    /// Set the scene coordinator used for heading-Y resolution.
    void setCoordinator(SceneCoordinator *coordinator);

    /// Replace the column list (takes ownership; deletes any previous columns).
    void setColumns(QList<GutterColumn *> columns);

    /// Sum of column widths plus a 2px right-side separator (0 if no columns).
    int width() const;

    // QGraphicsItem
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    /// Test-only: bypass scene lookup. Dispatches a click on the column at
    /// localPos.x(), passing headingIndex directly (use -1 to simulate a
    /// click on a row with no heading). Returns true if handled.
    bool handleMouseClickForTesting(QPoint localPos, int headingIndex,
                                    Qt::KeyboardModifiers mods);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    /// Returns the column index for local x, or -1 if outside all columns.
    /// Fills *localOut (if non-null) with the x offset relative to that column.
    int columnAt(qreal x, int *localXOut = nullptr) const;

    FoldingModel    *m_model;
    SceneCoordinator *m_coordinator = nullptr;
    QList<GutterColumn *> m_columns;
    int m_separator = 2;
};

} // namespace Markoff

#endif // MARKOFF_FOLDGUTTER_H
