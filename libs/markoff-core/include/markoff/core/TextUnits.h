// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <markoff/core/MarkoffCoreExport.h>

/// Conversions between the two text units every view has to straddle: the
/// document's UTF-8 **byte** offsets and Qt's UTF-16 **QChar** (code-unit)
/// indices. Pure, allocation-free, no Qt-widget or view dependency; the one
/// copy, consumed by markoff-live and markoff-canvas (promoted at P1.2 —
/// each leaf had carried an identical fork).
///
/// **Always call these against a block's own text** (`doc.blockText(id)`),
/// never against a layout's `text()`: the canvas layout string substitutes
/// U+2028 for '\n', which is 1 QChar but not 1 byte, so round-tripping
/// through the layout string is wrong by 2 bytes per preceding newline
/// (canvas spike spec §9, T1 finding). Since that substitution is
/// 1 QChar → 1 QChar, a QChar index computed here is valid for both the
/// raw block text and the layout text.
namespace Markoff::TextUnits {

/// Number of Qt UTF-16 code units (QChars) spanned by the first
/// `byteOffset` UTF-8 bytes in `utf8`. Returns the total QChar count if
/// `byteOffset >= utf8.size()`. O(byteOffset) scan, no allocation.
/// Malformed lead bytes are treated as one byte each rather than throwing
/// the walk off.
MARKOFF_CORE_EXPORT
qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset);

/// Byte offset in `utf8` at which the `qtPos`-th UTF-16 code unit begins.
/// Returns `utf8.size()` if `qtPos` is past the end. O(qtPos) scan, no
/// allocation. For a surrogate pair (a 4-byte UTF-8 codepoint) both
/// surrogates map to the same sequence's start byte, so a caret cannot
/// land between them.
MARKOFF_CORE_EXPORT
qsizetype qtPosToByte(const QByteArray &utf8, qsizetype qtPos);

}  // namespace Markoff::TextUnits
