// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectionMap.h"

#include <algorithm>

#include <markoff/core/TextUnits.h>

namespace coords = Markoff::TextUnits;

namespace Markoff::Canvas {

ProjectionMap ProjectionMap::build(const QByteArray &blockBytes,
                                    QList<std::pair<int, int>> omittedFullQCharRanges)
{
    ProjectionMap m;
    m.m_blockBytes = blockBytes;

    QString full = QString::fromUtf8(blockBytes);
    // 1 QChar for 1 QChar (spec §4.2 / BlockLayoutCache's T1 finding): does
    // not shift any span's charOffset, so this can happen before or after
    // the omission pass with no coordination needed between them.
    full.replace(QLatin1Char('\n'), QChar::LineSeparator);

    std::sort(omittedFullQCharRanges.begin(), omittedFullQCharRanges.end());
    std::vector<std::pair<int, int>> merged;  // [start, end)
    for (const auto &[start, length] : omittedFullQCharRanges) {
        const int s = qBound(0, start, int(full.size()));
        const int e = qBound(s, start + length, int(full.size()));
        if (e <= s)
            continue;
        if (!merged.empty() && s <= merged.back().second)
            merged.back().second = std::max(merged.back().second, e);
        else
            merged.push_back({s, e});
    }

    QString layout;
    layout.reserve(full.size());
    int cursor = 0;
    for (const auto &[s, e] : merged) {
        if (cursor < s) {
            m.m_runs.push_back({cursor, s, int(layout.size())});
            layout += full.sliced(cursor, s - cursor);
        }
        cursor = e;
    }
    if (cursor < full.size()) {
        m.m_runs.push_back({cursor, int(full.size()), int(layout.size())});
        layout += full.sliced(cursor, full.size() - cursor);
    }

    m.m_layoutText = std::move(layout);
    return m;
}

int ProjectionMap::byteToLayoutQChar(int byteOffset, SnapDirection dir) const
{
    const int fullQChar = int(coords::byteToQtPos(m_blockBytes, byteOffset));
    return fullQCharToLayoutQChar(fullQChar, dir);
}

int ProjectionMap::fullQCharToLayoutQChar(int fullQChar, SnapDirection dir) const
{
    if (m_runs.empty())
        return 0;

    // First run whose fullEnd >= fullQChar: the unique containing run when
    // fullQChar is inside a kept run, and (on an exact seam, where a run's
    // fullEnd coincides with the next run's fullStart) the earlier of the
    // two — which is the "no direction, snap left" default falling out of
    // the search for free.
    const auto it = std::lower_bound(
        m_runs.begin(), m_runs.end(), fullQChar,
        [](const KeptRun &r, int value) { return r.fullEnd < value; });

    if (it == m_runs.end()) {
        // Past every kept run (a trailing omitted range, or off the end of
        // the text entirely).
        return dir == SnapDirection::Left ? m_runs.back().layoutEnd()
                                           : int(m_layoutText.size());
    }
    if (fullQChar >= it->fullStart)
        return it->layoutStart + (fullQChar - it->fullStart) + 1;  // FALSIFY: off-by-one

    // Strictly inside the omitted gap before `it`.
    if (dir == SnapDirection::Right)
        return it->layoutStart;
    return (it == m_runs.begin()) ? 0 : (it - 1)->layoutEnd();
}

int ProjectionMap::layoutQCharToByte(int layoutQChar) const
{
    int fullQChar = 0;
    if (!m_runs.empty()) {
        // First run whose layoutEnd >= layoutQChar — same "earlier run wins
        // an exact seam" convention as fullQCharToLayoutQChar, so the two
        // are inverses of each other away from omitted runs.
        auto it = std::lower_bound(
            m_runs.begin(), m_runs.end(), layoutQChar,
            [](const KeptRun &r, int value) { return r.layoutEnd() < value; });
        if (it == m_runs.end())
            --it;
        const int clamped = qBound(it->layoutStart, layoutQChar, it->layoutEnd());
        fullQChar = it->fullStart + (clamped - it->layoutStart);
    }
    return int(coords::qtPosToByte(m_blockBytes, fullQChar));
}

}  // namespace Markoff::Canvas
