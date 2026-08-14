// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#pragma once

#include <functional>

#include <QPixmap>
#include <QString>

namespace Markoff::Canvas {

/// P5.4 image seam: consumer-provided lookup from an Image block's parsed
/// target (the `src` half of `![alt](src)`) to a ready-to-paint QPixmap.
/// A null return (or no lookup set at all) is a miss — View::paintEvent
/// treats both the same way: a placeholder box, not a crash or blank
/// space. Not a `Markoff::Vault::ResourceProvider` reuse: that interface
/// returns a `QUrl`/raw bytes, not a paint-ready pixmap, and this leaf has
/// no image-decoding step of its own to add — the consumer decides how
/// "target string in" becomes "pixmap out".
using ImageResourceLookup = std::function<QPixmap(const QString &target)>;

/// P5.4 mermaid seam: consumer-injected renderer for fenced code blocks
/// whose info-string language is "mermaid" (case-insensitive — the same
/// `CodeFenceInfo::language` field a syntax-highlight service would key
/// off). Not owned by the view; the consumer keeps it alive for as long as
/// it stays set on a `View`/`EditorWidget` (mirrors `setLinkService`'s
/// "reference, not owner" convention in this leaf).
class MermaidRenderer
{
public:
    virtual ~MermaidRenderer() = default;

    /// Render `source` (the mermaid block's fenced content, fence lines
    /// and info string already excluded) to a pixmap. A null return is a
    /// render failure — treated the same as "no renderer set": the block
    /// keeps painting its normal source text (P4.6 code-block styling),
    /// never a placeholder box. Mermaid has no placeholder-box requirement
    /// in the plan the way Image/Embed do; a miss here is the
    /// `CodeHighlighting` precedent instead: "a service miss renders
    /// plain monospace."
    virtual QPixmap render(const QString &source) const = 0;
};

}  // namespace Markoff::Canvas
