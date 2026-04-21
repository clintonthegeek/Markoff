// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SCENECOORDINATOR_H
#define MARKOFF_SCENECOORDINATOR_H

#include <markoff/Theme.h>
#include <markoff/MarkoffDocument.h>
#include "TableConverter.h"
#include <markoff-parser/Document.h>
#include <QObject>
#include <QList>
#include <QVector>
#include <QFont>
#include <QHash>
#include <QPair>
#include <QByteArray>

class QTimer;

namespace Markoff {

class SelectionScene;
class SelectableItem;
class MarkdownTextItem;
class MarkdownHighlighter;
class TreeSitterParser;
class ResourceProvider;
class FoldingModel;

/// Manages the ordered list of scene items, their vertical positioning,
/// splitting/merging on reparse, and serialization back to markdown.
class SceneCoordinator : public QObject {
    Q_OBJECT
public:
    explicit SceneCoordinator(SelectionScene *scene, QObject *parent = nullptr);
    ~SceneCoordinator() override;

    struct GlobalPosition {
        int line = 1;
        int column = 1;
    };

    struct ItemPosition {
        MarkdownTextItem *item = nullptr;
        int localBlockNumber = 0;
    };

    /// Per-item canonical offset record. `canonicalStart` and `canonicalEnd`
    /// are QString char-index offsets into the canonical markdown buffer
    /// that this item renders. `canonicalEnd` is exclusive.
    struct ItemEntry {
        int canonicalStart = 0;
        int canonicalEnd = 0;   ///< exclusive
        SelectableItem *item = nullptr;
    };

    GlobalPosition globalPositionOf(const MarkdownTextItem *item,
                                     int localBlockNumber,
                                     int columnInBlock) const;

    ItemPosition itemAtGlobalLine(int globalLine) const;

    void removeBlockItem(int index);

    /// Load markdown: split at block boundaries, apply live-preview formatting.
    void loadMarkdown(const QString &markdown);

    /// Serialize all items back to flat markdown.
    QString toMarkdown() const;

    /// Set item width (for viewport resize).
    void setItemWidth(qreal width);

    /// Set font for all text items.
    void setFont(const QFont &font);

    /// Set theme for all text item highlighters.
    void setTheme(const Theme &theme);

    /// Set a non-owning resource provider used to resolve relative
    /// resource paths (images, embeds, links) in the editor pipeline.
    /// Used by ImageBlockItem for image resolution.
    void setResourceProvider(ResourceProvider *provider);
    ResourceProvider *resourceProvider() const { return m_resourceProvider; }

    /// Get ordered items (for external use).
    const QList<SelectableItem *> &items() const { return m_items; }

    /// Returns the item-offset map (one entry per scene item, ordered by
    /// canonicalStart). Populated during loadMarkdown / reparse.
    const QVector<ItemEntry> &itemMap() const { return m_itemMap; }

    /// Binary search: returns the index of the item whose
    /// [canonicalStart, canonicalEnd) range contains `offset`, or -1 if
    /// `offset` is not covered. If `offset == back().canonicalEnd` the
    /// last item index is returned (end-of-buffer sentinel).
    int findItemIndexForOffset(int offset) const;

    /// Shift all items strictly after `itemIndex` by `delta` chars.
    /// Leaves item at `itemIndex` and all items before it unchanged.
    /// Used by Task 16 inbound-splice to keep the map consistent after
    /// a partial-document replacement.
    void shiftItemsAfter(int itemIndex, int delta);

    /// Transfer focus to an adjacent item. Returns true if successful.
    bool moveFocusTo(MarkdownTextItem *from, Qt::Edge edge);

    /// Subscribe to fold-state changes and apply item visibility.
    void setFoldingModel(FoldingModel *model);

    /// Return the index of the item whose scene bounding rect contains sceneY.
    /// Returns -1 if none.
    int itemIndexAt(qreal sceneY) const;

    /// Return the heading path enclosing itemIndex (i.e. the most-recent
    /// heading at or before itemIndex). Returns empty list for items before
    /// the first heading. PUBLIC — used by Editor (Task 8) for auto-unfold.
    QStringList enclosingHeadingPath(int itemIndex) const;

    /// Return the heading index in FoldingModel::headings() if itemIndex
    /// itself is a heading item, otherwise -1. Used by Task 10 gutter click
    /// dispatch.
    int headingIndexForItem(int itemIndex) const;

    /// Returns the heading-index (into FoldingModel::headings()) at the given
    /// scene Y, or -1 if no heading block's Y range contains sceneY.
    /// Walks all items in document order counting heading-blocks per-block,
    /// mirroring applyFoldVisibility's hSeen counting.
    int headingIndexAtSceneY(qreal sceneY) const;

    /// Returns the scene Y of the top of the heading block at headingIndex
    /// in FoldingModel::headings(), or -1 if not found / not laid out.
    qreal headingSceneY(int headingIndex) const;

    /// Resolve the enclosing heading path for a specific QTextBlock within
    /// the given item. Walks all items[0..itemIndex] tallying heading-blocks
    /// (block.text().trimmed().startsWith('#')); within itemIndex, only counts
    /// heading-blocks at or before blockNumber. Returns the path of the
    /// most-recently-counted heading, or empty if none seen.
    /// PUBLIC — used by Editor (Task 8) for find auto-unfold.
    QStringList enclosingHeadingPathAtBlock(int itemIndex, int blockNumber) const;

    /// Phase C3 Task 15 — inform the coordinator of the bound MarkoffDocument
    /// so that per-item contentsChange can push MarkdownDeltas onto its undo
    /// stack. Call from Editor::setDocument; pass nullptr to detach.
    void setBoundDocument(Markoff::MarkoffDocument *doc);

    /// Phase C3 Task 16 — apply an inbound canonical delta (from
    /// MarkoffDocument::contentsChanged) into the affected per-item
    /// QTextDocument. Single-item deltas are spliced directly; multi-item
    /// deltas set m_sceneNeedsFullRebuildOnNextParse and apply a conservative
    /// offset update so the next parseUpdated can force-rebuild.
    void applyCanonicalDelta(qsizetype offset, qsizetype removed, qsizetype inserted);

Q_SIGNALS:
    void textChanged();
    void reparsed();

private:
    static int sourceLineCount(const MarkdownTextItem *item);

    MarkdownTextItem *createTextItem(const QString &text);
    void handleBoundary(MarkdownTextItem *from, Qt::Edge edge);
    void clearItems();
    void repositionItems();
    void onItemTextChanged();
    void reparse();

    /// Phase C3 Task 15 — translates a per-item local contentsChange into a
    /// canonical MarkdownDelta and pushes it onto m_boundDoc->undoStack().
    void onLocalItemContentsChange(int itemIndex, int localPos,
                                   int charsRemoved, int charsAdded);

    FoldingModel *m_foldingModel = nullptr;
    void applyFoldVisibility();

    /// Authoritative map from (itemIdx, blockNumber) → index into
    /// FoldingModel::headings(). Built from heading sourceOffsets so it
    /// can't be confused by code-block content like "#include". Rebuilt
    /// lazily when m_headingMapDirty is set (on reparsed() or
    /// setFoldingModel).
    mutable QHash<QPair<int,int>, int> m_blockToHeadingIdx;
    mutable bool m_headingMapDirty = true;
    void ensureHeadingMap() const;
    int headingAtBlock(int itemIdx, int blockNumber) const;

    /// Full-document parse artifacts captured by `loadMarkdown()` /
    /// `reparse()` right after `MarkdownSplitter::split()`. Reused by
    /// `ensureHeadingMap()` so fold operations don't trigger an
    /// independent full-document tree-sitter parse. Both fields are
    /// refreshed whenever the document is re-split.
    QList<HeadingInfo> m_rawHeadings;
    QByteArray m_rawUtf8;
    void captureFullDocumentParse(const QString &markdown);

    QList<TableConverter::TableRegion> detectTableRegions(const QString &markdown) const;

    /// Canonical offset map: one entry per scene item, ordered by
    /// canonicalStart. Populated during loadMarkdown / reparse.
    QVector<ItemEntry> m_itemMap;

    SelectionScene *m_scene = nullptr;
    QList<SelectableItem *> m_items;
    TreeSitterParser *m_parser = nullptr;
    QHash<MarkdownTextItem *, TableConverter> m_tableConverters;
    QTimer *m_reparseTimer = nullptr;
    ResourceProvider *m_resourceProvider = nullptr;
    qreal m_itemWidth = 600.0;
    qreal m_spacing = 8.0;
    qreal m_leftMargin = 16.0;
    qreal m_topMargin = 12.0;
    QFont m_font;
    Theme m_theme;
    bool m_inReparse = false;
    int m_keyboardCurrentIdx = -1;
    int m_keyboardAnchorIdx = -1;
    int m_keyboardAnchorPos = -1;

    // Phase C3 Task 15/16 — outbound + inbound delta path
    Markoff::MarkoffDocument *m_boundDoc = nullptr;
    bool m_applyingCanonicalDelta = false;

    // Phase C3 Task 16 — set when an inbound multi-item delta arrives; cleared
    // on the next parseUpdated so the next loadMarkdown() rebuilds from scratch.
    bool m_sceneNeedsFullRebuildOnNextParse = false;
};

} // namespace Markoff

#endif // MARKOFF_SCENECOORDINATOR_H
