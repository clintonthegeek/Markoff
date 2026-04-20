// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_EMBEDRENDERER_H
#define CORBOMITE_READINGVIEW_EMBEDRENDERER_H

#include <QString>

#include <memory>

#include "corbomite/core/EmbedDepthGuard.h"
#include "corbomite/core/EmbedRegistry.h"
#include "corbomite/core/MarkdownRenderChild.h"

class QWidget;

namespace Corbomite {
class MetadataCache;
}
namespace Corbomite::Core {
class VaultResourceProvider;
}

namespace Corbomite::ReadingView {

/// Per-embed mini-renderer for `![[Target]]`, `![[Target#heading]]` and
/// `![[Target#^blockid]]`. Resolves subpaths via `MetadataCache` when a
/// cache entry exists, otherwise falls back to a synchronous on-demand
/// parse (needed for tests and first-touch fallback).
///
/// Depth is tracked via `Corbomite::Core::EmbedDepthGuard` — an attempted
/// sixth embed level ( `depth >= 5` ) produces the clickable placeholder
/// child via `EmbedDepthGuard::placeholder(target)`. Host widgets can
/// read `EmbedDepthGuard::placeholderTarget(target)` to wire an onClick
/// handler that opens `target` in a new pane (Obsidian parity).
///
/// Lifecycle: the returned `MarkdownRenderChild` is an owning unique_ptr
/// that host code mounts via `child->mountInto(parent)`. Hosts hold
/// the child via QPointer after mount to avoid dangling references on
/// SectionRecyclePool reclaim.
class EmbedRenderer
{
public:
    EmbedRenderer(Corbomite::Core::EmbedRegistry *registry,
                  Corbomite::MetadataCache *cache,
                  Corbomite::Core::VaultResourceProvider *resources);

    /// Cluster J Phase 6 — late-bind the per-vault metadata cache and
    /// resource provider after construction. Hosts (e.g., HoverPopover)
    /// build the renderer eagerly at app start so registry-factory
    /// lambdas can capture `&renderer` once, then re-point the resource
    /// adapter on every vault open / close. Caller retains ownership;
    /// pass `nullptr` to clear.
    void setMetadataCache(Corbomite::MetadataCache *cache);
    void setResources(Corbomite::Core::VaultResourceProvider *resources);

    /// Resolve and render an embed request. Returns a non-null
    /// `MarkdownRenderChild` in all paths: on depth-cap-rejection the
    /// child carries the `EmbedDepthGuard::placeholder(...)` string; on
    /// unknown-extension the child carries a `[unknown embed type: X]`
    /// placeholder; on normal success the child's `renderedText()`
    /// carries the subpath-sliced markdown.
    std::unique_ptr<Corbomite::Core::MarkdownRenderChild>
    render(const Corbomite::Core::EmbedRequest &req);

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
    std::unique_ptr<Corbomite::Core::MarkdownRenderChild>
    renderMarkdown(const Corbomite::Core::EmbedRequest &req);

private:
    Corbomite::Core::EmbedDepthGuard m_guard;
    Corbomite::Core::EmbedRegistry *m_registry;
    Corbomite::MetadataCache *m_cache;
    Corbomite::Core::VaultResourceProvider *m_resources;
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
void registerBuiltinEmbedFactories(Corbomite::Core::EmbedRegistry &reg,
                                   EmbedRenderer &renderer);

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_EMBEDRENDERER_H
