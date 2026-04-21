// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "markoff/reading/CodeBlockHighlighter.h"
#include "markoff/reading/VaultResourceProvider.h"

#include <markoff/CodeBlockProcessorRegistry.h>
#include <markoff/EmbedDepthGuard.h>
#include <markoff/EmbedRegistry.h>
#include <markoff/MarkdownView.h>
#include <markoff/MermaidRenderer.h>
#include <markoff/vault/LinkResolver.h>
#include <markoff/vault/MetadataCache.h>
#include <markoff/vault/MetadataParser.h>

#include <QMultiHash>
#include <QString>
#include <QVector>
#include <memory>

class QGraphicsScene;
class QGraphicsView;
class QGraphicsItem;
class QScrollBar;
class QTimer;

namespace Markoff::Reading {

class ReadingParseWorker;
class ReadingPipeline;
class ReadingSection;
class SectionLayout;
class SectionRecyclePool;
class StyleManager;
class VirtualScrollController;
class ReadingSearchAdapter;

/// Obsidian-compatible Reading-mode widget. Phase 3b wires in eleven
/// content types — headings, paragraphs, code blocks, lists, horizontal
/// rules, blockquotes, tables, inline images, wiki-links, math (inline +
/// display), and Mermaid fenced blocks.
///
/// Tri-view Phase A: inherits Markoff::MarkdownView (a QWidget) and
/// composes a QGraphicsView child rather than inheriting QGraphicsView
/// directly, so the polymorphic view API can be uniform across Source,
/// LivePreview, and Reading leaves.
class ReadingView : public Markoff::MarkdownView {
    Q_OBJECT

public:
    explicit ReadingView(QWidget *parent = nullptr);
    ~ReadingView() override;

    void setPlainText(const QString &markdown);

    float scrollPositionVisualLine() const;
    void setScrollPositionVisualLine(float visualLine);

    qreal contentWidth() const;
    void setContentWidth(qreal width);

    Theme theme() const;
    void setTheme(Theme theme);

    /// Supply a vault resource provider for image embeds + wiki-link
    /// resolution. The caller retains ownership; pass `nullptr` to clear.
    void setVaultResourceProvider(VaultResourceProvider *provider);
    VaultResourceProvider *vaultResourceProvider() const;

    /// Phase C1 DI seam: inject host-owned implementations of the
    /// Markoff::* and Markoff::Vault:: abstracts. All setters accept
    /// `nullptr` (reverts to the lazy-constructed no-op default from
    /// markoff-core). Caller retains ownership.
    void setEmbedRegistry(Markoff::EmbedRegistry *registry);
    void setVaultLinkResolver(Markoff::Vault::LinkResolver *resolver);
    void setVaultMetadataCache(Markoff::Vault::MetadataCache *cache);
    void setVaultMetadataParser(Markoff::Vault::MetadataParser *parser);
    void setMermaidRenderer(Markoff::MermaidRenderer *renderer);

    /// Depth guard used for embed dispatch. Exposed so hosts can share
    /// state with their own embed pipeline.
    Markoff::EmbedDepthGuard *embedDepthGuard();

    const QVector<std::shared_ptr<ReadingSection>> &sections() const
    {
        return m_sections;
    }

    /// Pool size — exposed for tests and diagnostics.
    int recyclePoolSize() const;

    /// Phase 6 — heading-fold persistence. `foldedHeadingLines()` returns
    /// the source-line indices of collapsed headings;
    /// `setFoldedHeadingLines()` restores them. Folding a level-N heading
    /// hides every subsequent section until the next heading at level ≤ N.
    ///
    /// Named `*Lines` to avoid colliding with MarkdownView's FoldSpec-based
    /// `foldedHeadings()` override (which is also provided below).
    QVector<int> foldedHeadingLines() const;
    void setFoldedHeadingLines(const QVector<int> &lines);

    /// Toggle the `headingCollapsed` flag on section `sectionIdx` and
    /// re-evaluate visibility + mounting. No-op for non-heading sections.
    void toggleFold(int sectionIdx);

    /// Phase 6 accessor — number of sections currently mounted by the
    /// virtual-scroll controller. Exposed for tests.
    int mountedCount() const;

    /// Phase C3 accessor — total number of sections built from the last
    /// parse. Exposed for tests.
    int sectionCount() const;

    /// Cluster J phase 5 — built-in code-block processor registry.
    Markoff::CodeBlockProcessorRegistry *codeBlockProcessorRegistry();
    const Markoff::CodeBlockProcessorRegistry *
    codeBlockProcessorRegistry() const;

    // --- QGraphicsView passthroughs (tests + internal helpers) ---
    QGraphicsScene *scene() const;
    QGraphicsView *graphicsView() const { return m_graphicsView; }
    QWidget *viewport() const;
    QScrollBar *verticalScrollBar() const;
    QScrollBar *horizontalScrollBar() const;
    QPointF mapToScene(const QPoint &p) const;
    QPointF mapToScene(int x, int y) const;
    QPoint mapFromScene(const QPointF &p) const;
    // Rect overload used internally for viewport-scene conversion.
    QPolygonF mapToScene(const QRect &rect) const;

    // --- MarkdownView overrides ---
    void setDocument(Markoff::MarkoffDocument *doc) override;
    Markoff::MarkoffDocument *document() const override;
    void setViewTheme(const Markoff::Theme &theme) override;
    void setViewResourceProvider(Markoff::ResourceProvider *rp) override;
    void setViewLinkResolver(Markoff::LinkResolver *lr) override;
    float scrollPosition() const override;
    void setScrollPosition(float visualLine) override;
    void zoomIn() override;
    void zoomOut() override;
    void resetZoom() override;
    QJsonObject ephemeralState() const override;
    void setEphemeralState(const QJsonObject &) override;
    Markoff::SearchAdapter *searchAdapter() override;
    bool hasCursor() const override { return false; }
    bool hasEditing() const override { return false; }
    bool hasFold() const override { return true; }
    bool setReadOnly(bool readOnly) override;
    bool isReadOnly() const override;
    QVector<Markoff::FoldSpec> foldedHeadings() const override;
    void setFoldedHeadings(const QVector<Markoff::FoldSpec> &) override;

Q_SIGNALS:
    /// Emitted after `zoomIn/zoomOut/resetZoom` has mutated the view
    /// transform. Callers may query the current factor via the viewport
    /// transform if needed.
    void zoomChanged();
    void scrollPositionVisualLineChanged(float visualLine);
    void wikiLinkActivated(const QString &target);
    /// Phase C5 — unified link-hover signal. Fires for both wiki-links
    /// and external URLs. `href` is the target (resolved wiki-target
    /// string for wiki-links; raw URL for regular links). Empty `href`
    /// indicates hover-leave — subscribers should hide their popover.
    /// `globalPos` is the hover position in global screen coordinates.
    void linkHovered(const QString &href, const QPoint &globalPos);
    /// Emitted when every section of the most recent parse has been
    /// considered for mount.
    void mountingFinished();
    /// Phase 6 — fold state changed via `toggleFold()` or
    /// `setFoldedHeadingLines()`. Caller may persist via
    /// `foldedHeadingLines()`.
    void foldedHeadingsChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void registerBuiltinCodeBlockProcessors();
    void beginMount(QVector<std::shared_ptr<ReadingSection>> newSections);
    void mountInitialWindowWithBudget(int startIdx);
    void onParseFinished(quint64 requestId,
                         QVector<std::shared_ptr<ReadingSection>> sections);
    qreal visualLineSpacing() const;
    QString wikiLinkTargetAt(const QPoint &viewportPos) const;
    int sectionIndexAt(const QPoint &viewportPos) const;

    // Phase C3 — canonical document binding slots.
    void onCanonicalParseUpdated();
    void onCanonicalDocumentReloaded();

    // Phase 6 — fold + geometry machinery.
    void recomputeFoldVisibility();
    void recomputeLayoutGeometry();
    QGraphicsItem *layoutSectionForController(int sectionIdx);
    void releaseSectionForController(int sectionIdx, QGraphicsItem *item);
    void updateViewportMount();

    // Composed QGraphicsView child. `m_graphicsView` is this widget's
    // only visible child; its viewport receives mouse events that we
    // intercept via eventFilter to service wiki-link activation and
    // fold-arrow clicks.
    QGraphicsView *m_graphicsView = nullptr;

    QString m_markdown;
    QString m_lastMarkdown;
    qreal m_contentWidth = 800.0;
    Theme m_theme = Theme::Light;
    VaultResourceProvider *m_vaultProvider = nullptr;

    std::unique_ptr<ReadingPipeline> m_pipeline;
    std::unique_ptr<SectionLayout> m_layout;
    std::unique_ptr<StyleManager> m_styles;
    std::unique_ptr<SectionRecyclePool> m_recyclePool;
    std::unique_ptr<ReadingParseWorker> m_worker;
    std::unique_ptr<VirtualScrollController> m_controller;
    std::unique_ptr<Markoff::CodeBlockProcessorRegistry>
        m_codeBlockRegistry;

    // Phase C1 DI seam — host-injected pointers (not owned) + lazy-
    // constructed Default* fallbacks. The public accessors below pick
    // whichever is current.
    Markoff::EmbedRegistry *m_embedRegistry = nullptr;
    Markoff::Vault::LinkResolver *m_vaultLinkResolver = nullptr;
    Markoff::Vault::MetadataCache *m_vaultMetadataCache = nullptr;
    Markoff::Vault::MetadataParser *m_vaultMetadataParser = nullptr;
    Markoff::MermaidRenderer *m_mermaidRenderer = nullptr;
    Markoff::EmbedDepthGuard m_embedDepthGuard;

    // Lazy-defaults for the above. Allocated on first accessor call when
    // no host injection has occurred. Header forward-declares the types
    // to keep ReadingView.h light.
    struct LazyDefaults;
    std::unique_ptr<LazyDefaults> m_lazyDefaults;

    QVector<std::shared_ptr<ReadingSection>> m_sections;

    // Mount-loop state — lives across frame yields.
    QVector<std::shared_ptr<ReadingSection>> m_pendingSections;
    QMultiHash<QByteArray, std::shared_ptr<ReadingSection>> m_oldByShape;
    bool m_pendingFmChanged = false;
    bool m_mountInProgress = false;
    bool m_initialWindowDone = false;

    // Coalescing against stale parseFinished arrivals.
    quint64 m_requestIdCounter = 0;
    quint64 m_lastRequestIdHandled = 0;

    QTimer *m_hoverTimer = nullptr;
    QString m_pendingHoverTarget;
    QPoint m_pendingHoverViewportPos;

    // Cumulative user-applied zoom factor. 1.0 == no user zoom.
    double m_userZoom = 1.0;

    // MarkdownView bridge
    Markoff::MarkoffDocument *m_markoffDoc = nullptr;
    std::unique_ptr<ReadingSearchAdapter> m_searchAdapter;
};

} // namespace Markoff::Reading
