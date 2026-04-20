// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_READINGPIPELINE_H
#define CORBOMITE_READINGVIEW_READINGPIPELINE_H

#include "corbomite/readingview/ReadingSection.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <memory>

namespace Corbomite::ReadingView {

/// Parse → section-split driver. Phase 3a is synchronous; Phase 5 will
/// promote ≥ 10240-byte parses onto a worker thread.
class ReadingPipeline : public QObject
{
    Q_OBJECT

public:
    explicit ReadingPipeline(QObject *parent = nullptr);

    /// Synchronous parse + split. Returns section records with populated
    /// byte-offset `SourceRange`s relative to `markdown`.
    QVector<std::shared_ptr<ReadingSection>>
    splitIntoSections(const QString &markdown);

    /// Stateless: compare the frontmatter bytes between the leading
    /// `---\n…\n---\n` fences of `oldMarkdown` and `newMarkdown`. If
    /// exactly one document has frontmatter, or the bytes differ, the
    /// frontmatter is considered changed. Phase 4 uses this as a forcing
    /// trigger for sections with `usesFrontMatter=true`.
    static bool detectFrontmatterChange(const QString &oldMarkdown,
                                        const QString &newMarkdown);
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_READINGPIPELINE_H
