// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_FOLDINGMODEL_H
#define MARKOFF_FOLDINGMODEL_H

#include <markoff/FoldingTypes.h>
#include <markoff-parser/Document.h>
#include <QObject>
#include <QSet>
#include <QList>
#include <QJsonObject>

namespace Markoff {

/// Owns the set of folded heading paths. Pure data, no widgets. Fed by
/// `Editor` from the `headingsChanged` reparse signal; consulted by
/// `SceneCoordinator` for item visibility and by `FoldGutter` for paint.
class FoldingModel : public QObject {
    Q_OBJECT
public:
    struct HeadingEntry {
        FoldRegionKey path;
        HeadingInfo info;
    };

    explicit FoldingModel(QObject *parent = nullptr);

    // --- Query ---
    bool isFolded(const FoldRegionKey &path) const;
    QList<FoldRegionKey> foldedPaths() const;
    QList<FoldRegionKey> allPaths() const;
    const QList<HeadingEntry> &headings() const { return m_headings; }

    /// True if `path` is folded OR any of its ancestor prefixes is folded.
    /// Used to decide item visibility.
    bool isHiddenByFold(const FoldRegionKey &path) const;

    // --- Individual mutation ---
    void fold(const FoldRegionKey &path);
    void unfold(const FoldRegionKey &path);
    void toggle(const FoldRegionKey &path);

    // --- Bulk mutation (implemented in Task 3) ---
    void foldAll();
    void unfoldAll();
    void foldAllAtLevel(int level);
    void unfoldAllAtLevel(int level);
    void foldLevel(int n);
    void unfoldLevel(int n);

    // --- Persistence (Task 4) ---
    QJsonObject serialize() const;
    void restore(const QJsonObject &);

    // --- Reparse reconcile (Task 5) ---
    void reconcile(const QList<HeadingInfo> &newHeadings);

    /// Test-only: seed the heading cache without going through reconcile.
    void setHeadingsForTesting(QList<HeadingEntry> h) { m_headings = std::move(h); }

    /// Walk `path` from root and unfold any folded prefix. Returns the
    /// prefixes actually unfolded (empty if none were folded). Used by
    /// auto-unfold on navigation and find.
    QList<FoldRegionKey> unfoldAncestors(const FoldRegionKey &path);

Q_SIGNALS:
    void foldStateChanged();

private:
    QSet<FoldRegionKey> m_folded;
    QList<HeadingEntry> m_headings;
};

} // namespace Markoff

#endif // MARKOFF_FOLDINGMODEL_H
