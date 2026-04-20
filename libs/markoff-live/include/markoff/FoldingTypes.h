// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_FOLDINGTYPES_H
#define MARKOFF_FOLDINGTYPES_H

#include <QList>
#include <QStringList>

namespace Markoff {

struct HeadingInfo; // fwd decl from markoff-parser

/// A fold region's identity. For headings, this is the hierarchy path
/// ["Intro", "Goals", "Non-goals"]. Later block types (lists, code
/// blocks) will use their own shape but reuse this type.
using FoldRegionKey = QStringList;

/// Compute the hierarchy path for each heading in document order.
/// Stripping rules: inline markdown removed (`**bold**` -> `bold`).
/// Duplicate siblings disambiguated with `#N` suffix starting at `#2`.
QList<FoldRegionKey> computeHeadingPaths(const QList<HeadingInfo> &headings);

/// Strip inline markdown delimiters from a heading's raw text. Public
/// so tests and host code can compute paths equivalently.
QString normalizeHeadingText(const QString &raw);

} // namespace Markoff

#endif // MARKOFF_FOLDINGTYPES_H
