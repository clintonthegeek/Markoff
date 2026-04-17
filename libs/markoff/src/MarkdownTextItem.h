// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_MARKDOWNTEXTITEM_H
#define MARKOFF_MARKDOWNTEXTITEM_H

#include "SelectableItem.h"
#include "DecoratedRange.h"
#include <QGraphicsObject>
#include <markoff-parser/SourceSpan.h>

class QTextDocument;

namespace Markoff {

class TextControl;
class MathTextObject;
class CheckboxTextObject;

/// Editable markdown text region in the graphics scene.
/// Wraps TextControl + QTextDocument. Implements SelectableItem
/// for text-level selection operations.
class MarkdownTextItem : public QGraphicsObject, public SelectableItem {
    Q_OBJECT
public:
    explicit MarkdownTextItem(QGraphicsItem *parent = nullptr);
    ~MarkdownTextItem() override;

    /// Set the raw markdown text content.
    void setPlainText(const QString &text);

    /// Access the underlying document and control.
    QTextDocument *document() const;
    TextControl *textControl() const { return m_control; }

    /// Set the text width (for word wrap). Triggers relayout.
    void setTextWidth(qreal width);

    /// Decorated ranges detected in this item's document.
    const QList<DecoratedRange> &decoratedRanges() const { return m_decoratedRanges; }

    // QGraphicsItem
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

public:

    // SelectableItem
    QGraphicsItem *asGraphicsItem() override { return this; }
    bool isTextItem() const override { return true; }
    int hitTest(const QPointF &scenePos) const override;
    void setSelection(int anchorPos, int cursorPos) override;
    void clearSelection() override;
    QString selectedMarkdown() const override;
    QString allMarkdown() const override;
    int documentLength() const override;
    QString toMarkdown() const override;

    /// Reapply all inline object substitutions (math, checkboxes, etc.)
    /// based on the highlighter's span map. Strips existing objects first.
    void refreshInlineSubstitutions();

    /// Re-detect decorated ranges and block formatting from the current
    /// span map. Called by SceneCoordinator after updating the span map
    /// during a non-structural reparse.
    void refreshBlockFormatting();

    /// Replace all U+FFFC inline objects with their stored source text,
    /// leaving the document in canonical source form. Called between
    /// reparse and re-substitution so span offsets line up.
    int stripInlineSubstitutions();

    /// Build a string with the same character count as the QTextDocument
    /// where blocks inside QTextTable frames are replaced with spaces.
    /// Used for tree-sitter parsing during reparse so that span offsets
    /// are in document-coordinate space (not pipe-text-coordinate space).
    /// Must be called when the document is in source form (after
    /// stripInlineSubstitutions).
    QString buildHighlightingSource() const;

    /// Invalidate the cached source-position spans so that the next
    /// refreshInlineSubstitutions() re-reads from the highlighter
    /// instead of restoring stale cached spans. Call this after
    /// externally setting a new span map (e.g., after table offset
    /// mapping adjusts span coordinates).
    void invalidateSourcePositionSpans() { m_sourcePositionSpans.clear(); }

    /// Set a QTextBlock visible or hidden by folding. Hidden blocks are
    /// rendered with zero height so they take no space in the layout.
    /// Exposed for SceneCoordinator's applyFoldVisibility.
    void setBlockFolded(int blockNumber, bool folded);

Q_SIGNALS:
    void textChanged();
    /// Emitted when arrow key can't move further.
    void cursorAtBoundary(Qt::Edge edge);

private:
    void updateGeometry();
    void onCursorPositionChanged();
    void snapCursorPastDelimiters();
    void detectDecoratedRanges();
    void paintDecoratedRanges(QPainter *painter);
    void applyBlockFormats();

    /// Walk the highlighter span map and replace each inline object span
    /// (math, checkboxes, etc.) with U+FFFC + format. Must be called on
    /// a document in canonical source form (no leftover U+FFFC).
    void applyInlineSubstitutions();

    /// Cursor-driven reveal/collapse: expand a U+FFFC under the cursor
    /// to its source text on mouse click; collapse when cursor leaves.
    /// Type-dispatched: math gets reveal/collapse, checkboxes get toggle.
    void updateReveal();

    TextControl *m_control = nullptr;
    QTextDocument *m_document = nullptr;
    MathTextObject *m_mathObject = nullptr;
    CheckboxTextObject *m_checkboxObject = nullptr;
    qreal m_width = 600.0;
    bool m_snappingCursor = false;
    bool m_inSubstitution = false;
    bool m_inCursorUpdate = false;
    QList<DecoratedRange> m_decoratedRanges;

    // Cursor-reveal state for inline math. At most one math region is
    // "revealed" (shown as raw source) at a time — the one the cursor is
    // currently inside. Values are char offsets in the current document;
    // -1 means nothing is revealed.
    int m_revealedStart = -1;
    int m_revealedEnd = -1;
    bool m_revealedIsDisplay = false;  // $$ vs $
    bool m_mouseTriggered = false;     // reveal only on mouse clicks

    // Snapshot of highlighter spans at source positions, saved before
    // applyInlineSubstitutions() adjusts offsets for the substituted
    // document. Used by refreshInlineSubstitutions() to restore correct
    // source-position spans after stripping.
    QList<SourceSpan> m_sourcePositionSpans;
};

} // namespace Markoff

#endif // MARKOFF_MARKDOWNTEXTITEM_H
