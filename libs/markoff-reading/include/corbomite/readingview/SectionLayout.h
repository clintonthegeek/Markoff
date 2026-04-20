// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_SECTIONLAYOUT_H
#define CORBOMITE_READINGVIEW_SECTIONLAYOUT_H

#include "corbomite/readingview/CodeBlockHighlighter.h"
#include "corbomite/readingview/VaultResourceProvider.h"

#include <QList>
#include <QObject>
#include <QString>

class QGraphicsItemGroup;

namespace Corbomite::ReadingView {

class ReadingSection;
class StyleManager;
class CodeBlockHighlighter;

/// Lay out a single `ReadingSection` into a mounted QGraphicsItem subtree.
/// Phase 3b: synchronous, simple-stacking. Eleven content types — headings,
/// paragraphs, code blocks, lists, horizontal rules, blockquotes, tables,
/// inline images, wiki-links, math (inline + display), and mermaid fenced
/// blocks.
///
/// Inline span styling is CharacterStyle-driven via `SpanRenderer` (see
/// src/SpanRenderer.h) — the Phase 3a `inlineToHtml` helper is gone.
class SectionLayout
{
public:
    struct Context {
        StyleManager *styles = nullptr;                // not owned
        Theme theme = Theme::Light;
        qreal contentWidth = 800.0;                    // pixel width for wrap
        VaultResourceProvider *vaultProvider = nullptr; // not owned

        /// Phase 6: when true, heading sections get a clickable gutter
        /// arrow (▶ collapsed, ▼ expanded) at the left edge. The arrow
        /// carries `sectionIndex` in its QGraphicsItem data under the
        /// ReadingView-internal property key so click-handling can map
        /// hits back to `m_sections[idx]`.
        bool headingCollapsedIndicator = true;
        int sectionIndex = -1;
    };

    SectionLayout();
    ~SectionLayout();

    /// Lay out `section` using its source markdown. Returns a
    /// `QGraphicsItemGroup` root that the caller mounts into a scene and
    /// owns. Returns `nullptr` on failure.
    QGraphicsItemGroup *layoutSection(ReadingSection &section,
                                      const QString &sectionMarkdown,
                                      const Context &ctx);

    /// Highlighters kept alive for the lifetime of this layout engine.
    const QList<CodeBlockHighlighter *> &ownedHighlighters() const
    {
        return m_highlighters;
    }

    /// Math text-object instances owned by this engine. Kept alive so the
    /// document-layout interface pointers remain valid across scene paints.
    const QList<QObject *> &ownedTextObjects() const { return m_textObjects; }

private:
    QList<CodeBlockHighlighter *> m_highlighters;
    QList<QObject *> m_textObjects;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_SECTIONLAYOUT_H
