// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectionMap.h"

#include <algorithm>

#include <markoff/core/TextUnits.h>

namespace coords = Markoff::TextUnits;

namespace {

// P7.2g (F1 #9): dangerous/invisible/control codepoints get a visible
// substitute at layout-build time, 1 QChar for 1 QChar — same shape as the
// '\n' -> U+2028 substitution below, so it neither shifts any span's
// charOffset nor breaks the byte<->QChar projection (C4). Two treatments
// (task's own "decide + log"):
//  - C0 controls (except \t/\n — already allowed through by this leaf
//    elsewhere; \n is handled by the pre-existing substitution just below,
//    \t is ordinary content) plus DEL get the matching Unicode Control
//    Picture glyph (U+2400+code, DEL -> U+2421 — same choice CodeMirror's
//    own `placeholder()` makes), rendered as a normal glyph via the
//    block's existing foreground format. No extra paint pass needed.
//  - Everything else in the task's list — C1 controls, soft hyphen, ZWSP,
//    LRM/RLM, and, the safety-relevant subset, the bidi override/isolate
//    controls U+202D/E and U+2066-9, plus the BOM — has no legible
//    control-picture equivalent, so it is substituted with a Private-Use
//    sentinel (U+E000). Per DerivedBidiClass.txt, an unassigned Private-Use
//    codepoint defaults to bidi class L (strong left-to-right) — this
//    NEUTRALIZES the bidi-spoofing effect at the substitution point
//    itself (the dangerous character never reaches QTextLayout's bidi
//    algorithm at all), not merely covers it with paint on top. The
//    sentinel's own glyph is made transparent (BlockLayoutCache::
//    rebuildInline) and a custom hex-labeled box is painted over it
//    (View::paintEvent) — same "paint at this rect" idiom the leaf's task
//    checkboxes already use, since a translucent FormatRange background
//    alone cannot render a distinct hex label.
//
// Confirmed against CM's own `view/src/special-chars.ts` `Specials` regex:
// its literal range U+000A-U+001F technically includes LF, but CM's
// document model splits text into per-line chunks before any decoration
// ever runs, so a literal '\n' is never actually present in the substring
// scanned within one line — the practical exclusion matches this leaf's
// own \t-is-content / \n-already-substituted precedent, not a literal
// carve-out in CM's regex.
enum class SpecialTreatment { None, ControlPicture, BoxedHex };

SpecialTreatment classifySpecialChar(char16_t ch)
{
    if (ch == u'\t' || ch == u'\n')
        return SpecialTreatment::None;
    // FALSIFY (throwaway): C0 controls/DEL no longer get a control-picture glyph.
    if ((ch >= 0x80 && ch <= 0x9f)     // C1 controls
        || ch == 0x00ad                 // soft hyphen
        || ch == 0x200b                 // zero-width space
        || ch == 0x200e || ch == 0x200f // LRM / RLM
        || ch == 0x202d || ch == 0x202e // bidi override (LRO/RLO)
        || (ch >= 0x2066 && ch <= 0x2069) // bidi isolates (LRI/RLI/FSI/PDI)
        || ch == 0xfeff)                // BOM / zero-width no-break space
        return SpecialTreatment::BoxedHex;
    return SpecialTreatment::None;
}

QChar controlPictureGlyph(char16_t ch)
{
    return ch == 0x7f ? QChar(0x2421) : QChar(0x2400 + ch);
}

constexpr char16_t kBoxedHexSentinel = 0xE000;

}  // namespace

namespace Markoff::Canvas {

ProjectionMap ProjectionMap::build(const QByteArray &blockBytes,
                                    QList<std::pair<int, int>> omittedFullQCharRanges)
{
    ProjectionMap m;
    m.m_blockBytes = blockBytes;

    QString full = QString::fromUtf8(blockBytes);
    // 1 QChar for 1 QChar (spec §4.2 / BlockLayoutCache's T1 finding): does
    // not shift any span's charOffset, so this can happen before or after
    // the omission pass with no coordination needed between them. Folded
    // into the same single scan as the P7.2g substitutions below (both are
    // 1:1, so order between them doesn't matter — combined here rather
    // than as a second `QString::replace` pass over the same text).
    QList<std::pair<int, int>> specialFullPositions;  // fullQChar -> codepoint (BoxedHex only)
    for (int i = 0; i < full.size(); ++i) {
        const char16_t ch = full.at(i).unicode();
        if (ch == u'\n') {
            full[i] = QChar::LineSeparator;
            continue;
        }
        switch (classifySpecialChar(ch)) {
        case SpecialTreatment::ControlPicture:
            full[i] = controlPictureGlyph(ch);
            break;
        case SpecialTreatment::BoxedHex:
            specialFullPositions.push_back({i, int(ch)});
            full[i] = QChar(kBoxedHexSentinel);
            break;
        case SpecialTreatment::None:
            break;
        }
    }

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

    // P7.2g: remap each BoxedHex sentinel's "full" position into layout
    // space now that m_runs/m_layoutText are built. SnapDirection::Right
    // (arbitrary — a sentinel is ordinary content, never itself an omitted
    // delimiter, so it always lands exactly on a kept run) plus a landed-
    // on-the-sentinel guard: if a dangerous character happened to sit
    // inside a still-hidden delimiter run (e.g. inside a link's hidden
    // URL), the character it substituted is swallowed by the omission —
    // correctly so, nothing to box until it's revealed — rather than
    // painting a box at the wrong QChar.
    for (const auto &[fullPos, codepoint] : specialFullPositions) {
        const int layoutPos = m.fullQCharToLayoutQChar(fullPos, SnapDirection::Right);
        if (layoutPos >= 0 && layoutPos < m.m_layoutText.size()
            && m.m_layoutText.at(layoutPos).unicode() == kBoxedHexSentinel) {
            m.m_specialCharBoxes.push_back({layoutPos, codepoint});
        }
    }

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
        return it->layoutStart + (fullQChar - it->fullStart);

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
