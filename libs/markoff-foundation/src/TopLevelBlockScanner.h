// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>

namespace Markoff::Detail {

/// Half-open byte range [startByte, endByte) in UTF-8 body coordinates.
struct BlockByteRange {
    quint32 startByte = 0;
    quint32 endByte   = 0;
};

/// Enumerate top-level block byte ranges in a UTF-8 markdown body.
///
/// The algorithm mirrors libs/markoff-view-qml/src/BlockWalker.cpp:
///   - Skip leading blank lines.
///   - A fenced code block (line starting with ``` or ~~~ of three or
///     more) is one block, from the opening fence through the closing
///     fence (or to EOF if unclosed).
///   - Otherwise, a block is a run of contiguous non-blank lines,
///     terminated by a blank line or EOF.
///
/// The returned ranges are non-overlapping, in source order, and
/// strictly within `[0, body.size())`. They cover the *block content*
/// only; the inter-block blank-line separators are NOT included.
///
/// This scanner is foundation-internal. v0 maintains algorithmic
/// parity with view-qml's BlockWalker by convention; convergence is
/// asserted via the test cases below. Future unification (factoring
/// the scanner into a shared library) is tracked in the live-editing
/// plan.
QList<BlockByteRange> scanTopLevelBlockRanges(const QByteArray &body);

}  // namespace Markoff::Detail
