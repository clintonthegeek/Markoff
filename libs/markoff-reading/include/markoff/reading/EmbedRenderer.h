// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#ifndef MARKOFF_READING_EMBEDRENDERER_H
#define MARKOFF_READING_EMBEDRENDERER_H

#include <QString>

#include <memory>

#include <markoff/EmbedDepthGuard.h>
#include <markoff/EmbedRegistry.h>
#include <markoff/MarkdownRenderChild.h>

class QWidget;

namespace Markoff::Vault {
class MetadataCache;
class MetadataParser;
class ResourceProvider;
} // namespace Markoff::Vault

namespace Markoff::Reading {

/// Per-embed mini-renderer for `![[Target]]`, `![[Target#heading]]` and
/// `![[Target#^blockid]]`. Resolves subpaths via `MetadataCache` when a
/// cache entry exists, otherwise falls back to a synchronous on-demand
/// parse via the injected `MetadataParser` (needed for tests and
/// first-touch fallback).
///
/// Depth is tracked via `Markoff::EmbedDepthGuard` — an attempted sixth
/// embed level ( `depth >= 5` ) produces the clickable placeholder child
/// via `EmbedDepthGuard::placeholder(target)`. Host widgets can read
/// `EmbedDepthGuard::placeholderTarget(target)` to wire an onClick
/// handler that opens `target` in a new pane (Obsidian parity).
///
/// Lifecycle: the returned `MarkdownRenderChild` is an owning unique_ptr
/// that host code mounts via `child->mountInto(parent)`. Hosts hold
/// the child via QPointer after mount to avoid dangling references on
/// SectionRecyclePool reclaim.
class EmbedRenderer
{
public:
    EmbedRenderer(Markoff::EmbedRegistry *registry,
                  Markoff::Vault::MetadataCache *cache,
                  Markoff::Vault::ResourceProvider *resources);

    /// Cluster J Phase 6 — late-bind the per-vault metadata cache and
    /// resource provider after construction. Hosts (e.g., HoverPopover)
    /// build the renderer eagerly at app start so registry-factory
    /// lambdas can capture `&renderer` once, then re-point the resource
    /// adapter on every vault open / close. Caller retains ownership;
    /// pass `nullptr` to clear.
    void setMetadataCache(Markoff::Vault::MetadataCache *cache);
    void setResources(Markoff::Vault::ResourceProvider *resources);

    /// Phase C1 addition — inject the `MetadataParser` used as the
    /// synchronous fallback when the cache has no entry for a target.
    /// Pre-C1, markoff-reading called a static `Corbomite::MetadataParser::parse`
    /// directly; C1 makes the parser injectable so standalone builds can
    /// get away with a no-op default.
    void setMetadataParser(Markoff::Vault::MetadataParser *parser);

    /// Resolve and render an embed request. Returns a non-null
    /// `MarkdownRenderChild` in all paths: on depth-cap-rejection the
    /// child carries the `EmbedDepthGuard::placeholder(...)` string; on
    /// unknown-extension the child carries a `[unknown embed type: X]`
    /// placeholder; on normal success the child's `renderedText()`
    /// carries the subpath-sliced markdown.
    std::unique_ptr<Markoff::MarkdownRenderChild>
    render(const Markoff::EmbedRequest &req);

    /// Convenience: render `targetPath#subpath` directly into an existing
    /// QWidget parent. Used by Phase 6 HoverPopover.
    bool renderInto(QWidget *parent,
                    const QString &targetPath,
                    const QString &subpath);

    /// Render the embedded markdown as a text slice (no registry
    /// dispatch). Used by the markdown-extension factory a host
    /// registers on our `EmbedRegistry`. Subpath resolution:
    /// - `"#^blockid"` → MetadataCache.blocks (or sync-parse fallback).
    /// - `"#heading"`  → MetadataCache.headings (or sync-parse fallback).
    /// - empty         → whole note.
    /// Returns a child whose `renderedText()` is the sliced markdown.
    std::unique_ptr<Markoff::MarkdownRenderChild>
    renderMarkdown(const Markoff::EmbedRequest &req);

private:
    Markoff::EmbedDepthGuard m_guard;
    Markoff::EmbedRegistry *m_registry;
    Markoff::Vault::MetadataCache *m_cache;
    Markoff::Vault::ResourceProvider *m_resources;
    Markoff::Vault::MetadataParser *m_parser = nullptr;
};

/// Cluster J phase 5 — populate `reg` with the built-in EmbedRegistry
/// factories ReadingView ships with:
///
/// - `.md` → delegates to `renderer.renderMarkdown(req)` (recursive embed
///   expansion with depth-guard).
/// - `.png`, `.jpg`, `.jpeg`, `.gif`, `.svg`, `.webp` → wikilink-shim
///   factory. Converts the wikilink `EmbedRequest` into an equivalent
///   `![](path)` markdown snippet and sets it as the rendered text; the
///   SpanRenderer image path consumes the snippet when the host routes
///   it back through the section-layout pipeline.
/// - `.pdf`, `.mp3`, `.wav`, `.mp4`, `.webm` → placeholder factory. The
///   MarkdownRenderChild's rendered text carries a "preview not yet
///   available — <filename>" hint scoped by media kind (PDF / Audio /
///   Video). Strings are translation-ready via `tr()`.
///
/// Callers retain ownership of `reg` + `renderer`. The factory lambdas
/// capture `&renderer` by reference — callers must keep the renderer
/// alive for as long as the registry is used. Returning unique handles
/// is out of scope; callers who need de-registration should use
/// `EmbedRegistry::registerExtension` directly.
void registerBuiltinEmbedFactories(Markoff::EmbedRegistry &reg,
                                   EmbedRenderer &renderer);

} // namespace Markoff::Reading

#endif // MARKOFF_READING_EMBEDRENDERER_H
