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
///   - A fenced code block opens with a line starting with exactly
///     three backticks (no leading spaces, no tildes — see view-qml
///     BlockWalker's `^```\s*(\S*)\s*$`). Optional info string after
///     the backticks. The block extends from the open-fence line
///     through the close-fence line (or to EOF if unclosed). The
///     close-fence is a line starting with exactly three backticks
///     followed only by whitespace (`^```\s*$`) — a line like
///     `` ```python `` is an open or mid-block, not a close.
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
