// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QPlainTextEdit>
#include <QString>

namespace Markoff::Source {
namespace Detail {

/// Thin QPlainTextEdit subclass that promotes protected geometry accessors
/// to public so Gutter can call them without being a QPlainTextEdit subclass,
/// and paints ListItem raw-markdown marker decorations (queue #8.3; spec
/// docs/specs/2026-06-16-source-listitem-marker-decoration-design.md).
///
/// The markers are paint-time-only: they are NOT inserted into the
/// QTextDocument. Editor reserves left-margin space per ListItem
/// QTextBlockFormat (applyListItemMarkerDecorations()) and hands this class
/// the marker string to paint into that reserved gap — same visible-block
/// walk Gutter uses, just drawn into the viewport instead of the side
/// gutter.
class InnerEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InnerEditor(QWidget *parent = nullptr) : QPlainTextEdit(parent) {}

    // Promote protected QPlainTextEdit/QAbstractScrollArea methods to public.
    using QPlainTextEdit::firstVisibleBlock;
    using QPlainTextEdit::blockBoundingGeometry;
    using QPlainTextEdit::contentOffset;
    using QPlainTextEdit::blockBoundingRect;
    using QAbstractScrollArea::setViewportMargins;

    /// Replaces the block-number → marker-text table used by paintEvent.
    /// Rebuilt wholesale on every call (no persistent cache to go stale).
    void setListItemMarkers(const QHash<int, QString> &markers) {
        m_listItemMarkers = markers;
        viewport()->update();
    }
    QString listItemMarkerForBlock(int blockNumber) const {
        return m_listItemMarkers.value(blockNumber);
    }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QHash<int, QString> m_listItemMarkers;
};

} // namespace Detail
} // namespace Markoff::Source
