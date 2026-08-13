// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <QAbstractScrollArea>
#include <QRectF>

#include <markoff/core/BlockId.h>
#include <markoff/core/Theme.h>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::Canvas {

class BlockLayoutCache;

/**
 * Projection view leaf: renders a MarkoffDocument directly, one
 * QTextLayout per block, with its own input pipeline.
 *
 * Authority model (spec §2): the document wins totally. This widget
 * holds no editable text state — every layout it builds is a derived
 * cache keyed by (BlockId, blockEditSequence), rebuilt from
 * blockText(), never patched in place.
 *
 * T1: read-only. Lazy layout, per-kind presentation, wheel/keyboard
 * scrolling. Caret, hit-testing and editing arrive in T2.
 */
class View : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit View(QWidget *parent = nullptr);
    ~View() override;

    /// Non-owning. Null is legal (an unbound view paints an empty page).
    void setDocument(MarkoffDocument *doc);
    MarkoffDocument *document() const;

    void setTheme(const Theme &theme);
    const Theme &theme() const;

    // ---- Inspection surface -------------------------------------------
    // Read-only views onto the derived cache, for tests and the manual
    // harness. Nothing here is authority; it all recomputes from the
    // document. Kept on the widget rather than exposing BlockLayoutCache
    // so the cache stays a private implementation detail.

    int   blockCount() const;
    int   realizedBlockCount() const;
    qreal documentHeight() const;

    /// Block bounds in document coordinates (y=0 is the document top, not
    /// the viewport top). Null rect if the id is not in the document.
    QRectF blockRect(BlockId id) const;

    /// Paints since construction. Cheap paint-counter for tests that need
    /// to know a repaint actually happened.
    quint64 paintCount() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onDocumentChanged();
    /// Realize whatever the current viewport needs and refresh the scroll
    /// range. Safe to call repeatedly; realization is monotonic.
    void ensureLayoutForViewport();
    void updateScrollRange();
    qreal pageMargin() const;
    qreal textWidth() const;

    MarkoffDocument *m_doc = nullptr;
    Theme m_theme;
    std::unique_ptr<BlockLayoutCache> m_cache;
    quint64 m_paintCount = 0;
};

}  // namespace Markoff::Canvas
