// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <QByteArray>
#include <QtGlobal>

namespace Markoff::Live::Coordinates {

/// Number of Qt UTF-16 code units (QChars) spanned by the first
/// `byteOffset` UTF-8 bytes in `utf8`. Returns the total QChar count
/// if `byteOffset >= utf8.size()`. No allocation; O(byteOffset) scan.
MARKOFF_LIVE_RENDER_EXPORT
qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset);

/// Byte offset in `utf8` at which the `qtPos`-th UTF-16 code unit begins.
/// Returns `utf8.size()` if `qtPos` is past the end. No allocation;
/// O(qtPos) scan. For a surrogate pair (4-byte UTF-8 codepoint), both
/// surrogates map to the same 4-byte sequence's start byte.
MARKOFF_LIVE_RENDER_EXPORT
qsizetype qtPosToByte(const QByteArray &utf8, qsizetype qtPos);

}  // namespace Markoff::Live::Coordinates
