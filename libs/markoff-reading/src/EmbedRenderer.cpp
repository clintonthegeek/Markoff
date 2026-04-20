// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include "markoff/reading/EmbedRenderer.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QWidget>

#include <markoff/vault/CachedMetadata.h>
#include <markoff/vault/DefaultLinkResolver.h>
#include <markoff/vault/LinkResolver.h>
#include <markoff/vault/MetadataCache.h>
#include <markoff/vault/MetadataParser.h>
#include <markoff/vault/ResourceProvider.h>

namespace Markoff::Reading {

namespace {

/// Fetch `CachedMetadata` for `path`. Prefers the `MetadataCache` entry
/// when one exists (fast path, reflects the worker-indexed state);
/// falls back to a synchronous `MetadataParser::parse` when the cache
/// is empty (first-touch / test-harness path). The sync fallback uses
/// an empty LinkResolver — heading/block extraction does not need link
/// resolution, so this is harmless.
Markoff::Vault::CachedMetadata
fetchMetadata(Markoff::Vault::MetadataCache *cache,
              Markoff::Vault::MetadataParser *parser,
              const QString &targetPath,
              const QByteArray &content)
{
    if (cache) {
        const auto cached = cache->getFileCache(targetPath);
        if (cached && (cached->headings || cached->blocks
                       || cached->sections)) {
            return *cached;
        }
    }
    if (!parser) return {};
    // Sync fallback — parser is expected to be pure; LinkResolver doesn't
    // participate in heading/block slicing so an empty resolver suffices.
    Markoff::Vault::DefaultLinkResolver resolver;
    return parser->parse(content, targetPath, resolver).cache;
}

/// Slice `content` from `start` up to (but not including) the next heading
/// of the same-or-lesser level after `start`. If no later heading exists,
/// returns the suffix from `start` onward.
QString sliceHeadingSection(
    const QString &content,
    const QVector<Markoff::Vault::HeadingCache> &headings,
    int headingIndex)
{
    const auto &h = headings[headingIndex];
    const int start = h.position.start.offset;
    const int myLevel = h.level;
    int end = content.size();
    for (int i = headingIndex + 1; i < headings.size(); ++i) {
        if (headings[i].level <= myLevel) {
            end = headings[i].position.start.offset;
            break;
        }
    }
    if (start < 0 || start >= content.size()) return {};
    return content.mid(start, end - start);
}

/// Slice `content` by the containing block for the given block-id.
///
/// The cached `BlockCache.position` in Phase-2 metadata only covers the
/// `^blkid` marker span itself, not the surrounding paragraph. We use
/// the marker's line as an anchor and expand outward to the nearest
/// blank lines (or file boundaries) to recover the full block body.
QString sliceBlock(const QString &content,
                   const Markoff::Vault::CachedMetadata &meta,
                   const QString &blockId)
{
    if (!meta.blocks) return {};
    const auto it = meta.blocks->constFind(blockId);
    if (it == meta.blocks->constEnd()) return {};

    const int anchor = it->position.start.offset;
    if (anchor < 0 || anchor >= content.size()) return {};

    // Walk to the start of the containing paragraph: back up through
    // non-blank lines, stopping at a blank line or BOF.
    int lineStart = anchor;
    while (lineStart > 0 && content.at(lineStart - 1) != QLatin1Char('\n'))
        --lineStart;
    int blockStart = lineStart;
    while (blockStart > 0) {
        // Walk back one line.
        int prevLineEnd = blockStart - 1; // on '\n'
        int prevLineStart = prevLineEnd;
        while (prevLineStart > 0
               && content.at(prevLineStart - 1) != QLatin1Char('\n'))
            --prevLineStart;
        // Is the previous line blank (only whitespace)?
        bool blank = true;
        for (int k = prevLineStart; k < prevLineEnd; ++k) {
            if (!content.at(k).isSpace()) {
                blank = false;
                break;
            }
        }
        if (blank) break;
        blockStart = prevLineStart;
    }

    // Walk forward to the end of the containing paragraph: advance
    // through non-blank lines until a blank line or EOF.
    int scan = anchor;
    while (scan < content.size() && content.at(scan) != QLatin1Char('\n'))
        ++scan;
    int blockEnd = scan; // up to (not including) the newline of the anchor line
    while (blockEnd < content.size()) {
        int nextLineStart = blockEnd + 1; // skip newline
        int nextLineEnd = nextLineStart;
        while (nextLineEnd < content.size()
               && content.at(nextLineEnd) != QLatin1Char('\n'))
            ++nextLineEnd;
        bool blank = true;
        for (int k = nextLineStart; k < nextLineEnd; ++k) {
            if (!content.at(k).isSpace()) {
                blank = false;
                break;
            }
        }
        if (blank) break;
        blockEnd = nextLineEnd;
    }

    if (blockStart >= blockEnd) return {};
    return content.mid(blockStart, blockEnd - blockStart);
}

/// Parse a raw `Target[#subpath]` wiki-embed reference into targetPath +
/// subpath. Mirrors Obsidian's wikilink linktext split: the first `#`
/// separates the target from the subpath; everything up to the first
/// `|` (alias separator) forms the linktext portion parsed here.
void splitWikiEmbed(const QString &raw, QString *target, QString *subpath)
{
    QString body = raw;
    const int pipe = body.indexOf(QLatin1Char('|'));
    if (pipe >= 0) body = body.left(pipe);
    const int hash = body.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        *target = body.left(hash);
        *subpath = body.mid(hash);
    } else {
        *target = body;
        subpath->clear();
    }
    // Auto-append .md for extensionless markdown-style targets.
    if (!target->isEmpty() && !target->contains(QLatin1Char('.'))) {
        *target += QStringLiteral(".md");
    }
}

} // namespace

EmbedRenderer::EmbedRenderer(Markoff::EmbedRegistry *registry,
                             Markoff::Vault::MetadataCache *cache,
                             Markoff::Vault::ResourceProvider *resources)
    : m_registry(registry), m_cache(cache), m_resources(resources)
{
}

void EmbedRenderer::setMetadataCache(Markoff::Vault::MetadataCache *cache)
{
    m_cache = cache;
}

void EmbedRenderer::setResources(Markoff::Vault::ResourceProvider *resources)
{
    m_resources = resources;
}

void EmbedRenderer::setMetadataParser(Markoff::Vault::MetadataParser *parser)
{
    m_parser = parser;
}

std::unique_ptr<Markoff::MarkdownRenderChild>
EmbedRenderer::render(const Markoff::EmbedRequest &req)
{
    // Depth guard — attempted 6th level lands on placeholder. The caller
    // contract (per docs/superpowers/research/2026-04-15-embed-depth-findings.md)
    // is: depth is incremented before the call, so `allow(5)` is false.
    if (!m_guard.allow(req.depth)) {
        auto child = std::make_unique<Markoff::MarkdownRenderChild>();
        child->setRenderedText(
            Markoff::EmbedDepthGuard::placeholder(req.targetPath));
        return child;
    }

    // Prefer registry dispatch for all extensions (including .md when a
    // host has registered a markdown factory). If no factory is present
    // for `.md`, fall through to the built-in `renderMarkdown` so the
    // ReadingView still shows *something* for a raw-note embed.
    if (m_registry) {
        if (auto child = m_registry->dispatch(req)) return child;
    }

    const QString ext = QFileInfo(req.targetPath).suffix().toLower();
    if (ext == QStringLiteral("md") || ext.isEmpty()) {
        return renderMarkdown(req);
    }

    // Unknown extension — stub placeholder (real factories land in Phase 5).
    auto child = std::make_unique<Markoff::MarkdownRenderChild>();
    child->setRenderedText(QStringLiteral("[unknown embed type: ") + ext
                           + QStringLiteral("]"));
    return child;
}

std::unique_ptr<Markoff::MarkdownRenderChild>
EmbedRenderer::renderMarkdown(const Markoff::EmbedRequest &req)
{
    auto child = std::make_unique<Markoff::MarkdownRenderChild>();

    // Pull raw markdown from the resource provider. Callers supply the
    // provider via `req.resources`; fall back to the EmbedRenderer's
    // member provider when the request did not carry one.
    Markoff::Vault::ResourceProvider *resources =
        req.resources ? req.resources : m_resources;
    if (!resources) {
        child->setRenderedText(QStringLiteral("[missing resources]"));
        return child;
    }
    const std::optional<QString> embed =
        resources->resolveEmbed(req.targetPath);
    if (!embed) {
        child->setRenderedText(QStringLiteral("[missing embed: ")
                               + req.targetPath + QStringLiteral("]"));
        return child;
    }
    const QString content = *embed;
    const QByteArray bytes = content.toUtf8();

    // Subpath resolution. Pick the slice we want from the content; the
    // nested-embed expansion pass below runs against this slice.
    const QString subpath = req.subpath;
    QString sliced;
    const Markoff::Vault::CachedMetadata meta =
        fetchMetadata(m_cache, m_parser, req.targetPath, bytes);

    if (subpath.isEmpty()) {
        sliced = content;
    } else if (subpath.startsWith(QStringLiteral("#^"))) {
        const QString blockId = subpath.mid(2);
        sliced = sliceBlock(content, meta, blockId);
    } else if (subpath.startsWith(QLatin1Char('#'))) {
        const QString headingName = subpath.mid(1);
        if (meta.headings) {
            const auto &headings = *meta.headings;
            for (int i = 0; i < headings.size(); ++i) {
                if (headings[i].heading == headingName) {
                    sliced = sliceHeadingSection(content, headings, i);
                    break;
                }
            }
        }
    }
    if (sliced.isEmpty()) {
        // Heading/block lookup miss OR cap-adjacent parent miss; surface
        // the subpath marker so the host can render a "missing" hint
        // instead of silently returning empty.
        child->setRenderedText(subpath.isEmpty() ? content : subpath);
        return child;
    }

    // Nested-embed expansion: walk the sliced content, and for each
    // `![[Target[#sub]]]` token, recursively render at depth+1 and
    // inline-substitute the rendered text. This is where the depth
    // guard fires for deep chains (Obsidian's JZ cap).
    QRegularExpression re(QStringLiteral(R"(!\[\[([^\]]+)\]\])"));
    QString out;
    out.reserve(sliced.size());
    int pos = 0;
    auto matchIt = re.globalMatch(sliced);
    while (matchIt.hasNext()) {
        const auto m = matchIt.next();
        out.append(sliced.mid(pos, m.capturedStart() - pos));
        QString nestedTarget;
        QString nestedSubpath;
        splitWikiEmbed(m.captured(1), &nestedTarget, &nestedSubpath);
        Markoff::EmbedRequest nestedReq{nestedTarget,
                                        nestedSubpath,
                                        resources,
                                        req.depth + 1};
        if (!m_guard.allow(nestedReq.depth)) {
            out.append(Markoff::EmbedDepthGuard::placeholder(
                nestedReq.targetPath));
        } else {
            // Prefer registry; fall back to self's renderMarkdown for
            // the `.md` case if registry has no factory registered.
            std::unique_ptr<Markoff::MarkdownRenderChild> nested;
            if (m_registry) nested = m_registry->dispatch(nestedReq);
            if (!nested) nested = renderMarkdown(nestedReq);
            out.append(nested->renderedText());
        }
        pos = m.capturedEnd();
    }
    out.append(sliced.mid(pos));
    child->setRenderedText(out);
    return child;
}

bool EmbedRenderer::renderInto(QWidget *parent,
                               const QString &targetPath,
                               const QString &subpath)
{
    if (!parent) return false;
    auto child = render({targetPath, subpath, m_resources, 1});
    if (!child) return false;
    child->mountInto(parent);
    return true;
}

namespace {

/// Wikilink-shim image factory. Rewrites an `![[foo.png]]` request into
/// the equivalent `![](foo.png)` markdown snippet so hosts that route
/// the rendered text through the SpanRenderer image path get the same
/// output as for native `![](...)` sources. Alias / subpath are
/// intentionally ignored — Obsidian's image-embed syntax does not use
/// subpath the same way markdown-embed does; the target path becomes
/// the img src directly.
Markoff::EmbedFactory imageWikilinkShim()
{
    return [](const Markoff::EmbedRequest &req)
        -> std::unique_ptr<Markoff::MarkdownRenderChild> {
        auto child = std::make_unique<Markoff::MarkdownRenderChild>();
        child->setRenderedText(QStringLiteral("![](") + req.targetPath
                               + QStringLiteral(")"));
        return child;
    };
}

/// Media-stub placeholder factory. The rendered text carries a
/// translator-friendly "<kind> preview not yet available — <filename>"
/// hint so hosts that route it through a text widget see a clear,
/// distinct card per media kind. `kind` is one of the translated
/// strings ("PDF", "Audio", "Video") — distinct prefixes are the
/// only distinguishing signal, so translators must keep them distinct
/// within a locale.
Markoff::EmbedFactory mediaPlaceholderFactory(const QString &kind)
{
    return [kind](const Markoff::EmbedRequest &req)
        -> std::unique_ptr<Markoff::MarkdownRenderChild> {
        auto child = std::make_unique<Markoff::MarkdownRenderChild>();
        child->setRenderedText(
            QCoreApplication::translate(
                "Markoff::Reading",
                "%1 preview not yet available — %2")
                .arg(kind, req.targetPath));
        return child;
    };
}

} // namespace

void registerBuiltinEmbedFactories(Markoff::EmbedRegistry &reg,
                                   EmbedRenderer &renderer)
{
    // .md — delegate to the recursive markdown renderer. The lambda
    // captures &renderer; callers must keep the renderer alive for the
    // lifetime of the registry (documented in EmbedRenderer.h).
    reg.registerExtension(
        QStringLiteral("md"),
        [&renderer](const Markoff::EmbedRequest &req)
            -> std::unique_ptr<Markoff::MarkdownRenderChild> {
            return renderer.renderMarkdown(req);
        });

    // Images — wikilink-shim to `![](path)`. Registers the common
    // Obsidian-supported raster + vector + webp extensions under one
    // shared factory.
    const auto imgFactory = imageWikilinkShim();
    for (const QString &ext : {QStringLiteral("png"), QStringLiteral("jpg"),
                               QStringLiteral("jpeg"), QStringLiteral("gif"),
                               QStringLiteral("svg"), QStringLiteral("webp")}) {
        reg.registerExtension(ext, imgFactory);
    }

    // Media stubs — per-media-kind placeholder factories with distinct
    // prefixes so different media types produce distinct rendered text.
    // Real preview widgets land in a future cluster; these are the
    // Obsidian-parity "preview not yet available" placeholders.
    const QString pdfKind = QCoreApplication::translate(
        "Markoff::Reading", "PDF");
    reg.registerExtension(QStringLiteral("pdf"),
                          mediaPlaceholderFactory(pdfKind));

    const QString audioKind = QCoreApplication::translate(
        "Markoff::Reading", "Audio");
    reg.registerExtension(QStringLiteral("mp3"),
                          mediaPlaceholderFactory(audioKind));
    reg.registerExtension(QStringLiteral("wav"),
                          mediaPlaceholderFactory(audioKind));

    const QString videoKind = QCoreApplication::translate(
        "Markoff::Reading", "Video");
    reg.registerExtension(QStringLiteral("mp4"),
                          mediaPlaceholderFactory(videoKind));
    reg.registerExtension(QStringLiteral("webm"),
                          mediaPlaceholderFactory(videoKind));
}

} // namespace Markoff::Reading
