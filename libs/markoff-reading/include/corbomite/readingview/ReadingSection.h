// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_READINGSECTION_H
#define CORBOMITE_READINGVIEW_READINGSECTION_H

#include <QByteArray>

class QGraphicsItem;

namespace Corbomite::ReadingView {

/// A rendered unit of the reading pipeline — a contiguous span of source
/// markdown bounded by heading boundaries (or by document / frontmatter
/// bounds). One `QGraphicsItem` subtree per mounted section. Phase 4 uses
/// `renderedShape()` as the recycling key.
class ReadingSection
{
public:
    struct SourceRange { int from = 0; int to = 0; };

    ReadingSection();
    ~ReadingSection();

    SourceRange sourceRange() const { return m_sourceRange; }
    void setSourceRange(SourceRange range) { m_sourceRange = range; }

    // Heading metadata (if this section starts with a heading).
    int headingLevel() const { return m_headingLevel; }  // 0 = no heading
    void setHeadingLevel(int level) { m_headingLevel = level; }

    bool headingCollapsed() const { return m_headingCollapsed; }
    void setHeadingCollapsed(bool collapsed) { m_headingCollapsed = collapsed; }

    bool usesFrontMatter() const { return m_usesFrontMatter; }
    void setUsesFrontMatter(bool uses) { m_usesFrontMatter = uses; }

    bool isFrontMatterSection() const { return m_isFrontMatter; }
    void setIsFrontMatterSection(bool on) { m_isFrontMatter = on; }

    // Recycle key — populated by SectionLayout; Phase 4 will use this.
    QByteArray renderedShape() const { return m_renderedShape; }
    void setRenderedShape(const QByteArray &shape) { m_renderedShape = shape; }

    // The QGraphicsItem subtree for this section when mounted.
    QGraphicsItem *graphicsItem() const { return m_graphicsItem; }
    void setGraphicsItem(QGraphicsItem *item) { m_graphicsItem = item; }

    // Phase 6 — virtual-scroll geometry + fold persistence.
    //
    // `sourceLine` is the 0-based line index in the source markdown where
    // this section starts. Used as the persistence key for
    // `foldedHeadings`. For the frontmatter section sourceLine is 0; for
    // body sections without a heading sourceLine is the line after the
    // frontmatter block.
    int sourceLine() const { return m_sourceLine; }
    void setSourceLine(int line) { m_sourceLine = line; }

    // Estimated height — seeded pre-layout from a line-count heuristic so
    // the scene rect can be sized before any section is mounted. Updated
    // to the actual laid-out height once the section has been mounted.
    qreal estimatedHeight() const { return m_estimatedHeight; }
    void setEstimatedHeight(qreal h) { m_estimatedHeight = h; }

    qreal actualHeight() const { return m_actualHeight; }
    void setActualHeight(qreal h) { m_actualHeight = h; }

    // Best-guess vertical offset in the scene. Populated pre-mount as a
    // cumulative sum of estimated heights of visible (non-hidden) sections
    // above this one, then rebased post-mount from actual heights.
    qreal yPos() const { return m_yPos; }
    void setYPos(qreal y) { m_yPos = y; }

    // `hidden` is computed per-mount by the fold engine. A hidden section
    // contributes zero to the scene rect and is never mounted by the
    // virtual-scroll controller.
    bool hidden() const { return m_hidden; }
    void setHidden(bool h) { m_hidden = h; }

private:
    SourceRange m_sourceRange{};
    int m_headingLevel = 0;
    bool m_headingCollapsed = false;
    bool m_usesFrontMatter = false;
    bool m_isFrontMatter = false;
    QByteArray m_renderedShape;
    QGraphicsItem *m_graphicsItem = nullptr;

    int m_sourceLine = 0;
    qreal m_estimatedHeight = 0.0;
    qreal m_actualHeight = 0.0;
    qreal m_yPos = 0.0;
    bool m_hidden = false;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_READINGSECTION_H
