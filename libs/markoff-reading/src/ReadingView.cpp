// SPDX-License-Identifier: GPL-3.0-or-later
#include "corbomite/readingview/ReadingView.h"

#include "MermaidRenderer.h"
#include "SpanRenderer.h"
#include "corbomite/readingview/CodeBlockHighlighter.h"
#include "corbomite/readingview/ReadingParseWorker.h"
#include "corbomite/readingview/ReadingPipeline.h"
#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingViewConstants.h"
#include "corbomite/readingview/SectionLayout.h"
#include "corbomite/readingview/SectionRecyclePool.h"
#include "corbomite/readingview/VaultResourceProvider.h"
#include "corbomite/readingview/VirtualScrollController.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <jkqtmathtext/jkqtmathtext.h>

#include <QAbstractTextDocumentLayout>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

namespace Corbomite::ReadingView {

namespace {

constexpr qreal kSectionVerticalGap = 4.0;

} // namespace

ReadingView::ReadingView(QWidget *parent)
    : QGraphicsView(parent)
    , m_pipeline(std::make_unique<ReadingPipeline>(this))
    , m_layout(std::make_unique<SectionLayout>())
    , m_styles(std::unique_ptr<StyleManager>(
          StyleManager::makeObsidianDefault(Theme::Light)))
    , m_recyclePool(std::make_unique<SectionRecyclePool>())
    , m_worker(std::make_unique<ReadingParseWorker>())
    , m_controller(std::make_unique<VirtualScrollController>(this))
    , m_codeBlockRegistry(
          std::make_unique<Corbomite::Core::CodeBlockProcessorRegistry>())
{
    // Cluster J phase 5 — seed the plugin-reachable code-block dispatch
    // surface. Built-in processors land at construction so host code
    // (tests + later plugin layer) sees a populated registry from the
    // moment the widget exists. SectionLayout continues to own the
    // graphics-item emission for the mermaid/math/syntax paths; this
    // registry is a parallel contract, not a replacement.
    registerBuiltinCodeBlockProcessors();
    // Worker's parseFinished emits from its own thread; Qt::QueuedConnection
    // hops to ours so the UI mount loop stays on the main thread.
    connect(m_worker.get(), &ReadingParseWorker::parseFinished,
            this, &ReadingView::onParseFinished,
            Qt::QueuedConnection);
    auto *scene = new QGraphicsScene(this);
    setScene(scene);
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setMouseTracking(true);

    // Wire the controller's callbacks. The controller never owns items or
    // sections — it just asks us to build/release them.
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
        if (!m_pendingHoverTarget.isEmpty())
            Q_EMIT wikiLinkHovered(m_pendingHoverTarget);
    });

    if (auto *vbar = verticalScrollBar()) {
        connect(vbar, &QScrollBar::valueChanged, this, [this] {
            Q_EMIT scrollPositionVisualLineChanged(scrollPositionVisualLine());
            // Phase 6: re-evaluate the mounted window on scroll. Scroll-mounts
            // happen on-demand (no frame-budget throttling — typical scroll
            // mounts are <5 sections at a time).
            if (m_initialWindowDone)
                updateViewportMount();
        });
    }
}

ReadingView::~ReadingView() = default;

Corbomite::Core::CodeBlockProcessorRegistry *
ReadingView::codeBlockProcessorRegistry()
{
    return m_codeBlockRegistry.get();
}

const Corbomite::Core::CodeBlockProcessorRegistry *
ReadingView::codeBlockProcessorRegistry() const
{
    return m_codeBlockRegistry.get();
}

void ReadingView::registerBuiltinCodeBlockProcessors()
{
    if (!m_codeBlockRegistry) return;

    // Mermaid: delegate to the existing Rust-FFI bridge in MermaidRenderer.
    // `renderSvg` returning non-empty is our signal that the bridge
    // handled the source. Graphics-item emission still lives in
    // SectionLayout's BlockKind::Mermaid branch (same `renderSvg` call);
    // routing through the registry here gives plugin authors and
    // downstream hosts a stable hook.
    m_codeBlockRegistry->registerLanguage(
        QStringLiteral("mermaid"),
        [](const QString &source,
           void * /*node*/,
           const Corbomite::Core::CodeBlockContext & /*ctx*/) -> bool {
            const QByteArray svg = MermaidRenderer::renderSvg(source);
            return !svg.isEmpty();
        });

    // Math: delegate to JKQTMathText. We only ask the parser to consume
    // the LaTeX source and report success. SectionLayout's display-math
    // branch runs the full render. Registering both `math` and `latex`
    // gives plugin-style language keys the same handler.
    auto mathProcessor =
        [](const QString &source,
           void * /*node*/,
           const Corbomite::Core::CodeBlockContext & /*ctx*/) -> bool {
            JKQTMathText mt;
            mt.parse(source);
            return !source.trimmed().isEmpty();
        };
    m_codeBlockRegistry->registerLanguage(QStringLiteral("math"),
                                          mathProcessor);
    m_codeBlockRegistry->registerLanguage(QStringLiteral("latex"),
                                          mathProcessor);

    // Default syntax-highlighting fallback: Obsidian-style "no per-language
    // processor matched" path. The registry currently dispatches by exact
    // language key — the `default` entry gives plugin callers a shorthand
    // that exercises the KF6::SyntaxHighlighting repository. (Wildcard
    // fallthrough at the registry layer is not in this phase's scope.)
    m_codeBlockRegistry->registerLanguage(
        QStringLiteral("default"),
        [](const QString & /*source*/,
           void * /*node*/,
           const Corbomite::Core::CodeBlockContext & /*ctx*/) -> bool {
            // Theme resolution happens in SectionLayout; here we only
            // exercise the bridge by constructing a highlighter and
            // asking it to adopt the light theme. Construction touches
            // the KSyntaxHighlighting repository singleton.
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
    // Layout context changed; rendered-shape cache would compare bogusly.
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
    // Styles changed — every mounted item is wrong now.
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

void ReadingView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_initialWindowDone)
        updateViewportMount();
}

void ReadingView::rebuild()
{
    auto *s = scene();
    if (!s) return;

    // Phase 5: parse gate. Notes at or above `kAsyncParseThresholdBytes`
    // (10240) go to the worker; smaller notes parse sync on this thread.
    // Either way the mount loop is frame-budgeted.
    //
    // `toUtf8().size()` is the byte length Obsidian's threshold is specified
    // in. QString::size() would count UTF-16 code units — slightly different
    // for notes with multi-byte characters, enough to push an edge-case note
    // over the threshold in one direction on the wire and the other in our
    // check. Matching the contract means matching on UTF-8 bytes.
    const int byteLen = m_markdown.toUtf8().size();
    const quint64 requestId = ++m_requestIdCounter;

    if (byteLen >= kAsyncParseThresholdBytes) {
        // Align the worker's internal latest-id with ours so its coalescing
        // matches the UI-level coalescing (belt-and-suspenders).
        while (m_worker->bumpRequestId() < requestId) { /* catch up */ }
        m_worker->parseAsync(m_markdown, requestId);
        // Mount happens when parseFinished fires. Return now so the UI
        // thread stays free.
        return;
    }

    // Sync path — < 10240 bytes.
    auto newSections = m_worker->parseSync(m_markdown);
    m_lastRequestIdHandled = requestId;
    beginMount(std::move(newSections));
}

void ReadingView::onParseFinished(
    quint64 requestId,
    QVector<std::shared_ptr<ReadingSection>> sections)
{
    // UI-level coalescing: ignore anything older than what we've already
    // seen, and anything older than the latest requestId counter (the
    // worker does its own check, but queued signals already in flight
    // when a newer parseAsync fires can still arrive here).
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

    // Build an index of old sections by their renderedShape. Multiple old
    // sections may share a shape; QMultiHash resolves one at a time via
    // `take`.
    m_oldByShape.clear();
    for (auto &old : m_sections) {
        const QByteArray key = old->renderedShape();
        if (!key.isEmpty() && old->graphicsItem() != nullptr)
            m_oldByShape.insert(key, old);
    }

    // Detach every known old section item from the scene BEFORE clearing
    // the scene, so we don't double-delete items we plan to reuse.
    for (auto &old : m_sections) {
        if (auto *item = old->graphicsItem()) {
            if (item->scene() == s)
                s->removeItem(item);
        }
    }
    // Anything still on the scene is orphan/stray — clear deletes it.
    s->clear();

    // Phase 6: the pipeline populated `headingLevel`, `sourceLine`, and
    // `estimatedHeight`. We restore any prior fold state keyed by source
    // line before geometry is computed.
    //
    // Snapshot previous fold state before we swap in the new section list.
    QVector<int> previouslyFolded = foldedHeadings();

    m_pendingSections = std::move(newSections);
    m_controller->setSections({}); // release any stale bookkeeping
    m_sections = m_pendingSections; // publish immediately — controller
                                    // consumes the same shared_ptrs.
    m_lastMarkdown = m_markdown;
    m_initialWindowDone = false;
    m_mountInProgress = true;

    // Re-apply fold state from the previous parse, then compute hidden
    // ranges and cumulative Y positions from estimated heights. QSet
    // keeps the reload O(n) for notes with hundreds of folded headings.
    if (!previouslyFolded.isEmpty()) {
        QSet<int> foldedSet;
        for (int line : previouslyFolded) foldedSet.insert(line);
        for (auto &sec : m_sections) {
            sec->setHeadingCollapsed(foldedSet.contains(sec->sourceLine()));
        }
    }
    recomputeFoldVisibility();
    recomputeLayoutGeometry();

    // Seed the scene rect from estimates so the scrollbar is correctly
    // sized before any section is mounted.
    qreal totalHeight = 0.0;
    for (const auto &sec : m_sections) {
        if (sec->hidden()) continue;
        totalHeight = qMax<qreal>(totalHeight,
                                   sec->yPos() + sec->estimatedHeight());
    }
    s->setSceneRect(0, 0, m_contentWidth, qMax<qreal>(totalHeight, 1.0));

    m_controller->setSections(m_sections);
    // Initial-mount window = viewport. Under offscreen test conditions the
    // viewport size may be (0, 0) until resizeEvent fires; the controller
    // degrades to a 400-px fallback window so we still mount something.
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
        // Already mounted (shouldn't normally happen — controller tracks
        // mounted set). Defensively re-attach to the scene.
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
        ctx.headingCollapsedIndicator = true;
        ctx.sectionIndex = sectionIdx;

        auto *group = m_layout->layoutSection(*sec, md, ctx);
        if (!group) return nullptr;
        item = group;
    }

    sec->setGraphicsItem(item);
    item->setPos(0, sec->yPos());
    if (!item->scene()) s->addItem(item);

    // Track actual height — first-layout overwrites the estimate so scroll
    // math converges to reality for already-mounted sections. Unmounted
    // sections keep their estimates.
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

    // Drop into the recycle pool keyed by shape so scroll-back re-mounts
    // get pointer-identical items.
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

    // The controller itself computes the desired set; we just ensure we
    // drive it under the frame-budget for the initial pass. When the
    // viewport is large + the window covers many sections we may need to
    // yield to keep any single frame under 5 ms. We manually loop by
    // incrementally expanding the "budget-allowed" section set: on each
    // frame we mount up to the min(kFrameBudgetSections, wall-budget)
    // desired sections, yield, and resume.

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
        if (sec->graphicsItem()) continue; // already mounted

        QGraphicsItem *item = layoutSectionForController(i);
        if (!item) continue;
        // Tell the controller we own this mount so future scrolls see it in
        // `mountedIndices()`. The controller's sections list must match
        // ours; we just mark the entry mounted by calling updateMounted
        // after loop completion. (We instead directly insert by exercising
        // the controller's lifecycle: first unmount, then mount.)
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

    // Finalise: drain leftover old-by-shape entries into the recycle pool
    // for next reparse. (Items tied to sections that weren't mounted
    // because they were outside the window also go here.)
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

    // Now ask the controller to consume our current mounted set. Because
    // the controller's `updateMounted` will see items already on the scene
    // as "to-be-mounted" (desired intersects window, item already has a
    // graphicsItem), our `layoutSectionForController` bails out cleanly.
    updateViewportMount();

    // Post-layout scene rect rebase — actual heights may differ from
    // estimates; recompute Y positions before final emit.
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
    // Walk the section list. When a section has `headingCollapsed = true`,
    // hide every subsequent section until we hit a heading at the same or
    // shallower level. Outer folds override inner ones.
    int hideUntilIndex = -1;          // exclusive end of hide range
    int activeLevel = 0;              // level of the outer fold

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
            // Find end index: first j > i with level != 0 and level <= N.
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

QVector<int> ReadingView::foldedHeadings() const
{
    QVector<int> out;
    for (const auto &sec : m_sections) {
        if (sec && sec->headingLevel() > 0 && sec->headingCollapsed())
            out.push_back(sec->sourceLine());
    }
    return out;
}

void ReadingView::setFoldedHeadings(const QVector<int> &lines)
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
        // Fold state affects visibility + cumulative Y. A section that was
        // mounted but is now hidden must be released; a section that was
        // hidden but is now in-viewport should mount.
        recomputeFoldVisibility();
        // Unmount any currently-mounted hidden sections before rebasing
        // geometry so the controller's mounted set stays consistent.
        if (m_controller) {
            for (int i = 0; i < m_sections.size(); ++i) {
                const auto &sec = m_sections.at(i);
                if (sec && sec->hidden() && sec->graphicsItem()) {
                    // The controller owns the mounted-set bookkeeping —
                    // ask it to release via remountSection (which calls
                    // releaseOne and then re-evaluates). For a hidden
                    // section, re-evaluation will not remount it.
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
    // Walk all items at this point and look for a QGraphicsTextItem whose
    // document fragment under the hit position carries the wiki-link target
    // property.
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
    // Look for a gutter-arrow item: a QGraphicsItem carrying the fold-
    // arrow property. The property value is the section index.
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

void ReadingView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Phase 6: gutter-arrow click toggles fold. Takes precedence over
        // wiki-link clicks (the arrow lives in the heading's left margin
        // and doesn't overlap link text).
        const int foldIdx = sectionIndexAt(event->pos());
        if (foldIdx >= 0) {
            toggleFold(foldIdx);
            event->accept();
            return;
        }
        const QString target = wikiLinkTargetAt(event->pos());
        if (!target.isEmpty()) {
            Q_EMIT wikiLinkActivated(target);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void ReadingView::mouseMoveEvent(QMouseEvent *event)
{
    const QString target = wikiLinkTargetAt(event->pos());
    if (target != m_pendingHoverTarget) {
        m_pendingHoverTarget = target;
        if (!target.isEmpty())
            m_hoverTimer->start();
        else
            m_hoverTimer->stop();
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ReadingView::zoomIn()
{
    constexpr double kFactor = 1.1;
    scale(kFactor, kFactor);
    m_userZoom *= kFactor;
    Q_EMIT zoomChanged();
}

void ReadingView::zoomOut()
{
    constexpr double kFactor = 1.0 / 1.1;
    scale(kFactor, kFactor);
    m_userZoom *= kFactor;
    Q_EMIT zoomChanged();
}

void ReadingView::resetZoom()
{
    const double inv = 1.0 / m_userZoom;
    scale(inv, inv);
    m_userZoom = 1.0;
    Q_EMIT zoomChanged();
}

} // namespace Corbomite::ReadingView
