// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SCENECOORDINATOR_H
#define MARKOFF_SCENECOORDINATOR_H

#include <markoff/Theme.h>
#include "TableConverter.h"
#include <QObject>
#include <QList>
#include <QFont>
#include <QHash>
#include <QPair>

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

Q_SIGNALS:
    void textChanged();
    void reparsed();

private:
    static int interItemNewlines(bool prevIsText, bool currIsText);
    static int sourceLineCount(const MarkdownTextItem *item);

    MarkdownTextItem *createTextItem(const QString &text);
    void handleBoundary(MarkdownTextItem *from, Qt::Edge edge);
    void clearItems();
    void repositionItems();
    void onItemTextChanged();
    void reparse();

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

    QList<TableConverter::TableRegion> detectTableRegions(const QString &markdown) const;

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
    bool m_inReparse = false;
    int m_keyboardCurrentIdx = -1;
    int m_keyboardAnchorIdx = -1;
    int m_keyboardAnchorPos = -1;
};

} // namespace Markoff

#endif // MARKOFF_SCENECOORDINATOR_H
