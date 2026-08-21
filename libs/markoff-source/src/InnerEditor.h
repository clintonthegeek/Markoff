// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QPlainTextEdit>
#include <QString>

#include <markoff/core/ClipboardCodec.h>

class QKeyEvent;
class QMimeData;
class QPaintEvent;

namespace Markoff::Source {
namespace Detail {

/// Thin QPlainTextEdit subclass that promotes protected geometry accessors
/// to public so Gutter can call them without being a QPlainTextEdit subclass,
/// paints ListItem raw-markdown marker decorations (queue #8.3), and routes
/// clipboard through ClipboardCodec (Cluster N) so Copy emits multi-flavor
/// mime and Paste converts HTML/RTF → markdown.
class InnerEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InnerEditor(QWidget *parent = nullptr);

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

    /// Selection as markdown bytes (U+2029 paragraph separators → `\n`).
    QByteArray selectedMarkdown() const;

    void copyWithFlavor(Markoff::ClipboardCodec::Flavor flavor);
    void pasteWithMode(Markoff::ClipboardCodec::PasteMode mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    QMimeData *createMimeDataFromSelection() const override;
    void insertFromMimeData(const QMimeData *source) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QHash<int, QString> m_listItemMarkers;
};

} // namespace Detail
} // namespace Markoff::Source
