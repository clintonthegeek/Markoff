// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_GUTTERCOLUMN_H
#define MARKOFF_GUTTERCOLUMN_H

#include <QRect>
#include <Qt>

class QPainter;

namespace Markoff {

class FoldingModel;

/// Abstract column rendered inside FoldGutter. Columns paint cells
/// for each visible heading-index and handle clicks addressed to them.
class GutterColumn {
public:
    virtual ~GutterColumn() = default;
    virtual int width() const = 0;
    /// Paint a cell for heading at `headingIndex` (index into
    /// FoldingModel::headings()). `cellRect` is in column-local coords.
    virtual void paintCell(QPainter *painter,
                           const QRect &cellRect,
                           int headingIndex) = 0;
    /// Handle a click. `localPos` is within this column's rect.
    /// Returns true if the click was handled.
    virtual bool handleClick(QPoint localPos,
                             int headingIndex,
                             Qt::KeyboardModifiers mods) = 0;
};

/// Concrete column: paints a fold triangle per heading, handles
/// toggle on click, foldAllAtLevel on Ctrl+Click.
class FoldArrowColumn : public GutterColumn {
public:
    explicit FoldArrowColumn(FoldingModel *model) : m_model(model) {}
    int width() const override { return 16; }
    void paintCell(QPainter *p, const QRect &rect, int idx) override;
    bool handleClick(QPoint pos, int idx, Qt::KeyboardModifiers mods) override;
private:
    FoldingModel *m_model;
};

} // namespace Markoff

#endif // MARKOFF_GUTTERCOLUMN_H
