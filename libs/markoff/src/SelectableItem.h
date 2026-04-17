// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SELECTABLEITEM_H
#define MARKOFF_SELECTABLEITEM_H

#include <QString>
#include <QPointF>

class QGraphicsItem;

namespace Markoff {

/// Interface for items that participate in cross-boundary selection.
/// Text items implement hitTest/setSelection/selectedMarkdown.
/// Non-text items implement setFullySelected/toMarkdown.
class SelectableItem {
public:
    virtual ~SelectableItem() = default;

    virtual QGraphicsItem *asGraphicsItem() = 0;
    virtual bool isTextItem() const = 0;

    // --- Text item operations (no-op defaults for non-text) ---
    virtual int hitTest(const QPointF &scenePos) const { Q_UNUSED(scenePos); return -1; }
    virtual void setSelection(int anchorPos, int cursorPos) { Q_UNUSED(anchorPos); Q_UNUSED(cursorPos); }
    virtual void clearSelection() {}
    virtual QString selectedMarkdown() const { return {}; }
    virtual QString allMarkdown() const { return {}; }
    /// Character count in the document (for cursor positioning).
    /// For text items with U+FFFC substitutions, this is shorter than
    /// allMarkdown().length().
    virtual int documentLength() const { return 0; }

    // --- Non-text item operations (no-op defaults for text) ---
    virtual void setFullySelected(bool selected) { Q_UNUSED(selected); }
    virtual bool isFullySelected() const { return false; }

    // --- Common ---
    virtual QString toMarkdown() const = 0;
};

} // namespace Markoff

#endif // MARKOFF_SELECTABLEITEM_H
