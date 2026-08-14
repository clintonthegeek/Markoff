// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include <QByteArray>
#include <QList>
#include <QString>

namespace Markoff::Canvas {

/**
 * Per-block delimiter reflow (spec §4.2, plan P2.1): maps a block's real
 * text (UTF-8 bytes, the document's one coordinate space — C4) onto the
 * layout text a QTextLayout actually renders, which OMITS hidden delimiter
 * runs (and substitutes U+2028 for '\n', folded into the same pass — a
 * QTextLayout does not break lines on '\n' itself).
 *
 * This is the one sanctioned second index space (layout QChar), and it
 * lives only here — never crosses the layout boundary into View.cpp, which
 * deals in bytes on one side and gets byte results back on the other.
 *
 * Authority: a CACHE, like BlockLayoutCache itself — built fresh from a
 * block's text + the set of omitted ranges every time either changes
 * (content edit or reveal-state change), never patched in place.
 */
class ProjectionMap {
public:
    /// Which way to resolve a byte position that falls strictly inside an
    /// omitted run (spec §4.2 snap rule): Left is "nearest kept boundary
    /// before the omission", Right is "nearest kept boundary after". An
    /// exact seam between two kept runs (nothing but an omission between
    /// them) always resolves toward the earlier run regardless of
    /// direction — that is the "no direction" default the spec calls out,
    /// and it falls out of the run lookup for free rather than needing a
    /// separate case.
    enum class SnapDirection { Left, Right };

    ProjectionMap() = default;

    /// Build from a block's raw UTF-8 text and the set of QChar ranges to
    /// omit, given in "full" space — i.e. QChar offsets into
    /// `QString::fromUtf8(blockBytes)` with '\n' *not yet* substituted
    /// (the substitution is 1 QChar for 1 QChar, so it does not shift any
    /// of SourceSpan's charOffset/charLength values). Ranges may be
    /// unsorted or overlapping; this sorts, clips to the text, and merges
    /// them before building the kept-run list.
    static ProjectionMap build(const QByteArray &blockBytes,
                                QList<std::pair<int, int>> omittedFullQCharRanges);

    /// The text a QTextLayout for this block should actually hold: full
    /// text with every omitted range removed and '\n' -> U+2028.
    const QString &layoutText() const { return m_layoutText; }

    /// Byte offset (this block's own coordinate space) -> layout QChar
    /// position. Well-defined everywhere; a byte landing inside an omitted
    /// run snaps per `dir`.
    int byteToLayoutQChar(int byteOffset, SnapDirection dir = SnapDirection::Left) const;
    /// Layout QChar position -> byte offset. Always well-defined: the
    /// layout text contains no omitted characters to be ambiguous about.
    int layoutQCharToByte(int layoutQChar) const;

    /// Same as byteToLayoutQChar, but starting from a "full" QChar position
    /// (pre-omission, post-\n-substitution — the space SourceSpan's
    /// charOffset/charLength already live in). Used to remap surviving
    /// spans' format ranges into layout space; content spans never overlap
    /// an omitted run, so `dir` is inert for that caller.
    int fullQCharToLayoutQChar(int fullQChar, SnapDirection dir = SnapDirection::Left) const;

private:
    struct KeptRun {
        int fullStart   = 0;
        int fullEnd     = 0;
        int layoutStart = 0;
        int layoutEnd() const { return layoutStart + (fullEnd - fullStart); }
    };

    QByteArray m_blockBytes;
    QString m_layoutText;
    std::vector<KeptRun> m_runs;  //!< sorted by fullStart (== sorted by layoutStart)
};

}  // namespace Markoff::Canvas
