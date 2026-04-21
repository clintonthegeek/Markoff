// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/reading/ReadingView.h"

#include "ReadingSearchAdapter.h"
#include "SpanRenderer.h"
#include "markoff/reading/CodeBlockHighlighter.h"
#include "markoff/reading/ReadingParseWorker.h"
#include "markoff/reading/ReadingPipeline.h"
#include "markoff/reading/ReadingSection.h"
#include "markoff/reading/ReadingViewConstants.h"
#include "markoff/reading/SectionLayout.h"
#include "markoff/reading/SectionRecyclePool.h"
#include "markoff/reading/VaultResourceProvider.h"
#include "markoff/reading/VirtualScrollController.h"
#include "markoff/reading/styling/StyleManager.h"

#include <markoff/MarkoffDocument.h>
#include <markoff/DefaultMermaidRenderer.h>
#include <markoff/vault/DefaultLinkResolver.h>
#include <markoff/vault/DefaultMetadataCache.h>
#include <markoff/vault/DefaultMetadataParser.h>

#include <jkqtmathtext/jkqtmathtext.h>

#include <QAbstractTextDocumentLayout>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

namespace Markoff::Reading {

namespace {

Q_LOGGING_CATEGORY(lcReading, "markoff.reading")

constexpr qreal kSectionVerticalGap = 4.0;

} // namespace

/// Lazy-default holders. Allocated on first accessor call for each
/// abstract when the host hasn't injected a real implementation. Hidden
/// behind a forward declaration in ReadingView.h to keep that header
/// light.
struct ReadingView::LazyDefaults
{
    std::unique_ptr<Markoff::Vault::DefaultLinkResolver> linkResolver;
    std::unique_ptr<Markoff::Vault::DefaultMetadataCache> metadataCache;
    std::unique_ptr<Markoff::Vault::DefaultMetadataParser> metadataParser;
    std::unique_ptr<Markoff::DefaultMermaidRenderer> mermaidRenderer;
};

ReadingView::ReadingView(QWidget *parent)
    : Markoff::MarkdownView(parent)
    , m_pipeline(std::make_unique<ReadingPipeline>(this))
    , m_layout(std::make_unique<SectionLayout>())
    , m_styles(std::unique_ptr<StyleManager>(
          StyleManager::makeObsidianDefault(Theme::Light)))
    , m_recyclePool(std::make_unique<SectionRecyclePool>())
    , m_worker(std::make_unique<ReadingParseWorker>())
    , m_controller(std::make_unique<VirtualScrollController>(this))
    , m_codeBlockRegistry(
          std::make_unique<Markoff::CodeBlockProcessorRegistry>())
    , m_lazyDefaults(std::make_unique<LazyDefaults>())
{
    // Composed QGraphicsView. The baseline (QGraphicsView-as-base) tests
    // construct a ReadingView without `show()` or `resize()` and still
    // observe a ~640×480 viewport — that's QAbstractScrollArea's default
    // size for an unparented QGraphicsView. To preserve that contract in
    // the composed widget, we copy the child's default size up onto
    // `this`, then manually size the child to fill `this`. (Using a
    // QLayout instead is a wash: the layout only activates on first
    // show/poll, and tests skip that.)
    m_graphicsView = new QGraphicsView;
    resize(m_graphicsView->size());
    m_graphicsView->setParent(this);
    m_graphicsView->setGeometry(rect());
    setFocusProxy(m_graphicsView);

    // Cluster J phase 5 — seed the plugin-reachable code-block dispatch
    // surface. Built-in processors land at construction.
    registerBuiltinCodeBlockProcessors();
    // Worker's parseFinished emits from its own thread; Qt::QueuedConnection
    // hops to ours so the UI mount loop stays on the main thread.
    connect(m_worker.get(), &ReadingParseWorker::parseFinished,
            this, &ReadingView::onParseFinished,
            Qt::QueuedConnection);

    auto *scene = new QGraphicsScene(this);
    m_graphicsView->setScene(scene);
    m_graphicsView->setFrameShape(QFrame::NoFrame);
    m_graphicsView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_graphicsView->setRenderHints(QPainter::Antialiasing
                                   | QPainter::TextAntialiasing);
    m_graphicsView->setMouseTracking(true);

    // Intercept mouse events on the graphics view's viewport so wiki-link
    // and fold-arrow clicks dispatch before the stock QGraphicsView path.
    m_graphicsView->viewport()->installEventFilter(this);


    // Wire the controller's callbacks.
    VirtualScrollController::LayoutCallbacks cbs;
    cbs.layoutOne = [this](int idx) { return layoutSectionForController(idx); };
    cbs.releaseOne = [this](int idx, QGraphicsItem *item) {
        releaseSectionForController(idx, item);
    };
    m_controller->setCallbacks(std::move(cbs));

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(300);
    connect(m_hoverTimer, &QTimer::timeout, this, [this] {
        if (!m_pendingHoverTarget.isEmpty()) {
            const QPoint globalPos = m_graphicsView && m_graphicsView->viewport()
                ? m_graphicsView->viewport()->mapToGlobal(m_pendingHoverViewportPos)
                : m_pendingHoverViewportPos;
            Q_EMIT linkHovered(m_pendingHoverTarget, globalPos);
        }
    });

    if (auto *vbar = m_graphicsView->verticalScrollBar()) {
        connect(vbar, &QScrollBar::valueChanged, this, [this] {
            Q_EMIT scrollPositionVisualLineChanged(scrollPositionVisualLine());
            // Phase 6: re-evaluate the mounted window on scroll.
            if (m_initialWindowDone)
                updateViewportMount();
        });
    }

    m_searchAdapter = std::make_unique<ReadingSearchAdapter>(this);
}

ReadingView::~ReadingView() = default;

Markoff::CodeBlockProcessorRegistry *
ReadingView::codeBlockProcessorRegistry()
{
    return m_codeBlockRegistry.get();
}

const Markoff::CodeBlockProcessorRegistry *
ReadingView::codeBlockProcessorRegistry() const
{
    return m_codeBlockRegistry.get();
}

// --- Phase C1 DI-seam setters --------------------------------------

void ReadingView::setEmbedRegistry(Markoff::EmbedRegistry *registry)
{
    m_embedRegistry = registry;
}

void ReadingView::setVaultLinkResolver(Markoff::Vault::LinkResolver *resolver)
{
    m_vaultLinkResolver = resolver;
}

void ReadingView::setVaultMetadataCache(Markoff::Vault::MetadataCache *cache)
{
    m_vaultMetadataCache = cache;
}

void ReadingView::setVaultMetadataParser(Markoff::Vault::MetadataParser *parser)
{
    m_vaultMetadataParser = parser;
}

void ReadingView::setMermaidRenderer(Markoff::MermaidRenderer *renderer)
{
    m_mermaidRenderer = renderer;
}

Markoff::EmbedDepthGuard *ReadingView::embedDepthGuard()
{
    return &m_embedDepthGuard;
}

namespace {

// Internal convenience: fall back to a lazily-constructed Default* when
// the host has not injected a concrete implementation.
template <typename Iface, typename Default>
Iface *resolveOrDefault(Iface *host, std::unique_ptr<Default> &slot)
{
    if (host) return host;
    if (!slot) slot = std::make_unique<Default>();
    return slot.get();
}

} // namespace

// --- QGraphicsView passthroughs ---
QGraphicsScene *ReadingView::scene() const { return m_graphicsView ? m_graphicsView->scene() : nullptr; }
QWidget *ReadingView::viewport() const { return m_graphicsView ? m_graphicsView->viewport() : nullptr; }
QScrollBar *ReadingView::verticalScrollBar() const { return m_graphicsView ? m_graphicsView->verticalScrollBar() : nullptr; }
QScrollBar *ReadingView::horizontalScrollBar() const { return m_graphicsView ? m_graphicsView->horizontalScrollBar() : nullptr; }
QPointF ReadingView::mapToScene(const QPoint &p) const { return m_graphicsView ? m_graphicsView->mapToScene(p) : QPointF(); }
QPointF ReadingView::mapToScene(int x, int y) const { return m_graphicsView ? m_graphicsView->mapToScene(x, y) : QPointF(); }
QPoint ReadingView::mapFromScene(const QPointF &p) const { return m_graphicsView ? m_graphicsView->mapFromScene(p) : QPoint(); }
QPolygonF ReadingView::mapToScene(const QRect &rect) const { return m_graphicsView ? m_graphicsView->mapToScene(rect) : QPolygonF(); }

void ReadingView::registerBuiltinCodeBlockProcessors()
{
    if (!m_codeBlockRegistry) return;

    m_codeBlockRegistry->registerLanguage(
        QStringLiteral("mermaid"),
        [this](const QString &source,
               void * /*node*/,
               const Markoff::CodeBlockContext & /*ctx*/) -> bool {
            Markoff::MermaidRenderer *renderer = resolveOrDefault(
                m_mermaidRenderer, m_lazyDefaults->mermaidRenderer);
            const QByteArray svg = renderer->renderSvg(source);
            return !svg.isEmpty();
        });

    auto mathProcessor =
        [](const QString &source,
           void * /*node*/,
           const Markoff::CodeBlockContext & /*ctx*/) -> bool {
            JKQTMathText mt;
            mt.parse(source);
            return !source.trimmed().isEmpty();
        };
    m_codeBlockRegistry->registerLanguage(QStringLiteral("math"),
                                          mathProcessor);
    m_codeBlockRegistry->registerLanguage(QStringLiteral("latex"),
                                          mathProcessor);

    m_codeBlockRegistry->registerLanguage(
        QStringLiteral("default"),
        [](const QString & /*source*/,
           void * /*node*/,
           const Markoff::CodeBlockContext & /*ctx*/) -> bool {
            CodeBlockHighlighter hl(Theme::Light);
            Q_UNUSED(hl);
            return true;
        });
}

void ReadingView::setPlainText(const QString &markdown)
{
    m_markdown = markdown;
    rebuild();
}

qreal ReadingView::contentWidth() const { return m_contentWidth; }

void ReadingView::setContentWidth(qreal width)
{
    if (qFuzzyCompare(width, m_contentWidth)) return;
    m_contentWidth = width;
    m_recyclePool->clear();
    m_lastMarkdown.clear();
    m_layout = std::make_unique<SectionLayout>();
    rebuild();
}

Theme ReadingView::theme() const { return m_theme; }

void ReadingView::setTheme(Theme theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    m_styles.reset(StyleManager::makeObsidianDefault(theme));
    m_recyclePool->clear();
    m_lastMarkdown.clear();
    m_layout = std::make_unique<SectionLayout>();
    rebuild();
}

void ReadingView::setVaultResourceProvider(VaultResourceProvider *provider)
{
    m_vaultProvider = provider;
    m_recyclePool->clear();
    m_lastMarkdown.clear();
    m_layout = std::make_unique<SectionLayout>();
    rebuild();
}

int ReadingView::recyclePoolSize() const
{
    return m_recyclePool ? m_recyclePool->size() : 0;
}

VaultResourceProvider *ReadingView::vaultResourceProvider() const
{
    return m_vaultProvider;
}

int ReadingView::mountedCount() const
{
    return m_controller ? m_controller->mountedCount() : 0;
}

int ReadingView::sectionCount() const
{
    return static_cast<int>(m_sections.size());
}

void ReadingView::resizeEvent(QResizeEvent *event)
{
    Markoff::MarkdownView::resizeEvent(event);
    if (m_graphicsView) m_graphicsView->setGeometry(rect());
    if (m_initialWindowDone)
        updateViewportMount();
}

bool ReadingView::eventFilter(QObject *watched, QEvent *event)
{
    if (m_graphicsView && watched == m_graphicsView->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QPoint pos = me->pos();
                const int foldIdx = sectionIndexAt(pos);
                if (foldIdx >= 0) {
                    toggleFold(foldIdx);
                    me->accept();
                    return true;
                }
                const QString target = wikiLinkTargetAt(pos);
                if (!target.isEmpty()) {
                    Q_EMIT wikiLinkActivated(target);
                    me->accept();
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            const QString target = wikiLinkTargetAt(me->pos());
            if (target != m_pendingHoverTarget) {
                m_pendingHoverTarget = target;
                m_pendingHoverViewportPos = me->pos();
                if (!target.isEmpty())
                    m_hoverTimer->start();
                else
                    m_hoverTimer->stop();
            }
            break;
        }
        default:
            break;
        }
    }
    return Markoff::MarkdownView::eventFilter(watched, event);
}

void ReadingView::rebuild()
{
    auto *s = scene();
    if (!s) return;

    const int byteLen = m_markdown.toUtf8().size();
    const quint64 requestId = ++m_requestIdCounter;

    if (byteLen >= kAsyncParseThresholdBytes) {
        while (m_worker->bumpRequestId() < requestId) { /* catch up */ }
        m_worker->parseAsync(m_markdown, requestId);
        return;
    }

    auto newSections = m_worker->parseSync(m_markdown);
    m_lastRequestIdHandled = requestId;
    beginMount(std::move(newSections));
}

void ReadingView::onParseFinished(
    quint64 requestId,
    QVector<std::shared_ptr<ReadingSection>> sections)
{
    if (requestId <= m_lastRequestIdHandled) return;
    if (requestId < m_requestIdCounter) return;
    m_lastRequestIdHandled = requestId;
    beginMount(std::move(sections));
}

void ReadingView::beginMount(
    QVector<std::shared_ptr<ReadingSection>> newSections)
{
    auto *s = scene();
    if (!s) return;

    m_pendingFmChanged = ReadingPipeline::detectFrontmatterChange(
        m_lastMarkdown, m_markdown);

    m_oldByShape.clear();
    for (auto &old : m_sections) {
        const QByteArray key = old->renderedShape();
        if (!key.isEmpty() && old->graphicsItem() != nullptr)
            m_oldByShape.insert(key, old);
    }

    for (auto &old : m_sections) {
        if (auto *item = old->graphicsItem()) {
            if (item->scene() == s)
                s->removeItem(item);
        }
    }
    s->clear();

    QVector<int> previouslyFolded = foldedHeadingLines();

    m_pendingSections = std::move(newSections);
    m_controller->setSections({});
    m_sections = m_pendingSections;
    m_lastMarkdown = m_markdown;
    m_initialWindowDone = false;
    m_mountInProgress = true;

    if (!previouslyFolded.isEmpty()) {
        QSet<int> foldedSet;
        for (int line : previouslyFolded) foldedSet.insert(line);
        for (auto &sec : m_sections) {
            sec->setHeadingCollapsed(foldedSet.contains(sec->sourceLine()));
        }
    }
    recomputeFoldVisibility();
    recomputeLayoutGeometry();

    qreal totalHeight = 0.0;
    for (const auto &sec : m_sections) {
        if (sec->hidden()) continue;
        totalHeight = qMax<qreal>(totalHeight,
                                   sec->yPos() + sec->estimatedHeight());
    }
    s->setSceneRect(0, 0, m_contentWidth, qMax<qreal>(totalHeight, 1.0));

    m_controller->setSections(m_sections);
    mountInitialWindowWithBudget(0);
}

QGraphicsItem *ReadingView::layoutSectionForController(int sectionIdx)
{
    auto *s = scene();
    if (!s) return nullptr;
    if (sectionIdx < 0 || sectionIdx >= m_sections.size()) return nullptr;

    auto &sec = m_sections[sectionIdx];
    if (!sec || sec->hidden()) return nullptr;
    if (sec->graphicsItem()) {
        QGraphicsItem *it = sec->graphicsItem();
        if (it->scene() != s) s->addItem(it);
        it->setPos(0, sec->yPos());
        return it;
    }

    const auto range = sec->sourceRange();
    const QString md = m_markdown.mid(range.from, range.to - range.from);

    const QByteArray shape = sec->renderedShape();
    const bool forceReRender = m_pendingFmChanged && sec->usesFrontMatter();

    QGraphicsItem *item = nullptr;

    if (!forceReRender && !shape.isEmpty()) {
        auto it = m_oldByShape.find(shape);
        if (it != m_oldByShape.end()) {
            std::shared_ptr<ReadingSection> match = it.value();
            m_oldByShape.erase(it);
            item = match->graphicsItem();
            match->setGraphicsItem(nullptr);
        } else if (auto *pooled = m_recyclePool->take(shape)) {
            item = pooled;
        }
    }

    if (!item) {
        SectionLayout::Context ctx;
        ctx.styles = m_styles.get();
        ctx.theme = m_theme;
        ctx.contentWidth = m_contentWidth;
        ctx.vaultProvider = m_vaultProvider;
        ctx.mermaidRenderer = resolveOrDefault(
            m_mermaidRenderer, m_lazyDefaults->mermaidRenderer);
        ctx.headingCollapsedIndicator = true;
        ctx.sectionIndex = sectionIdx;

        auto *group = m_layout->layoutSection(*sec, md, ctx);
        if (!group) return nullptr;
        item = group;
    }

    sec->setGraphicsItem(item);
    item->setPos(0, sec->yPos());
    if (!item->scene()) s->addItem(item);

    const qreal actual = item->boundingRect().height();
    if (actual > 0.0) sec->setActualHeight(actual);

    return item;
}

void ReadingView::releaseSectionForController(int sectionIdx,
                                              QGraphicsItem *item)
{
    Q_UNUSED(sectionIdx);
    auto *s = scene();
    if (!item) return;
    if (sectionIdx >= 0 && sectionIdx < m_sections.size()) {
        auto &sec = m_sections[sectionIdx];
        if (sec) sec->setGraphicsItem(nullptr);
    }
    if (s && item->scene() == s) s->removeItem(item);

    QByteArray shape;
    if (sectionIdx >= 0 && sectionIdx < m_sections.size()) {
        shape = m_sections.at(sectionIdx)->renderedShape();
    }
    if (!shape.isEmpty()) m_recyclePool->offer(shape, item);
    else delete item;
}

void ReadingView::mountInitialWindowWithBudget(int startIdx)
{
    auto *s = scene();
    if (!s) { m_mountInProgress = false; return; }

    const QRectF viewportScene =
        mapToScene(viewport() ? viewport()->rect() : QRect(0, 0, 0, 0))
            .boundingRect();
    qreal vpTop = viewportScene.top();
    qreal vpHeight = viewportScene.height();
    if (vpHeight <= 0) vpHeight = height() > 0 ? height() : 600.0;

    qreal vh = vpHeight;
    const qreal windowTop = vpTop - vh;
    const qreal windowBottom = vpTop + 2.0 * vh;

    QElapsedTimer t;
    t.start();
    int layoutsThisFrame = 0;
    int sectionsThisFrame = 0;
    int i = startIdx;
    const int n = m_sections.size();
    for (; i < n; ++i) {
        auto &sec = m_sections[i];
        if (!sec || sec->hidden()) continue;
        const qreal top = sec->yPos();
        const qreal h = sec->actualHeight() > 0.0
                            ? sec->actualHeight()
                            : sec->estimatedHeight();
        const qreal bottom = top + qMax<qreal>(h, 1.0);
        if (bottom < windowTop || top > windowBottom) continue;
        if (sec->graphicsItem()) continue;

        QGraphicsItem *item = layoutSectionForController(i);
        if (!item) continue;
        ++layoutsThisFrame;
        ++sectionsThisFrame;

        const bool timerBudgetSpent =
            layoutsThisFrame > 0 && t.elapsed() >= kFrameBudgetMs;
        const bool sectionBudgetSpent =
            sectionsThisFrame >= kFrameBudgetSections;
        if ((timerBudgetSpent || sectionBudgetSpent) && i + 1 < n) {
            const int resumeIdx = i + 1;
            QPointer<ReadingView> guard(this);
            QTimer::singleShot(0, this, [guard, resumeIdx] {
                if (guard) guard->mountInitialWindowWithBudget(resumeIdx);
            });
            return;
        }
    }

    for (auto it = m_oldByShape.begin(); it != m_oldByShape.end(); ++it) {
        auto &old = it.value();
        QGraphicsItem *item = old->graphicsItem();
        if (!item) continue;
        old->setGraphicsItem(nullptr);
        if (item->scene()) item->scene()->removeItem(item);
        m_recyclePool->offer(it.key(), item);
    }
    m_oldByShape.clear();

    m_pendingSections.clear();
    m_mountInProgress = false;
    m_initialWindowDone = true;

    updateViewportMount();

    recomputeLayoutGeometry();
    qreal totalHeight = 0.0;
    for (const auto &sec : m_sections) {
        if (sec->hidden()) continue;
        totalHeight = qMax<qreal>(totalHeight,
                                   sec->yPos() + sec->estimatedHeight());
        if (auto *item = sec->graphicsItem()) {
            item->setPos(0, sec->yPos());
        }
    }
    s->setSceneRect(0, 0, m_contentWidth, qMax<qreal>(totalHeight, 1.0));

    Q_EMIT mountingFinished();
}

void ReadingView::updateViewportMount()
{
    if (!m_controller) return;
    const QRectF viewportScene =
        mapToScene(viewport() ? viewport()->rect() : QRect(0, 0, 0, 0))
            .boundingRect();
    qreal vpTop = viewportScene.top();
    qreal vpHeight = viewportScene.height();
    if (vpHeight <= 0) vpHeight = height() > 0 ? height() : 600.0;
    m_controller->updateMounted(vpTop, vpHeight);
}

void ReadingView::recomputeFoldVisibility()
{
    int hideUntilIndex = -1;
    int activeLevel = 0;

    for (int i = 0; i < m_sections.size(); ++i) {
        auto &sec = m_sections[i];
        if (!sec) continue;

        if (hideUntilIndex >= 0 && i < hideUntilIndex) {
            sec->setHidden(true);
        } else {
            sec->setHidden(false);
            hideUntilIndex = -1;
            activeLevel = 0;
        }

        if (!sec->hidden() && sec->headingLevel() > 0
            && sec->headingCollapsed()) {
            const int N = sec->headingLevel();
            int end = m_sections.size();
            for (int j = i + 1; j < m_sections.size(); ++j) {
                const auto &s2 = m_sections.at(j);
                if (s2 && s2->headingLevel() > 0 && s2->headingLevel() <= N) {
                    end = j;
                    break;
                }
            }
            hideUntilIndex = end;
            activeLevel = N;
            Q_UNUSED(activeLevel);
        }
    }
}

void ReadingView::recomputeLayoutGeometry()
{
    qreal y = 0.0;
    for (auto &sec : m_sections) {
        if (!sec) continue;
        if (sec->hidden()) {
            sec->setYPos(0.0);
            continue;
        }
        sec->setYPos(y);
        const qreal h = sec->actualHeight() > 0.0
                            ? sec->actualHeight()
                            : sec->estimatedHeight();
        y += h + kSectionVerticalGap;
    }
}

QVector<int> ReadingView::foldedHeadingLines() const
{
    QVector<int> out;
    for (const auto &sec : m_sections) {
        if (sec && sec->headingLevel() > 0 && sec->headingCollapsed())
            out.push_back(sec->sourceLine());
    }
    return out;
}

void ReadingView::setFoldedHeadingLines(const QVector<int> &lines)
{
    bool changed = false;
    for (auto &sec : m_sections) {
        if (!sec || sec->headingLevel() <= 0) continue;
        const bool should = lines.contains(sec->sourceLine());
        if (sec->headingCollapsed() != should) {
            sec->setHeadingCollapsed(should);
            changed = true;
        }
    }
    if (changed) {
        recomputeFoldVisibility();
        if (m_controller) {
            for (int i = 0; i < m_sections.size(); ++i) {
                const auto &sec = m_sections.at(i);
                if (sec && sec->hidden() && sec->graphicsItem()) {
                    m_controller->remountSection(i);
                }
            }
        }
        recomputeLayoutGeometry();

        auto *s = scene();
        if (s) {
            qreal totalHeight = 0.0;
            for (const auto &sec : m_sections) {
                if (sec->hidden()) continue;
                totalHeight = qMax<qreal>(
                    totalHeight, sec->yPos() + sec->estimatedHeight());
                if (auto *item = sec->graphicsItem())
                    item->setPos(0, sec->yPos());
            }
            s->setSceneRect(0, 0, m_contentWidth,
                             qMax<qreal>(totalHeight, 1.0));
        }
        if (m_initialWindowDone) updateViewportMount();
        Q_EMIT foldedHeadingsChanged();
    }
}

void ReadingView::toggleFold(int sectionIdx)
{
    if (sectionIdx < 0 || sectionIdx >= m_sections.size()) return;
    auto &sec = m_sections[sectionIdx];
    if (!sec || sec->headingLevel() <= 0) return;
    sec->setHeadingCollapsed(!sec->headingCollapsed());

    recomputeFoldVisibility();
    if (m_controller) {
        for (int i = 0; i < m_sections.size(); ++i) {
            const auto &s2 = m_sections.at(i);
            if (s2 && s2->hidden() && s2->graphicsItem()) {
                m_controller->remountSection(i);
            }
        }
    }
    recomputeLayoutGeometry();

    auto *s = scene();
    if (s) {
        qreal totalHeight = 0.0;
        for (const auto &s2 : m_sections) {
            if (s2->hidden()) continue;
            totalHeight = qMax<qreal>(totalHeight,
                                       s2->yPos() + s2->estimatedHeight());
            if (auto *item = s2->graphicsItem())
                item->setPos(0, s2->yPos());
        }
        s->setSceneRect(0, 0, m_contentWidth, qMax<qreal>(totalHeight, 1.0));
    }
    if (m_initialWindowDone) updateViewportMount();
    Q_EMIT foldedHeadingsChanged();
}

qreal ReadingView::visualLineSpacing() const
{
    const ParagraphStyle body =
        const_cast<StyleManager *>(m_styles.get())
            ->resolvedParagraphStyle(QStringLiteral("Body"));
    QFont font;
    if (body.hasFontFamily()) font.setFamily(body.fontFamily());
    if (body.hasFontSize()) font.setPointSizeF(body.fontSize());
    else font.setPointSizeF(14);
    const QFontMetricsF fm(font);
    return qMax<qreal>(fm.lineSpacing(), 1.0);
}

float ReadingView::scrollPositionVisualLine() const
{
    auto *vbar = verticalScrollBar();
    if (!vbar) return 0.0f;
    const qreal pixels = vbar->value();
    return static_cast<float>(pixels / visualLineSpacing());
}

void ReadingView::setScrollPositionVisualLine(float visualLine)
{
    auto *vbar = verticalScrollBar();
    if (!vbar) return;
    const qreal pixels = visualLine * visualLineSpacing();
    vbar->setValue(qRound(pixels));
}

QString ReadingView::wikiLinkTargetAt(const QPoint &viewportPos) const
{
    auto *s = scene();
    if (!s) return {};
    const QPointF scenePos = mapToScene(viewportPos);
    const QList<QGraphicsItem *> hits = s->items(scenePos);
    for (QGraphicsItem *it : hits) {
        auto *ti = qgraphicsitem_cast<QGraphicsTextItem *>(it);
        if (!ti) continue;
        const QPointF itemPos = ti->mapFromScene(scenePos);
        QTextDocument *doc = ti->document();
        if (!doc) continue;
        const int cursor = doc->documentLayout()->hitTest(
            itemPos, Qt::FuzzyHit);
        if (cursor < 0) continue;
        QTextCursor tc(doc);
        tc.setPosition(cursor);
        const QTextCharFormat cf = tc.charFormat();
        const QVariant v = cf.property(SpanRenderer::WikiLinkTargetProperty);
        if (v.isValid() && !v.toString().isEmpty())
            return v.toString();
    }
    return {};
}

int ReadingView::sectionIndexAt(const QPoint &viewportPos) const
{
    auto *s = scene();
    if (!s) return -1;
    const QPointF scenePos = mapToScene(viewportPos);
    const QList<QGraphicsItem *> hits = s->items(scenePos);
    for (QGraphicsItem *it : hits) {
        const QVariant v = it->data(kFoldArrowSectionIdxProperty);
        if (v.isValid()) {
            bool ok = false;
            const int idx = v.toInt(&ok);
            if (ok) return idx;
        }
    }
    return -1;
}

void ReadingView::zoomIn()
{
    if (!m_graphicsView) return;
    constexpr double kFactor = 1.1;
    m_graphicsView->scale(kFactor, kFactor);
    m_userZoom *= kFactor;
    Q_EMIT zoomChanged();
}

void ReadingView::zoomOut()
{
    if (!m_graphicsView) return;
    constexpr double kFactor = 1.0 / 1.1;
    m_graphicsView->scale(kFactor, kFactor);
    m_userZoom *= kFactor;
    Q_EMIT zoomChanged();
}

void ReadingView::resetZoom()
{
    if (!m_graphicsView) return;
    const double inv = 1.0 / m_userZoom;
    m_graphicsView->scale(inv, inv);
    m_userZoom = 1.0;
    Q_EMIT zoomChanged();
}

// --- MarkdownView overrides ---

void ReadingView::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_markoffDoc == doc) return;

    if (m_markoffDoc) {
        disconnect(m_markoffDoc, nullptr, this, nullptr);
    }

    m_markoffDoc = doc;

    if (!doc) {
        // Detached — clear the section layout so stale sections don't
        // persist after the document is gone.
        m_markdown.clear();
        m_lastMarkdown.clear();
        m_layout = std::make_unique<SectionLayout>();
        m_sections.clear();
        if (m_controller) m_controller->setSections(m_sections);
        return;
    }

    connect(doc, &Markoff::MarkoffDocument::parseUpdated,
            this, [this](const Markoff::Document *) { onCanonicalParseUpdated(); });
    connect(doc, &Markoff::MarkoffDocument::documentReloaded,
            this, &ReadingView::onCanonicalDocumentReloaded);

    // If a parse result is already available, build the section layout now.
    // Otherwise, wait for the first parseUpdated signal.
    if (!doc->parseIsPending() && doc->parsedDocument()) {
        m_markdown = doc->toMarkdown();
        rebuild();
    }
}

Markoff::MarkoffDocument *ReadingView::document() const
{
    return m_markoffDoc;
}

void ReadingView::onCanonicalParseUpdated()
{
    if (!m_markoffDoc) return;
    // Use the canonical text — ReadingView's internal worker re-parses it
    // to produce ReadingSection objects for the section-layout pipeline.
    m_markdown = m_markoffDoc->toMarkdown();
    rebuild();
}

void ReadingView::onCanonicalDocumentReloaded()
{
    // Tear down the current section layout; the next parseUpdated will
    // rebuild from the new canonical text.
    m_lastMarkdown.clear();
    m_layout = std::make_unique<SectionLayout>();
    m_sections.clear();
    if (m_controller) m_controller->setSections(m_sections);
}

void ReadingView::setViewTheme(const Markoff::Theme &) {}
void ReadingView::setViewResourceProvider(Markoff::ResourceProvider *) {}
void ReadingView::setViewLinkResolver(Markoff::LinkResolver *) {}

float ReadingView::scrollPosition() const
{
    return scrollPositionVisualLine();
}

void ReadingView::setScrollPosition(float visualLine)
{
    setScrollPositionVisualLine(visualLine);
}

QJsonObject ReadingView::ephemeralState() const
{
    QJsonObject j;
    j.insert(QStringLiteral("scroll"), scrollPosition());
    return j;
}

void ReadingView::setEphemeralState(const QJsonObject &j)
{
    setScrollPosition(static_cast<float>(
        j.value(QStringLiteral("scroll")).toDouble(0.0)));
}

Markoff::SearchAdapter *ReadingView::searchAdapter()
{
    return m_searchAdapter.get();
}

bool ReadingView::setReadOnly(bool ro)
{
    if (!ro) {
        qCWarning(lcReading) << "setReadOnly(false) refused on ReadingView — "
                                "reading mode is read-only by design";
        return false;
    }
    return true;
}

bool ReadingView::isReadOnly() const { return true; }

QVector<Markoff::FoldSpec> ReadingView::foldedHeadings() const
{
    QVector<Markoff::FoldSpec> out;
    for (const auto &sec : m_sections) {
        if (sec && sec->headingLevel() > 0 && sec->headingCollapsed()) {
            Markoff::FoldSpec fs;
            fs.line = sec->sourceLine();
            fs.level = sec->headingLevel();
            out.push_back(fs);
        }
    }
    return out;
}

void ReadingView::setFoldedHeadings(const QVector<Markoff::FoldSpec> &specs)
{
    QVector<int> lines;
    lines.reserve(specs.size());
    for (const auto &fs : specs) lines.push_back(fs.line);
    setFoldedHeadingLines(lines);
}

} // namespace Markoff::Reading
